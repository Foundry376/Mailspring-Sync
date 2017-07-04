
//  SyncWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
#include <algorithm>

#include "SyncWorker.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"
#include "Label.hpp"
#include "File.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "Account.hpp"
#include "constants.h"

#define TEN_MINUTES         60 * 10

using namespace mailcore;
using namespace std;

class Progress : public IMAPProgressCallback {
public:
    void bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
//        cout << "Progress: " << current << "\n";
    }
    
    void itemsProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
//        cout << "Progress on Item: " << current << "\n";
    }
};


SyncWorker::SyncWorker(string name, shared_ptr<Account> account, CommStream * stream) :
    store(new MailStore()),
    account(account),
    unlinkPhase(1),
    stream(stream),
    logger(spdlog::get(name)),
    processor(new MailProcessor(store)),
    session(IMAPSession())
{
    MailUtils::configureSessionForAccount(session, account);
    store->addObserver(stream);
}

void SyncWorker::idleInterrupt()
{
    // called on main thread to interrupt idle
    std::unique_lock<std::mutex> lck(idleMtx);
    idleShouldReloop = true;
    session.interruptIdle();
    idleCv.notify_one();
}

void SyncWorker::idleQueueBodiesToSync(vector<string> & ids) {
    // called on main thread
    for (string & id : ids) {
        idleFetchBodyIDs.push_back(id);
    }
}

void SyncWorker::idleCycle()
{
    // called on dedicated thread
    
    while(true) {
        // Run body requests
        while (idleFetchBodyIDs.size() > 0) {
            string id = idleFetchBodyIDs.back();
            idleFetchBodyIDs.pop_back();
            Query byId = Query().equal("id", id);
            auto msg = store->find<Message>(byId);
            if (msg.get() != nullptr) {
                logger->info("Fetching body for message ID {}", msg->id());
                syncMessageBody(msg.get());
            }
        }

        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }

        // Run tasks ready for perform Remote
        Query q = Query().equal("status", "remote");
        auto tasks = store->findAll<Task>(q);
        TaskProcessor processor { store, &session };
        for (const auto task : tasks) {
            processor.performRemote(task.get());
        }

        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }
        q = Query().equal("role", "inbox");
        auto inbox = store->find<Folder>(q);
        if (inbox.get() == nullptr) {
            Query q = Query().equal("role", "all");
            inbox = store->find<Folder>(q);
            if (inbox.get() == nullptr) {
                logger->error("No inbox to idle on!");
                return;
            }
        }

        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }
        ErrorCode err = ErrorCode::ErrorNone;
        session.connectIfNeeded(&err);
        if (err != ErrorCode::ErrorNone) {
            throw err;
        }

        // Check for mail in the folder
        
        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }
        auto refreshedFolders = syncFoldersAndLabels();
        q = Query().equal("id", inbox->id());
        inbox = store->find<Folder>(q);
        if (inbox.get() == nullptr) {
            logger->error("Idling folder has disappeared? That's weird...");
            return;
        }
        
        // TODO: We should probably not do this if it's only been ~5 seconds since the last time
        String path = AS_MCSTR(inbox->path());
        IMAPFolderStatus remoteStatus = session.folderStatus(&path, &err);
        if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
            syncFolderChangesViaCondstore(*inbox, remoteStatus);
        } else {
            uint32_t uidnext = remoteStatus.uidNext();
            uint32_t syncedMinUID = inbox->localStatus()["syncedMinUID"].get<uint32_t>();
            uint32_t bottomUID = store->fetchMessageUIDAtDepth(*inbox, 100, uidnext);
            if (bottomUID < syncedMinUID) { bottomUID = syncedMinUID; }
            syncFolderUIDRange(*inbox, RangeMake(bottomUID, uidnext - bottomUID), false);
            inbox->localStatus()["lastShallow"] = time(0);
            inbox->localStatus()["uidnext"] = uidnext;
        }
        syncMessageBodies(*inbox, remoteStatus);
        store->save(inbox.get());

        // Idle on the folder
        
        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }
        if (session.setupIdle()) {
            logger->info("Idling on folder {}", inbox->path());
            String path = AS_MCSTR(inbox->path());
            session.idle(&path, 0, &err);
            session.unsetupIdle();
            logger->info("Idle exited with code {}", err);
        } else {
            logger->info("Connection does not support idling. Locking until more to do...");
            std::unique_lock<std::mutex> lck(idleMtx);
            idleCv.wait(lck);
        }
    }
}

