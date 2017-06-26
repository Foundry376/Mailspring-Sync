
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

#define AS_MCSTR(X)         mailcore::String(X.c_str())
#define KIND_ALL_HEADERS    (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID)
#define TEN_MINUTES         60 * 10

using namespace mailcore;
using namespace std;

class Progress : public IMAPProgressCallback {
public:
    void bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
        cout << "Progress: " << current << "\n";
    }
    
    void itemsProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
        cout << "Progress on Item: " << current << "\n";
    }
};


SyncWorker::SyncWorker(string name, CommStream * stream) :
    store(new MailStore()),
    stream(stream),
    logger(spdlog::stdout_color_mt(name)),
    processor(new MailProcessor(name + "_p", store)),
    session(IMAPSession())
{
//    session.setUsername(MCSTR("cypresstest@yahoo.com"));
//    session.setPassword(MCSTR("IHate2Gmail"));
//    session.setHostname(MCSTR("imap.mail.yahoo.com"));
//    session.setConnectionType(ConnectionType::ConnectionTypeTLS);
//    session.setPort(993);
    session.setUsername(MCSTR("bengotow@gmail.com"));
    session.setPassword(MCSTR("kvaiewqwhdzcgmbh"));
    session.setHostname(MCSTR("imap.gmail.com"));
    session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    session.setPort(993);
    
    preferredChunkSize = 200;

    store->addObserver(stream);
}

void SyncWorker::idleInterrupt()
{
    idleShouldReloop = true;
    session.interruptIdle();
}

void SyncWorker::idleQueueBodiesToSync(vector<string> & ids) {
    idleShouldReloop = true;
    for (string & id : ids) {
        idleFetchBodyIDs.push_back(id);
    }
}

