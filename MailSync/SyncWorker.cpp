
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


SyncWorker::SyncWorker() :
    store(new MailStore()),
    stream(new CommStream((char *)"/tmp/cmail.sock")),
    logger(spdlog::stdout_color_mt("console")),
    processor(new MailProcessor(store)),
    session(IMAPSession())
{
//    session.setUsername(MCSTR("cypresstest@yahoo.com"));
//    session.setPassword(MCSTR("IHate2Gmail"));
//    session.setHostname(MCSTR("imap.mail.yahoo.com"));
//    session.setConnectionType(ConnectionType::ConnectionTypeTLS);
//    session.setPort(993);
    session.setUsername(MCSTR("bengotow@gmail.com"));
    session.setPassword(MCSTR("akvaiewqwhdzcgmbh"));
    session.setHostname(MCSTR("imap.gmail.com"));
    session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    session.setPort(993);
    
    store->addObserver(stream);
}

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

        if (fullScanInProgress == false) {
            // Retrieve CONDSTORE changes to the mailbox if possible.
            // Otherwise, do a shallow sync of the last N messages.
            if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
                syncFolderChangesViaCondstore(*folder, remoteStatus);
            } else {
                syncFolderChangesViaShallowScan(*folder, remoteStatus);
            }
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
            store->save(local.get());
        }
    }
    
    // delete any folders / labels no longer present on the remote
    for (auto const item : allLocalFolders) {
        store->remove(item.second.get());
    }
    for (auto const item : allLocalLabels) {
        store->remove(item.second.get());
    }
    
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
    // For other accounts we deep scan every ten minutes to find flag changes / deletions deep in thh folder.
    uint32_t deepScanMinUID = ls.count("deepScanMinUID") ? ls["deepScanMinUID"].get<uint32_t>() : UINT32_MAX;
    time_t deepScanTime = ls.count("deepScanTime") ? ls["deepScanTime"].get<time_t>() : 0;
    bool condstoreSupported = session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore);

    if ((deepScanMinUID == UINT32_MAX) || (!condstoreSupported && (time(0) - deepScanTime > TEN_MINUTES))) {
        ls["uidnext"] = remoteStatus.uidNext();
        deepScanMinUID = remoteStatus.uidNext();
    }
    
    if (deepScanMinUID == 1) {
        return false;
    }

    // The UID value space is sparse, meaning there can be huge gaps where there are no
    // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
    // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
    // ensuring we never bite off more than we can chew.
    uint32_t chunkSize = 5000;
    uint32_t nextMinUID = deepScanMinUID > chunkSize ? deepScanMinUID - chunkSize : 1;
    if (remoteStatus.messageCount() < chunkSize) {
        nextMinUID = 1;
    }
    
    syncFolderRange(folder, RangeMake(nextMinUID, deepScanMinUID - nextMinUID));

    // The values we /started/ paginating with, unsure if they change behind our back
    // As long as deepScanMinId is still > 1, this function will be run again
    ls["deepScanMinUID"] = nextMinUID;
    ls["deepScanTime"] = time(0);
    
    return true;
}

/*
 Pull down just the most recent N messages in the folder, where N is from the current
 UIDNext of the folder down to the 500th message we previously synced.
 */
void SyncWorker::syncFolderChangesViaShallowScan(Folder & folder, IMAPFolderStatus & remoteStatus)
{    
    int uidStart = store->fetchMessageUIDAtDepth(folder, 499);
    int uidNext = remoteStatus.uidNext();
    syncFolderRange(folder, RangeMake(uidStart, uidNext - uidStart));
    folder.localStatus()["uidnext"] = uidNext;
}