// Background Behaviors

bool SyncWorker::syncNow()
{
    // allocated mailcore objects freed when `pool` is removed from the stack.
    // We create pools within sync but they stack so why not..
    AutoreleasePool pool;
    bool syncAgainImmediately = false;

    vector<shared_ptr<Folder>> folders = syncFoldersAndLabels();
    bool hasCondstore = session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore);
    bool hasQResync = session.storedCapabilities()->containsIndex(IMAPCapabilityQResync);

    // Identify folders to sync. On Gmail, labels are mapped to IMAP folders and
    // we only want to sync all, spam, and trash.
    
    array<string, 7> roleOrder{"inbox", "sent", "drafts", "all", "archive", "trash", "spam"};
    sort(folders.begin(), folders.end(), [&roleOrder](const shared_ptr<Folder> lhs, const shared_ptr<Folder> rhs) {
        ptrdiff_t lhsRank = find(roleOrder.begin(), roleOrder.end(), lhs->role()) - roleOrder.begin();
        ptrdiff_t rhsRank = find(roleOrder.begin(), roleOrder.end(), rhs->role()) - roleOrder.begin();
        return lhsRank < rhsRank;
    });
    
    for (auto & folder : folders) {
        json & localStatus = folder->localStatus();
        
        String path = AS_MCSTR(folder->path());
        ErrorCode err = ErrorCode::ErrorNone;
        IMAPFolderStatus remoteStatus = session.folderStatus(&path, &err);
        if (err) {
            throw err;
        }

        // Step 1: Check folder UIDValidity
        if (localStatus.empty()) {
            // We're about to fetch the top N UIDs in the folder and start working backwards in time.
            // When we eventually finish and start using CONDSTORE, this will be the highestmodseq
            // from the /oldest/ synced block of UIDs, ensuring we see changes.
            localStatus["highestmodseq"] = remoteStatus.highestModSeqValue();
            localStatus["uidvalidity"] = remoteStatus.uidValidity();
            localStatus["uidnext"] = remoteStatus.uidNext();
            localStatus["syncedMinUID"] = remoteStatus.uidNext();
            localStatus["lastShallow"] = 0;
            localStatus["lastDeep"] = 0;
        }
        
        if (localStatus["uidvalidity"].get<uint32_t>() != remoteStatus.uidValidity()) {
            throw "blow up the world";
        }
        
        // Step 2: Initial sync. Until we reach UID 1, we grab chunks of messages
        uint32_t syncedMinUID = localStatus["syncedMinUID"].get<uint32_t>();
        uint32_t chunkSize = 5000;

        if (syncedMinUID > 1) {
            // The UID value space is sparse, meaning there can be huge gaps where there are no
            // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
            // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
            // ensuring we never bite off more than we can chew.
            uint32_t chunkMinUID = syncedMinUID > chunkSize ? syncedMinUID - chunkSize : 1;
            if (remoteStatus.messageCount() < chunkSize) {
                chunkMinUID = 1;
            }
            syncFolderUIDRange(*folder, RangeMake(chunkMinUID, syncedMinUID - chunkMinUID), true);
            localStatus["syncedMinUID"] = chunkMinUID;
            syncedMinUID = chunkMinUID;
        }
        
        // Step 3: A) Retrieve new messages  B) update existing messages  C) delete missing messages
        // CONDSTORE, when available, does A + B.
        // XYZRESYNC, when available, does C
        if (hasCondstore && hasQResync) {
            // Hooray! We never need to fetch the entire range to sync. Just look at highestmodseq / uidnext
            // and sync if we need to.
            syncFolderChangesViaCondstore(*folder, remoteStatus);
        } else {
            uint32_t remoteUidnext = remoteStatus.uidNext();
            bool newMessages = remoteUidnext > localStatus["uidnext"].get<uint32_t>();
            bool timeForDeepScan = (time(0) - localStatus["lastDeep"].get<time_t>() > 2 * 60);
            bool timeForShallowScan = !timeForDeepScan && (time(0) - localStatus["lastShallow"].get<time_t>() > 2 * 60);
            
            if (newMessages || timeForShallowScan) {
                uint32_t bottomUID = store->fetchMessageUIDAtDepth(*folder, 499, remoteUidnext);
                if (bottomUID < syncedMinUID) {
                    bottomUID = syncedMinUID;
                }
                syncFolderUIDRange(*folder, RangeMake(bottomUID, remoteUidnext - bottomUID), false);
                localStatus["lastShallow"] = time(0);
                localStatus["uidnext"] = remoteUidnext;
            }
            if (timeForDeepScan) {
                syncFolderUIDRange(*folder, RangeMake(syncedMinUID, UINT32_MAX), false);
                localStatus["lastShallow"] = time(0);
                localStatus["lastDeep"] = time(0);
                localStatus["uidnext"] = remoteUidnext;
            }
        }
            
        // Retrieve some message bodies. We do this concurrently with the full header
        // scan so the user sees snippets on some messages quickly.
        if (syncMessageBodies(*folder, remoteStatus)) {
            syncAgainImmediately = true;
        }
        if (syncedMinUID > 1) {
            syncAgainImmediately = true;
        }
        
        // Save the folder - note that helper methods mutate localStatus.
        store->save(folder.get());
    }
    
    // We've just unlinked a bunch of messages with PHASE A, now we'll delete the ones
    // with PHASE B. This ensures anything we /just/ discovered was missing gets one
    // cycle to appear in another folder before we decide it's really, really gone.
    unlinkPhase = unlinkPhase == 1 ? 2 : 1;
    logger->info("Sync loop deleting unlinked messages with phase {}.", unlinkPhase);
    processor->deleteMessagesStillUnlinkedFromPhase(unlinkPhase);
    
    logger->info("Sync loop complete.");
    
    return syncAgainImmediately;
}