void SyncWorker::idleCycle()
{
    while(true) {
        // Run body requests
        while (idleFetchBodyIDs.size() > 0) {
            string id = idleFetchBodyIDs.back();
            idleFetchBodyIDs.pop_back();
            Query byId = Query().equal("id", id);
            auto msg = store->find<Message>(byId);
            if (msg.get() != nullptr) {
                logger->info("Fetching body for message ID {}", msg->id());
                auto folderPath = msg->folder()["path"].get<string>();
                syncMessageBody(folderPath, msg.get());
            }
        }

        if (idleShouldReloop) {
            idleShouldReloop = false;
            continue;
        }

        // Run tasks ready for perform Remote
        Query q = Query().equal("status", "remote");
        auto tasks = store->findAll<Task>(q);
        TaskProcessor processor { store, logger, &session };
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
            syncFolderChangesViaShallowScan(*inbox, remoteStatus);
        }
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
        }
        if (localStatus["uidvalidity"].get<uint32_t>() != remoteStatus.uidValidity()) {
            throw "blow up the world";
        }
        
        // Retrieve all attributes of all messages in the folder. We need to do this initially,
        // and then we need to do it periodically to find deleted messages.
        bool fullScanInProgress = syncFolderFullScanIncremental(*folder, remoteStatus);

        // Retrieve changes, at least to the last N messages or via CONDSTORE when possible.
        if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
            syncFolderChangesViaCondstore(*folder, remoteStatus);
        } else {
            syncFolderChangesViaShallowScan(*folder, remoteStatus);
        }

        // Retrieve some message bodies. We do this concurrently with the full header
        // scan so the user sees snippets on some messages quickly.
        bool bodiesInProgress = syncMessageBodies(*folder, remoteStatus);
        
        syncAgainImmediately = syncAgainImmediately || bodiesInProgress || fullScanInProgress;

        store->save(folder.get());
    }
    
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
    
    // Look through the folders for a Gmail /all folder
    bool allFolderPresent = false;
    for (int ii = remoteFolders->count() - 1; ii >= 0; ii--) {
        IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
        string remoteRole = MailUtils::roleForFolder(remote);
        if (remoteRole == "all") {
            allFolderPresent = true;
            break;
        }
    }
    
    store->beginTransaction();

    Query q = Query();
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
        string remoteId = MailUtils::idForFolder(remote);
        string remotePath = remote->path()->UTF8Characters();
        
        shared_ptr<Folder> local;
        
        if (allFolderPresent && (remoteRole != "all") && (remoteRole != "spam") && (remoteRole != "trash")) {
            // Treat as a label
            if (allLocalLabels.count(remoteId) > 0) {
                local = allLocalLabels[remoteId];
                allLocalLabels.erase(remoteId);
            } else {
                local = make_shared<Label>(Label(remoteId, "1", 0));
            }

        } else {
            // Treat as a folder
            if (allLocalFolders.count(remoteId) > 0) {
                local = allLocalFolders[remoteId];
                allLocalFolders.erase(remoteId);
            } else {
                local = make_shared<Folder>(Folder(remoteId, "1", 0));
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

/*
 Pull down all message attributes in the folder. For each range, compare against our local
 versions to determine 1) new, 2) changed, 3) deleted.
 
 @return True if work was performed, False if finished.
 */
bool SyncWorker::syncFolderFullScanIncremental(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    json & ls = folder.localStatus();

    // For condstore accounts, we only do a deep scan once
    // For other accounts we deep scan every ten minutes to find flag changes / deletions deep in the folder.
    unsigned long fullScanHead = ls.count("fullScanHead") ? ls["fullScanHead"].get<unsigned long>() : UINT32_MAX;
    time_t fullScanTime = ls.count("fullScanTime") ? ls["fullScanTime"].get<time_t>() : 0;
    bool condstoreSupported = session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore);

    if ((fullScanHead == UINT32_MAX) || (!condstoreSupported && (time(0) - fullScanTime > TEN_MINUTES))) {
        // Identify the message number that corresponds to uidnext.
        // We'll begin paging backwards from there.
        uint32_t uidnext = remoteStatus.uidNext();
        String path = AS_MCSTR(folder.path());
        ErrorCode err = ErrorCode::ErrorNone;
        HashMap * map = session.fetchMessageNumberUIDMapping(&path, uidnext - 1, uidnext - 1, &err);
        Value * v = (Value*)map->allKeys()->objectAtIndex(0);
        fullScanHead = v->unsignedLongValue();
        ls["uidnext"] = uidnext;

    }
    
    if (fullScanHead == 1) {
        return false;
    }

    // The UID value space is sparse, meaning there can be huge gaps where there are no
    // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
    // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
    // ensuring we never bite off more than we can chew.
    unsigned long chunkNextHead = fullScanHead > preferredChunkSize ? fullScanHead - preferredChunkSize : 1;
    if (remoteStatus.messageCount() < preferredChunkSize) {
        chunkNextHead = 1;
    }

    syncFolderSequenceRange(folder, RangeMake(chunkNextHead, fullScanHead - chunkNextHead));
    
    // The values we /started/ paginating with, unsure if they change behind our back
    // As long as deepScanMinId is still > 1, this function will be run again
    ls["fullScanHead"] = chunkNextHead;
    ls["fullScanTime"] = time(0);
    
    return true;
}

/*
 Pull down just the most recent N messages in the folder, where N is from the current
 UIDNext of the folder down to the 500th message we previously synced.
 */
void SyncWorker::syncFolderChangesViaShallowScan(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    // Todo - just use UIDs here?
    uint32_t uidnext = remoteStatus.uidNext();
    String path = AS_MCSTR(folder.path());
    ErrorCode err = ErrorCode::ErrorNone;
    HashMap * map = session.fetchMessageNumberUIDMapping(&path, uidnext - 1, uidnext - 1, &err);
    Value * v = (Value*)map->allKeys()->objectAtIndex(0);
    unsigned long shallowScanHead = v->unsignedLongValue();
    unsigned long shallowScanMin = shallowScanHead > 500 ? shallowScanHead - 500 : 1;
    
    syncFolderSequenceRange(folder, RangeMake(shallowScanMin, shallowScanHead - shallowScanMin));
    folder.localStatus()["uidnext"] = uidnext;
}

void SyncWorker::syncFolderSequenceRange(Folder & folder, Range range)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    logger->info("Syncing folder {} (Numbers {} - {})", folder.path(), range.location, range.location + range.length);
    
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IMAPProgressCallback * cb = new Progress();

    // Step 1: Fetch the remote UID range
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(folder.path()));
    Array * remote = session.fetchMessagesByNumber(&path, KIND_ALL_HEADERS, set, cb, &err);
    if (err) {
        logger->error("IMAP Error Occurred: {}", err);
        return;
    }
    if (remote->count() == 0) {
        logger->error("Request for messages in number range returned none.");
        return;
    }
    
    // TODO: Use numbers instead, calculate ideal chunk size based on whatever takes 1 second
    // to insert in database,
    // Step 2: Fetch the local attributes (unread, starred, etc.) for the same UID range
    uint32_t minUID = ((IMAPMessage*)remote->objectAtIndex(0))->uid();
    uint32_t maxUID = ((IMAPMessage*)remote->lastObject())->uid();
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(RangeMake(minUID, maxUID - minUID), folder));
    clock_t startClock = clock();
    
    for (int ii = remote->count() - 1; ii >= 0; ii--) {
        // Never sit in a hard loop inserting things into the database for more than a second.
        // This ensures we don't starve another thread waiting for a database connection
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
            processor->insertFallbackToUpdateMessage(remoteMsg, folder);
        }
        
        local.erase(remoteUID);
    }
    
    // Step 5: Unlink. The messages left in local map are the ones we had in the range,
    // which the server reported were no longer there. Remove their folderImapUID.
    // We'll delete them later if they don't appear in another folder during sync.
    if (local.size() > 0) {
        vector<uint32_t> deletedUIDs {};
        for(auto const &ent : local) {
            deletedUIDs.push_back(ent.first);
        }
        Query qd = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
        auto deletedMsgs = store->findAll<Message>(qd);
        processor->unlinkMessagesFromFolder(deletedMsgs);
    }
    
    double secElapsed = (double((clock() - startClock)) / CLOCKS_PER_SEC);
    preferredChunkSize = (double)remote->count() / secElapsed;
    preferredChunkSize = min(max(preferredChunkSize, 25), 400);
    
    logger->info("Applying range took {}, updating preferred chunk size to {}", secElapsed, preferredChunkSize);
}

