
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


#define AS_MCSTR(X)         mailcore::String(X.c_str())

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
    session.setUsername(MCSTR("cypresstest@yahoo.com"));
    session.setPassword(MCSTR("IHate2Gmail"));
    session.setHostname(MCSTR("imap.mail.yahoo.com"));
    session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    session.setPort(993);
    
    store->addObserver(stream);
}

void SyncWorker::syncNow()
{
    vector<shared_ptr<Folder>> folders = syncFolders();
    
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
            localStatus["uidvalidity"] = remoteStatus.uidValidity();
        }
        if (localStatus["uidvalidity"].get<uint32_t>() != remoteStatus.uidValidity()) {
            throw "blow up the world";
        }
        
        // Step 2: Decide which sync approach to use.
        time_t lastDeepScan = localStatus.count("last_deep_scan") ? localStatus["last_deep_scan"].get<time_t>() : 0;

        if (time(0) - lastDeepScan > 5000) {
            // Retrieve all attributes of all messages in the folder. We need to do this initially,
            // and then we need to do it periodically to find deleted messages.
            syncFolderFullScan(*folder, remoteStatus);
        } else {
            // Retrieve all changes to the mailbox if possible. Otherwise, do a shallow sync of
            // the last N messages.
            if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
                syncFolderChangesViaCondstore(*folder, remoteStatus);
            } else {
                syncFolderShallow(*folder, remoteStatus);
            }
        }
        
        store->save(folder.get());
    }
    
    logger->info("Sync loop complete.");
}

vector<shared_ptr<Folder>> SyncWorker::syncFolders()
{
    logger->info("Syncing folder list...");
    
    ErrorCode err = ErrorCode::ErrorNone;
    Array * results = session.fetchAllFolders(&err);
    if (err) {
        logger->error("Could not fetch folder list. ", err);
        throw "syncFolders: An error occurred. ";
    }
    
    Query q = Query();
    auto allLocalFolders = store->findAllMap<Folder>(q, "id");

    vector<shared_ptr<Folder>> collected{};
    
    for (int ii = results->count() - 1; ii >= 0; ii--) {
        IMAPFolder * remoteFolder = (IMAPFolder *)results->objectAtIndex(ii);
        if (remoteFolder->flags() & IMAPFolderFlagNoSelect) {
            results->removeObjectAtIndex(ii);
            continue;
        }

        string remoteId = MailUtils::idForFolder(remoteFolder);
        string remoteRole = MailUtils::roleForFolder(remoteFolder);
        string remotePath = remoteFolder->path()->UTF8Characters();
        
        shared_ptr<Folder> localFolder;
        if (allLocalFolders.count(remoteId) > 0) {
            localFolder = allLocalFolders[remoteId];
            
            if ((localFolder->role() != remoteRole) || (localFolder->role() != remoteRole)) {
                localFolder->setPath(remotePath);
                localFolder->setRole(remoteRole);
                store->save(localFolder.get());
            }
        } else {
            localFolder = make_shared<Folder>(Folder(remoteId, "1", 0));
            localFolder->setPath(remotePath);
            localFolder->setRole(remoteRole);
            store->save(localFolder.get());
        }
        
        collected.push_back(localFolder);
        allLocalFolders.erase(remoteId);
    }
    
    // delete any folders no longer present on the remote
    for (auto const item : allLocalFolders) {
        store->remove(item.second.get());
    }
    
    return collected;
}


/*
 Pull down all message attributes in the folder. For each range, compare against our local
 versions to determine 1) new, 2) changed, 3) deleted.
 */
void SyncWorker::syncFolderFullScan(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    uint32_t chunkSize = 10000;
    uint32_t uidMax = remoteStatus.uidNext();
    uint64_t modSeq = remoteStatus.highestModSeqValue();

    // The UID value space is sparse, meaning there can be huge gaps where there are no
    // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
    // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
    // ensuring we never bite off more than we can chew.
    if (remoteStatus.messageCount() < chunkSize) {
        syncFolderRange(folder, RangeMake(1, uidMax));
    } else {
        while (uidMax > 1) {
            uint32_t uidMin = uidMax > chunkSize ? uidMax - chunkSize : 1;
            syncFolderRange(folder, RangeMake(uidMin, uidMax - uidMin));
            uidMax = uidMin;
        }
    }
    
    // The values we /started/ paginating with, unsure if they change behind our back
    folder.localStatus()["highestmodseq"] = modSeq;
    folder.localStatus()["uidnext"] = uidMax;
    folder.localStatus()["last_deep_scan"] = time(0);
}

/*
 Pull down just the most recent N messages in the folder, where N is from the current
 UIDNext of the folder down to the 500th message we previously synced.
 */
void SyncWorker::syncFolderShallow(Folder & folder, IMAPFolderStatus & remoteStatus)
{    
    int uidStart = store->fetchMessageUIDAtDepth(folder, 499);
    int uidNext = remoteStatus.uidNext();
    syncFolderRange(folder, RangeMake(uidStart, uidNext - uidStart));
    folder.localStatus()["uidnext"] = uidNext;
}

void SyncWorker::syncFolderRange(Folder & folder, Range range)
{
    logger->info("Syncing folder {} (UIDS {} - {})", folder.path(), range.location, range.location + range.length);
    
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IMAPProgressCallback * cb = new Progress();

    // Step 1: Fetch the remote UID range
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(folder.path()));
    Array * remote = session.fetchMessagesByUID(&path, kind, set, cb, &err);
    if (err) {
        logger->error("Could not fetch messages in folder.", err);
        return;
    }

    // Step 2: Fetch the local attributes (unread, starred, etc.) for the same UID range
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(range, folder));
    vector<uint32_t> localUIDsModified {};
    
    for (int ii = 0; ii < remote->count(); ii++) {
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
    uint64_t modseq = folder.localStatus()["highestmodseq"].get<uint64_t>();
    uint64_t remoteModseq = remoteStatus.highestModSeqValue();
    
    if (modseq == remoteModseq) {
        logger->info("Syncing folder {}: highestmodseq matches, no changes.", folder.path());
        return;
    }

    logger->info("Syncing folder {}: highestmodseq changed, requesting changes...", folder.path());
    
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(1, UINT64_MAX));
    IMAPProgressCallback * cb = new Progress();

    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folder.path()));
    IMAPSyncResult * result = session.syncMessagesByUID(&path, kind, uids, modseq, cb, &err);

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

    folder.localStatus()["highestmodseq"] = remoteModseq;
}