vector<shared_ptr<Folder>> SyncWorker::syncFoldersAndLabels()
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    logger->info("Syncing folder list...");
    
    ErrorCode err = ErrorCode::ErrorNone;
    Array * remoteFolders = session.fetchAllFolders(&err);
    if (err) {
        logger->error("Could not fetch folder list. IMAP Error Occurred: {}", err);
        throw "syncFolders: An error occurred. ";
    }
    
    store->beginTransaction();

    Query q = Query();
    bool isGmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    auto allLocalFolders = store->findAllMap<Folder>(q, "id");
    auto allLocalLabels = store->findAllMap<Label>(q, "id");
    vector<shared_ptr<Folder>> foldersToSync{};
    
    for (int ii = remoteFolders->count() - 1; ii >= 0; ii--) {
        IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
        if (remote->flags() & IMAPFolderFlagNoSelect) {
            remoteFolders->removeObjectAtIndex(ii);
            continue;
        }

        string remoteRole = MailUtils::roleForFolder(remote);
        string remoteId = MailUtils::idForFolder(string(remote->path()->UTF8Characters()));
        string remotePath = remote->path()->UTF8Characters();
        
        shared_ptr<Folder> local;
        
        if (isGmail && (remoteRole != "all") && (remoteRole != "spam") && (remoteRole != "trash")) {
            // Treat as a label
            if (allLocalLabels.count(remoteId) > 0) {
                local = allLocalLabels[remoteId];
                allLocalLabels.erase(remoteId);
            } else {
                local = make_shared<Label>(Label(remoteId, account->id(), 0));
            }

        } else {
            // Treat as a folder
            if (allLocalFolders.count(remoteId) > 0) {
                local = allLocalFolders[remoteId];
                allLocalFolders.erase(remoteId);
            } else {
                local = make_shared<Folder>(Folder(remoteId, account->id(), 0));
            }
            foldersToSync.push_back(local);
        }
        
        if ((local->role() != remoteRole) || (local->path() != remotePath)) {
            local->setPath(remotePath);
            local->setRole(remoteRole);
            
            // place items into the thread counts table
            SQLite::Statement count(store->db(), "INSERT OR IGNORE INTO ThreadCounts (categoryId, unread, total) VALUES (?, 0, 0)");
            count.bind(1, local->id());
            count.exec();
            store->save(local.get());
        }
    }
    
    // delete any folders / labels no longer present on the remote
    for (auto const item : allLocalFolders) {
        SQLite::Statement count(store->db(), "DELETE FROM ThreadCounts WHERE categoryId = ?");
        count.bind(1, item.second->id());
        count.exec();
        store->remove(item.second.get());
    }
    for (auto const item : allLocalLabels) {
        SQLite::Statement count(store->db(), "DELETE FROM ThreadCounts WHERE categoryId = ?");
        count.bind(1, item.second->id());
        count.exec();
        store->remove(item.second.get());
    }

    store->commitTransaction();

    return foldersToSync;
}

