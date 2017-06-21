
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
            localStatus["fetchedmin"] = remoteStatus.uidNext();
            localStatus["fetchedmax"] = remoteStatus.uidNext();
            localStatus["highestmodseq"] = remoteStatus.highestModSeqValue();
        }
        if (localStatus["uidvalidity"].get<uint32_t>() != remoteStatus.uidValidity()) {
            throw "blow up the world";
        }
        
        // Step 2: Decide which sync approach to use.
        time_t lastDeepScan = localStatus.count("last_deep_scan") ? localStatus["last_deep_scan"].get<time_t>() : 0;

        if (time(0) - lastDeepScan > 5000) {
            // Retrieve all attributes of all messages, compare them with local. Populates
            // the entire folder quickly, but with no message bodies.
            syncFolderFullScan(*folder, remoteStatus);
        } else {
            if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore) ||
                session.storedCapabilities()->containsIndex(IMAPCapabilityXYMHighestModseq)) {
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



// Initial sync:
// - 1. Fetch all message headers in the folder in batches of 1000
//  + delete any that have been removed.
// - 2. Slowly iterate through and fetch bodies

// Speedy sync:
// - Expand fetchedmin/fetchedmax to new uidnext

// If we have condstore:
// - ask for all changed messages
// If no condstore:
// - compare top 1000 messages to our local version

/*
 Pull down all message attributes in the folder. For each range, compare against our local
 versions to determine 1) new, 2) changed, 3) deleted.
 */
void SyncWorker::syncFolderFullScan(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    int chunkSize = 10000;
    int uidMax = remoteStatus.uidNext();

    // The UID value space is sparse, meaning there can be huge gaps where there are no
    // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
    // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
    // ensuring we never bite off more than we can chew.
    if (remoteStatus.messageCount() < chunkSize) {
        syncFolderRange(folder, RangeMake(1, uidMax));
    } else {
        while (uidMax > 1) {
            int uidMin = max(1, uidMax - chunkSize);
            syncFolderRange(folder, RangeMake(uidMin, uidMax - uidMin));
            uidMax = uidMin;
        }
    }

    folder.localStatus()["last_deep_scan"] = time(0);
}

void SyncWorker::syncFolderShallow(Folder & folder, IMAPFolderStatus & remoteStatus)
{    
    int uidStart = store->fetchMessageUIDAtDepth(folder, 499);
    int uidNext = remoteStatus.uidNext();
    syncFolderRange(folder, RangeMake(uidStart, uidNext - uidStart));
    folder.localStatus()["fetchedmax"] = uidNext;
}

void SyncWorker::syncFolderRange(Folder & folder, Range range)
{
    logger->info("Syncing folder {} (UIDS {} - {})", folder.path(), range.location, range.location + range.length);
    
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IMAPProgressCallback * cb = new Progress();

    // Step 1: Fetch the remote UID range
    ErrorCode err = ErrorCode::ErrorNone;
    
    String path = AS_MCSTR(folder.path());
    Array * remote = session.fetchMessagesByUID(&path, kind, set, cb, &err);
    if (err) {
        logger->error("Could not fetch messages in folder.", err);
        return;
    }

    // Step 2: Fetch the local UID range
    map<uint32_t, MessageAttributes> local = store->fetchMessagesAttributesInRange(range, folder);
    
    for (int ii = 0; ii < remote->count(); ii++) {
        IMAPMessage * remoteMsg = (IMAPMessage *)remote->objectAtIndex(ii);
        if (local.count(remoteMsg->uid()) == 0) {
            // Found new message
            processor->insertMessage(remoteMsg, folder);
        } else {
            // Updating existing message attributes
            MessageAttributes localAttrs = local[remoteMsg->uid()];
            store->updateMessageAttributes(localAttrs, remoteMsg, folder);
        }
        local.erase(remoteMsg->uid());
    }
    
    // The messages left in localmap are the ones the server didn't give us.
    // They have been deleted. Unpersist them.
    vector<uint32_t> deletedUIDs {};
    for(auto const &ent : local) {
        deletedUIDs.push_back(ent.first);
    }
    Query query = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
    store->remove<Message>(query);
}


void SyncWorker::syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus)
{
    logger->info("Syncing folder {} changes via condstore...", folder.path());
    
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(1, UINT64_MAX));
    IMAPProgressCallback * cb = new Progress();
    ErrorCode err = ErrorCode::ErrorNone;

    uint64_t modseq = folder.localStatus()["highestmodseq"].get<uint64_t>();
    
    String path = AS_MCSTR(folder.path());
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
            Message * existing = local[uid].get();
            existing->updateAttributes(modifiedMsg);
            store->save(existing);
        }
    }
    
    // for deleted messages, collect UIDs and destroy
    if (result->vanishedMessages() != NULL) {
        vector<uint32_t> deletedUIDs = MailUtils::uidsOfIndexSet(result->vanishedMessages());
        Query query = Query().equal("folderId", folder.id()).equal("folderImapUID", deletedUIDs);
        store->remove<Message>(query);
    }

    folder.localStatus()["highestmodseq"] = remoteStatus.highestModSeqValue();
}