void SyncWorker::syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    uint64_t modseq = folder.localStatus()["highestmodseq"].get<uint64_t>();
    uint64_t remoteModseq = remoteStatus.highestModSeqValue();
    uint32_t remoteUIDNext = remoteStatus.uidNext();

    if (modseq == remoteModseq) {
        logger->info("Syncing folder {}: highestmodseq matches, no changes.", folder.path());
        return;
    }

    logger->info("Syncing folder {}: highestmodseq changed, requesting changes...", folder.path());
    
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(1, UINT64_MAX));
    IMAPProgressCallback * cb = new Progress();

    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folder.path()));
    IMAPSyncResult * result = session.syncMessagesByUID(&path, KIND_ALL_HEADERS, uids, modseq, cb, &err);
    if (err != ErrorCode::ErrorNone) {
        logger->error("IMAP Error Occurred: {}", err);
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
            processor->insertFallbackToUpdateMessage(msg, folder);
        } else {
            // Found message with an existing ID. Update it's attributes & folderId.
            // Note: Could potentially have moved from another folder!
            processor->updateMessage(local[id].get(), msg, folder);
        }
    }
    
    // for deleted messages, collect UIDs and destroy
    if (result->vanishedMessages() != NULL) {
        vector<uint32_t> deletedUIDs = MailUtils::uidsOfIndexSet(result->vanishedMessages());
        Query query = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
        auto deletedMsgs = store->findAll<Message>(query);
        processor->unlinkMessagesFromFolder(deletedMsgs);
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

    SQLite::Statement missing(store->db(), "SELECT Message.* FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.folderId = ? AND Message.date > ? AND MessageBody.value IS NULL ORDER BY Message.date DESC LIMIT 10");
    missing.bind(1, folder.id());
    missing.bind(2, (double)(time(0) - 24 * 60 * 60 * 30)); // one month TODO pref!
    vector<Message> results{};
    while (missing.executeStep()) {
        results.push_back(Message(missing));
    }
    
    for (auto result : results) {
        syncMessageBody(folder.path(), &result);
    }
    
    return results.size() > 0;
}

void SyncWorker::syncMessageBody(string folderPath, Message * message) {
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    IMAPProgressCallback * cb = new Progress();
    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folderPath));
    
    Data * data = session.fetchMessageByUID(&path, message->folderImapUID(), cb, &err);
    if (err) {
        logger->error("IMAP Error Occurred: {}", err);
        return;
    }
    MessageParser * messageParser = MessageParser::messageParserWithData(data);
    processor->retrievedMessageBody(message, messageParser);
}