IMAPMessagesRequestKind SyncWorker::fetchRequestKind(bool heavy) {
    bool gmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    
    if (heavy) {
        if (gmail) {
            return IMAPMessagesRequestKind(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);
        }
        return IMAPMessagesRequestKind(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    }

    if (gmail) {
        return IMAPMessagesRequestKind(IMAPMessagesRequestKindFlags | IMAPMessagesRequestKindGmailLabels);
    }
    return IMAPMessagesRequestKindFlags;
}

void SyncWorker::syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest)
{
    logger->info("syncFolderUIDRange - ({}, UIDs: {} - {}, Heavy: {})", folder.path(), range.location, range.location + range.length, heavyInitialRequest);
    
    AutoreleasePool pool;
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IndexSet * heavyNeeded = new IndexSet();
    IMAPProgressCallback * cb = new Progress();
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(folder.path()));
    time_t syncDataTimestamp = time(0);


    Array * remote = session.fetchMessagesByUID(&path, fetchRequestKind(heavyInitialRequest), set, cb, &err);
    if (err) {
        logger->error("syncFolderUIDRange - IMAP Error Occurred: {}", err);
        return;
    }
    
    // Step 2: Fetch the local attributes (unread, starred, etc.) for the same UID range
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(range, folder));
    clock_t lastSleepClock = clock();
    
    for (int ii = remote->count() - 1; ii >= 0; ii--) {
        // Never sit in a hard loop inserting things into the database for more than 250ms.
        // This ensures we don't starve another thread waiting for a database connection
        if (((clock() - lastSleepClock) * 4) / CLOCKS_PER_SEC > 1) {
            usleep(50000); // 50ms
            lastSleepClock = clock();
        }
        
        IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
        uint32_t remoteUID = remoteMsg->uid();

        // Step 3: Collect messages that are different or not in our local UID set.
        bool notInFolder = (local.count(remoteUID) == 0);
        bool notSame = (!MessageAttributesMatch(local[remoteUID], MessageAttributesForMessage(remoteMsg)));

        if (notInFolder || notSame) {
            // Step 4: Attempt to insert the new message. If we get unique exceptions,
            // look for the existing message and do an update instead. This happens whenever
            // a message has moved between folders or it's attributes have changed.
            
            // Note: We could prefetch all changedOrMissingIDs and then decide to update/insert,
            // but we can only query for 500 at a time, it /feels/ nasty, and we /could/ always
            // hit the exception anyway since another thread could be IDLEing and retrieving
            // the messages alongside us.
            if (heavyInitialRequest) {
                processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
            } else {
                heavyNeeded->addIndex(remoteUID);
            }
        }
        
        local.erase(remoteUID);
    }
    
    if (!heavyInitialRequest && heavyNeeded->count() > 0) {
        logger->error("syncFolderUIDRange - Fetching full headers for {}", heavyNeeded->count());

        syncDataTimestamp = time(0);
        remote = session.fetchMessagesByUID(&path, fetchRequestKind(true), heavyNeeded, cb, &err);
        if (err) {
            logger->error("syncFolderUIDRange - IMAP Error Occurred: {}", err);
            return;
        }
        for (int ii = remote->count() - 1; ii >= 0; ii--) {
            IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
            processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
        }
    }

    // Step 5: Unlink. The messages left in local map are the ones we had in the range,
    // which the server reported were no longer there. Remove their remoteUID.
    // We'll delete them later if they don't appear in another folder during sync.
    if (local.size() > 0) {
        vector<uint32_t> deletedUIDs {};
        for(auto const &ent : local) {
            deletedUIDs.push_back(ent.first);
        }
        Query qd = Query().equal("remoteFolderId", folder.id()).equal("remoteUID", deletedUIDs);
        auto deletedMsgs = store->findAll<Message>(qd);
        processor->unlinkMessagesFromFolder(deletedMsgs, unlinkPhase);
    }
}