void SyncWorker::syncFolderRange(Folder & folder, Range range)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    logger->info("Syncing folder {} (UIDS {} - {})", folder.path(), range.location, range.location + range.length);
    
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IMAPProgressCallback * cb = new Progress();

    // Step 1: Fetch the remote UID range
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(folder.path()));
    Array * remote = session.fetchMessagesByUID(&path, KIND_ALL_HEADERS, set, cb, &err);
    if (err) {
        logger->error("IMAP Error Occurred: {}", err);
        return;
    }

    // Step 2: Fetch the local attributes (unread, starred, etc.) for the same UID range
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(range, folder));
    vector<uint32_t> localUIDsModified {};
    
    for (int ii = remote->count() - 1; ii >= 0; ii--) {
        IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
        uint32_t remoteUID = remoteMsg->uid();

        // Step 3: Process new messages, mark when we find existing messages
        // with incorrect / out-of-date attributes.
        if (local.count(remoteUID) == 0) {
            processor->insertMessage(remoteMsg, folder);

        } else if (!MessageAttributesMatch(local[remoteUID], MessageAttributesForMessage(remoteMsg))) {
            localUIDsModified.push_back(remoteUID);
        }
        
        local.erase(remoteUID);
    }
    
    // Step 4: Retrieve and apply updates to existing messages. They need to be loaded fully
    // so we can change their flags and generate the event.
    Query modified = Query().equal("folderId", folder.id()).equal("folderImapUID", localUIDsModified);
    auto localMessages = store->findAllUINTMap<Message>(modified, "folderImapUID");
    
    for (int ii = 0; ii < remote->count(); ii++) {
        IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
        uint32_t remoteUID = remoteMsg->uid();
        
        if (localMessages.count(remoteUID) > 0) {
            // Updating existing message attributes
            processor->updateMessage(localMessages[remoteUID].get(), remoteMsg, folder);
        }
    }
    
    // Step 5: Delete. The messages left in local map are the ones the server didn't give us.
    // They have been deleted. Unpersist them.
    vector<uint32_t> deletedUIDs {};
    for(auto const &ent : local) {
        deletedUIDs.push_back(ent.first);
    }
    Query deleted = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
    store->remove<Message>(deleted);
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
    Array * modified = result->modifiedOrAddedMessages();
    vector<uint32_t> modifiedUIDs = MailUtils::uidsOfArray(modified);
    
    Query query = Query()
        .equal("folderId", folder.id())
        .equal("folderImapUID", modifiedUIDs);
    map<uint32_t, shared_ptr<Message>> local = store->findAllUINTMap<Message>(query, "folderImapUID");

    for (int ii = 0; ii < modified->count(); ii ++) {
        IMAPMessage * modifiedMsg = (IMAPMessage *)modified->objectAtIndex(ii);
        uint32_t uid = modifiedMsg->uid();

        if (local.count(uid) == 0) {
            // Found new message
            processor->insertMessage(modifiedMsg, folder);
        } else {
            // Updating existing message attributes
            processor->updateMessage(local[uid].get(), modifiedMsg, folder);
        }
    }
    
    // for deleted messages, collect UIDs and destroy
    if (result->vanishedMessages() != NULL) {
        vector<uint32_t> deletedUIDs = MailUtils::uidsOfIndexSet(result->vanishedMessages());
        Query query = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
        store->remove<Message>(query);
    }

    folder.localStatus()["uidnext"] = remoteUIDNext;
    folder.localStatus()["highestmodseq"] = remoteModseq;
}

/*
 Syncs the top N missing message bodies. Returns true if it did work, false if it did nothing.
 */
bool SyncWorker::syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus) {
    SQLite::Statement missing(store->db(), "SELECT Message.* FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.folderId = ? AND MessageBody.value IS NULL ORDER BY Message.date DESC LIMIT 10");
    missing.bind(1, folder.id());
    vector<Message> results{};
    while (missing.executeStep()) {
        results.push_back(Message(missing));
    }
    
    for (auto result : results) {
        syncMessageBody(folder, result);
    }
    
    return results.size() > 0;
}

void SyncWorker::syncMessageBody(Folder & folder, Message & message) {
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    IMAPProgressCallback * cb = new Progress();
    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folder.path()));
    
    Data * data = session.fetchMessageByUID(&path, message.folderImapUID(), cb, &err);
    if (err) {
        logger->error("IMAP Error Occurred: {}", err);
        return;
    }
    MessageParser * messageParser = MessageParser::messageParserWithData(data);
    String * text = messageParser->plainTextBodyRendering(true);
    String * html = messageParser->htmlBodyRendering();

    // write it to the MessageBodies table
    SQLite::Statement insert(store->db(), "INSERT INTO MessageBody (id, value) VALUES (?, ?)");
    insert.bind(1, message.id());
    insert.bind(2, html->UTF8Characters());
    insert.exec();
    
    // write the message snippet. This also gives us the database trigger!
    message.setSnippet(text->substringToIndex(400)->UTF8Characters());
    store->save(&message);
}