void SyncWorker::syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    uint32_t uidnext = folder.localStatus()["uidnext"].get<uint32_t>();
    uint64_t modseq = folder.localStatus()["highestmodseq"].get<uint64_t>();
    uint64_t remoteModseq = remoteStatus.highestModSeqValue();
    uint32_t remoteUIDNext = remoteStatus.uidNext();
    time_t syncDataTimestamp = time(0);
    
    if (modseq == remoteModseq && uidnext == remoteUIDNext) {
        logger->info("syncFolderChangesViaCondstore - {}: highestmodseq, uidnext match, no changes.", folder.path());
        return;
    }

    logger->info("syncFolderChangesViaCondstore - {}: highestmodseq changed, requesting changes...", folder.path());
    
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(1, UINT64_MAX));
    IMAPProgressCallback * cb = new Progress();

    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folder.path()));
    
    IMAPSyncResult * result = session.syncMessagesByUID(&path, fetchRequestKind(true), uids, modseq, cb, &err);
    if (err != ErrorCode::ErrorNone) {
        logger->error("syncFolderChangesViaCondstore - IMAP Error Occurred: {}", err);
        return;
    }

    // for modified messages, fetch local copy and apply changes
    Array * modifiedOrAdded = result->modifiedOrAddedMessages();
    vector<string> modifiedOrAddedIDs = MailUtils::messageIdsOfArray(modifiedOrAdded);
    
    Query query = Query().equal("id", modifiedOrAddedIDs);
    map<string, shared_ptr<Message>> local = store->findAllMap<Message>(query, "id");

    for (int ii = 0; ii < modifiedOrAdded->count(); ii ++) {
        IMAPMessage * msg = (IMAPMessage *)modifiedOrAdded->objectAtIndex(ii);
        string id = MailUtils::idForMessage(msg);

        if (local.count(id) == 0) {
            // Found message with an ID we've never seen in any folder. Add it!
            processor->insertFallbackToUpdateMessage(msg, folder, syncDataTimestamp);
        } else {
            // Found message with an existing ID. Update it's attributes & folderId.
            // Note: Could potentially have moved from another folder!
            processor->updateMessage(local[id].get(), msg, folder, syncDataTimestamp);
        }
    }
    
    // for deleted messages, collect UIDs and destroy. Note: vanishedMessages is only
    // populated when QRESYNC is available.
    if (result->vanishedMessages() != NULL) {
        vector<uint32_t> deletedUIDs = MailUtils::uidsOfIndexSet(result->vanishedMessages());
        logger->info("There have been {} messages removed", deletedUIDs.size());
        Query query = Query().equal("remoteFolderId", folder.id()).equal("remoteUID", deletedUIDs);
        auto deletedMsgs = store->findAll<Message>(query);
        processor->unlinkMessagesFromFolder(deletedMsgs, unlinkPhase);
    }

    folder.localStatus()["uidnext"] = remoteUIDNext;
    folder.localStatus()["highestmodseq"] = remoteModseq;
}

/*
 Syncs the top N missing message bodies. Returns true if it did work, false if it did nothing.
 */
bool SyncWorker::syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus) {
    // who needs this stuff? probably nobody.
    if ((folder.role() == "spam") || (folder.role() == "trash")) {
        return false;
    }

    SQLite::Statement missing(store->db(), "SELECT Message.* FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.remoteFolderId = ? AND (Message.date > ? OR Message.draft = 1) AND MessageBody.value IS NULL ORDER BY Message.date DESC LIMIT 20");
    missing.bind(1, folder.id());
    missing.bind(2, (double)(time(0) - 24 * 60 * 60 * 30)); // one month TODO pref!
    vector<Message> results{};
    while (missing.executeStep()) {
        results.push_back(Message(missing));
    }
    
    for (auto result : results) {
        syncMessageBody(&result);
    }
    
    return results.size() > 0;
}

void SyncWorker::syncMessageBody(Message * message) {
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    IMAPProgressCallback * cb = new Progress();
    ErrorCode err = ErrorCode::ErrorNone;
    string folderPath = message->remoteFolder()["path"].get<string>();
    String path(AS_MCSTR(folderPath));
    
    Data * data = session.fetchMessageByUID(&path, message->remoteUID(), cb, &err);
    if (err) {
        logger->error("syncMessageBody - IMAP Error {} fetching body for {} ({})", err, message->subject(), message->remoteUID());
        return;
    }
    MessageParser * messageParser = MessageParser::messageParserWithData(data);
    processor->retrievedMessageBody(message, messageParser);
}
