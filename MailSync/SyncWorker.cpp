
//  SyncWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//
#include <algorithm>

#include "SyncWorker.hpp"
#include "MailUtils.hpp"
#include "MailStoreTransaction.hpp"
#include "Folder.hpp"
#include "Label.hpp"
#include "File.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "Account.hpp"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"


#define CACHE_CLEANUP_INTERVAL      60 * 60
#define SHALLOW_SCAN_INTERVAL       60 * 2
#define DEEP_SCAN_INTERVAL          60 * 10

#define MAX_FULL_HEADERS_REQUEST_SIZE  1024
#define MODSEQ_TRUNCATION_THRESHOLD 4000
#define MODSEQ_TRUNCATION_UID_COUNT 12000

// These keys are saved to the folder object's "localState".
// Starred keys are used in the client to show sync progress.
#define LS_BUSY                     "busy"           // *
#define LS_UIDNEXT                  "uidnext"        // *
#define LS_SYNCED_MIN_UID           "syncedMinUID"   // *
#define LS_BODIES_PRESENT           "bodiesPresent"  // *
#define LS_BODIES_WANTED            "bodiesWanted"   // *
#define LS_LAST_CLEANUP             "lastCleanup"
/// IMPORTANT: deep/shallow are only used for some IMAP servers
#define LS_LAST_SHALLOW             "lastShallow"
#define LS_LAST_DEEP                "lastDeep"
#define LS_HIGHESTMODSEQ            "highestmodseq"
#define LS_UIDVALIDITY              "uidvalidity"
#define LS_UIDVALIDITY_RESET_COUNT  "uidvalidityResetCount"

using namespace mailcore;
using namespace std;


SyncWorker::SyncWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    unlinkPhase(1),
    logger(spdlog::get("logger")),
    processor(new MailProcessor(account, store)),
    session(IMAPSession())
{
    store->setStreamDelay(500);
}

void SyncWorker::configure()
{
    // For accounts connecting with XOAuth2, this function may
    // make HTTP requests so it's important this function is called
    // within the thread retry handlers.
    MailUtils::configureSessionForAccount(session, account);
}

void SyncWorker::idleInterrupt()
{
    // called on main / random threads to interrupt idle
    // do not call functions from here to avoid accidentally running on two threads!
    std::unique_lock<std::mutex> lck(idleMtx);
    idleShouldReloop = true;
    session.interruptIdle();
    idleCv.notify_one();
}

void SyncWorker::idleQueueBodiesToSync(vector<string> & ids) {
    // called on main thread
    std::unique_lock<std::mutex> lck(idleMtx);
    for (string & id : ids) {
        idleFetchBodyIDs.push_back(id);
    }
}

void SyncWorker::idleCycleIteration()
{
    // Run body requests from the client
    while (true) {
        string id;
        {
            std::unique_lock<std::mutex> lck(idleMtx);
            if (idleFetchBodyIDs.empty()) {
                break;
            }
            id = idleFetchBodyIDs.back();
            idleFetchBodyIDs.pop_back();
        }
        Query byId = Query().equal("id", id);
        auto msg = store->find<Message>(byId);
        if (msg.get() != nullptr) {
            logger->info("Fetching body for message ID {}", msg->id());

            // Check if session is connected before attempting fetch
            if (session.isDisconnected()) {
                logger->warn("IMAP session not connected, connecting before body fetch");
                ErrorCode connectErr = ErrorCode::ErrorNone;
                session.connectIfNeeded(&connectErr);
                if (connectErr != ErrorCode::ErrorNone) {
                    logger->error("Failed to connect for body fetch: {}", ErrorCodeToTypeMap[connectErr]);
                    continue;
                }
                session.loginIfNeeded(&connectErr);
                if (connectErr != ErrorCode::ErrorNone) {
                    logger->error("Failed to login for body fetch: {}", ErrorCodeToTypeMap[connectErr]);
                    continue;
                }
            }

            syncMessageBody(msg.get());
        }
    }

    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    
    // Connect and login to the IMAP server
    
    ErrorCode err = ErrorCode::ErrorNone;
    session.connectIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "connectIfNeeded");
    }
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    
    session.loginIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "loginIfNeeded");
    }
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }

    // Run tasks ready for performRemote. This is set up in an odd way
    // because we want tasks created when a task runs to also be run
    // immediately. (eg: A SendDraftTask queueing a SyncbackMetadataTask)
    //
    vector<shared_ptr<Task>> tasks;
    TaskProcessor processor { account, store, &session };

    // Ensure our pile of completed tasks doesn't grow unbounded
    processor.cleanupOldTasksAtRuntime();

    // Find tasks ready for "remote" that we haven't processed yet in this pass
    int rowid = -1;
    SQLite::Statement statement(store->db(), "SELECT rowid, data FROM Task WHERE accountId = ? AND status = \"remote\" AND rowid > ?");

    do {
        tasks = {};
        statement.bind(1, account->id());
        statement.bind(2, rowid);
        while (statement.executeStep()) {
            tasks.push_back(make_shared<Task>(statement));
            rowid = max(rowid, statement.getColumn("rowid").getInt());
        }
        statement.reset();
        for (auto & task : tasks) {
            processor.performRemote(task.get());
        }
    } while (tasks.size() > 0);

    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }

    // Identify the preferred idle folder (inbox / all)
    
    Query q = Query().equal("accountId", account->id()).equal("role", "inbox");
    auto inbox = store->find<Folder>(q);
    if (inbox.get() == nullptr) {
        Query q = Query().equal("accountId", account->id()).equal("role", "all");
        inbox = store->find<Folder>(q);
        if (inbox.get() == nullptr) {
            throw SyncException("no-inbox", "There is no inbox or all folder to IDLE on.", false);
        }
    }
    json inboxInitialStatus { inbox->localStatus() };
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    
    // Check for mail in the preferred idle folder (inbox / all)
    // Must verify both key presence AND that value is a number (not null) to prevent
    // JSON type_error exception when calling .get<uint32_t>() below
    bool hasStartedSyncingFolder = inbox->localStatus().count(LS_SYNCED_MIN_UID) > 0 &&
                                   inbox->localStatus()[LS_SYNCED_MIN_UID].is_number();

    if (hasStartedSyncingFolder) {
        String path = AS_MCSTR(inbox->path());
        IMAPFolderStatus remoteStatus = session.folderStatus(&path, &err);
        
        // Note: If we have CONDSTORE but don't have QRESYNC, this if/else may result
        // in us not seeing "vanished" messages until the next shallow sync iteration.
        // Right now I think that's fine.
        if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
            syncFolderChangesViaCondstore(*inbox, remoteStatus, false);
        } else {
            uint32_t uidnext = remoteStatus.uidNext();
            uint32_t syncedMinUID = inbox->localStatus()[LS_SYNCED_MIN_UID].get<uint32_t>();
            uint32_t bottomUID = store->fetchMessageUIDAtDepth(*inbox, 100, uidnext);
            if (bottomUID < syncedMinUID) { bottomUID = syncedMinUID; }
            // Guard against underflow if uidnext <= bottomUID (server inconsistency)
            if (uidnext > bottomUID) {
                syncFolderUIDRange(*inbox, RangeMake(bottomUID, uidnext - bottomUID), false);
            }
            inbox->localStatus()[LS_LAST_SHALLOW] = time(0);
            inbox->localStatus()[LS_UIDNEXT] = uidnext;
        }

        syncMessageBodies(*inbox, remoteStatus);
        
        store->saveFolderStatus(inbox.get(), inboxInitialStatus);
    }

    // Idle on the folder
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    if (session.setupIdle()) {
        logger->info("Idling on folder {}", inbox->path());
        String path = AS_MCSTR(inbox->path());
        session.idle(&path, 0, &err);
        session.unsetupIdle();
        logger->info("Idle exited with code {}", err);
        if (err != ErrorNone) {
            throw SyncException(err, "idle");
        }
    } else {
        logger->info("Connection does not support idling. Locking until more to do...");
        std::unique_lock<std::mutex> lck(idleMtx);
        idleCv.wait(lck);
    }
}

// Background Behaviors

void SyncWorker::markAllFoldersBusy() {
    logger->info("Marking all folders as `busy`");
    {
        MailStoreTransaction transaction(store, "markAllFoldersBusy");
        auto allLocalFolders = store->findAll<Folder>(Query().equal("accountId", account->id()));
        for (auto f : allLocalFolders) {
            f->localStatus()[LS_BUSY] = true;
            store->save(f.get());
        }
        transaction.commit();
    }
}


bool SyncWorker::syncNow()
{
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
        json initialLocalStatus = localStatus; // note: json not json&
        
        String path = AS_MCSTR(folder->path());
        ErrorCode err = ErrorCode::ErrorNone;
        IMAPFolderStatus remoteStatus = session.folderStatus(&path, &err);
        bool firstChunk = false;

        if (err != ErrorNone) {
            logger->warn("SyncNow: unable to get folder status for {} ({}), skipping...", folder->path(), ErrorCodeToTypeMap[err]);
            continue;
        }
        
        // Step 1: Check folder UIDValidity
        if (localStatus.empty() || localStatus[LS_UIDVALIDITY].is_null()) {
            // We're about to fetch the top N UIDs in the folder and start working backwards in time.
            // When we eventually finish and start using CONDSTORE, this will be the highestmodseq
            // from the /oldest/ synced block of UIDs, ensuring we see changes.
            localStatus[LS_HIGHESTMODSEQ] = remoteStatus.highestModSeqValue();
            localStatus[LS_UIDVALIDITY] = remoteStatus.uidValidity();
            localStatus[LS_UIDVALIDITY_RESET_COUNT] = 0;
            localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
            localStatus[LS_SYNCED_MIN_UID] = remoteStatus.uidNext();
            localStatus[LS_LAST_SHALLOW] = 0;
            localStatus[LS_LAST_DEEP] = 0;
            firstChunk = true;
        }
        
        // Step 1.5: Should we skip this folder?
        // On ProtonMail we use a container folder, all messages are duplicated into All Mail.
        // They need to be skipped and added to that folder on the client side if necessary.
        // Note: Check for containerFolder so Gmail "All Mail" is still synced.
        if (account->containerFolder() != "" && folder->path() == "All Mail") {
            logger->info("SyncNow: skipped ProtonMail global folder {}", folder->path());
            localStatus[LS_LAST_SHALLOW] = time(0); // pretend we synced now
            localStatus[LS_LAST_DEEP] = time(0);
            localStatus[LS_BODIES_WANTED] = 0; // pretend we want no message contents
            localStatus[LS_SYNCED_MIN_UID] = 1; // pretend we have scanned all the way to the oldest message
            localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
            store->saveFolderStatus(folder.get(), initialLocalStatus);
            continue;
        }

        if (localStatus[LS_UIDVALIDITY].get<uint32_t>() != remoteStatus.uidValidity()) {
            // UID Invalidity means that the UIDs the server previously reported for messages
            // in this folder can no longer be used. To recover from this, we need to:
            //
            // 1) Set remoteUID to the "UNLINKED" value for every message in the folder
            // 2) Run a 'deep' scan which will refetch the metadata for the messages,
            //    compute the Mailspring message IDs and re-map local models to remote UIDs.
            //
            // Notes:
            // - It's very important that this not generate deltas - because we're only changing
            //   the folderRemoteUID it should not broadcast this update to the Electron app.
            //
            // - UIDNext must be reset to the updated remote value
            //
            // - syncedMinUID must be reset to something and we set it to zero. If we haven't
            //   finished the initial scan of the folder yet, this could result in the creation
            //   of a huge number of Message models all at once and flood the app. Hopefully
            //   this scenario is rare.
            logger->warn("UIDInvalidity! Resetting remoteFolderUIDs, rebuilding index. This may take a moment...");
            processor->unlinkMessagesMatchingQuery(Query().equal("remoteFolderId", folder->id()), unlinkPhase);
            syncFolderUIDRange(*folder, RangeMake(1, UINT64_MAX), false);

            if (localStatus.count(LS_UIDVALIDITY_RESET_COUNT) == 0) {
                localStatus[LS_UIDVALIDITY_RESET_COUNT] = 1;
            }
            localStatus[LS_UIDVALIDITY_RESET_COUNT] = localStatus[LS_UIDVALIDITY_RESET_COUNT].get<uint32_t>() + 1;
            localStatus[LS_HIGHESTMODSEQ] = remoteStatus.highestModSeqValue();
            localStatus[LS_UIDVALIDITY] = remoteStatus.uidValidity();
            localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
            localStatus[LS_SYNCED_MIN_UID] = 1;
            localStatus[LS_LAST_SHALLOW] = time(0);
            localStatus[LS_LAST_DEEP] = time(0);
            
            store->saveFolderStatus(folder.get(), initialLocalStatus);
            continue;
        }
        
        // Step 2: Initial sync. Until we reach UID 1, we grab chunks of messages
        uint32_t syncedMinUID = localStatus[LS_SYNCED_MIN_UID].get<uint32_t>();
        uint32_t chunkSize = firstChunk ? 750 : 5000;

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
            localStatus[LS_SYNCED_MIN_UID] = chunkMinUID;
            syncedMinUID = chunkMinUID;
        }
        
        // Step 3: A) Retrieve new messages  B) update existing messages  C) delete missing messages
        // CONDSTORE, when available, does A + B.
        // XYZRESYNC, when available, does C
        if (hasCondstore && hasQResync) {
            // Hooray! We never need to fetch the entire range to sync. Just look at
            // highestmodseq / uidnext and sync if we need to.
            syncFolderChangesViaCondstore(*folder, remoteStatus, true);
        } else {
            uint32_t remoteUidnext = remoteStatus.uidNext();
            uint32_t localUidnext = localStatus[LS_UIDNEXT].get<uint32_t>();
            bool newMessages = remoteUidnext > localUidnext;
            bool timeForDeepScan = (iterationsSinceLaunch > 0) && (time(0) - localStatus[LS_LAST_DEEP].get<time_t>() > DEEP_SCAN_INTERVAL);
            bool timeForShallowScan = !timeForDeepScan && (time(0) - localStatus[LS_LAST_SHALLOW].get<time_t>() > SHALLOW_SCAN_INTERVAL);

            // Okay. If there are new messages in the folder (UIDnext has increased), do a heavy fetch of
            // those /AND/ get the bodies. This ensures people see both very quickly, which is important.
            //
            // This could potentially grab zillions of messages, in which case syncFolderUIDRange will
            // bail out and the next "deep" scan will pick up the ones we skipped.
            //
            if (newMessages) {
                vector<shared_ptr<Message>> synced{};
                syncFolderUIDRange(*folder, RangeMake(localUidnext, remoteUidnext - localUidnext), true, &synced);
                
                if ((folder->role() == "inbox") || (folder->role() == "all")) {
                    // if UIDs are ascending, flip them so we download the newest (highest) UID bodies first
                    if (synced.size() > 1 && synced[0]->remoteUID() < synced[1]->remoteUID()) {
                        std::reverse(synced.begin(), synced.end());
                    }
                    int count = 0;
                    for (auto msg : synced) {
                        if (!msg->isInInbox()) {
                            continue; // skip "all mail" that is not in inbox
                        }
                        syncMessageBody(msg.get());
                        if (count++ > 30) { break; }
                    }
                }
            }
            
            if (timeForShallowScan) {
                // note: we use local uidnext here, because we just fetched everything between
                // localUIDNext and remoteUIDNext so fetching that section again would just slow us down.
                uint32_t bottomUID = store->fetchMessageUIDAtDepth(*folder, 399, localUidnext);
                if (bottomUID < syncedMinUID) {
                    bottomUID = syncedMinUID;
                }
                // Guard against underflow if remoteUidnext <= bottomUID (server inconsistency)
                if (remoteUidnext > bottomUID) {
                    syncFolderUIDRange(*folder, RangeMake(bottomUID, remoteUidnext - bottomUID), false);
                }
                localStatus[LS_LAST_SHALLOW] = time(0);
                localStatus[LS_UIDNEXT] = remoteUidnext;
            }
            
            if (timeForDeepScan) {
                syncFolderUIDRange(*folder, RangeMake(syncedMinUID, UINT64_MAX), false);
                localStatus[LS_LAST_SHALLOW] = time(0);
                localStatus[LS_LAST_DEEP] = time(0);
                localStatus[LS_UIDNEXT] = remoteUidnext;
            }
        }
        
        bool moreToDo = false;

        // Retrieve some message bodies. We do this concurrently with the full header
        // scan so the user sees snippets on some messages quickly.
        if (syncMessageBodies(*folder, remoteStatus)) {
            moreToDo = true;
        }
        if (syncedMinUID > 1) {
            moreToDo = true;
        }
        
        // Update cache metrics and cleanup bodies we don't want anymore.
        // these queries are expensive so we do this infrequently and increment
        // blindly as we download bodies.
        time_t lastCleanup = localStatus.count(LS_LAST_CLEANUP) ? localStatus[LS_LAST_CLEANUP].get<time_t>() : 0;
        if (syncedMinUID == 1 && (time(0) - lastCleanup > CACHE_CLEANUP_INTERVAL)) {
            cleanMessageCache(*folder);
            localStatus[LS_LAST_CLEANUP] = time(0);
        }

        // Save a general flag that indicates whether we're still doing stuff
        // like syncing message bodies. Set to true below.
        localStatus[LS_BUSY] = moreToDo;
        syncAgainImmediately = syncAgainImmediately || moreToDo;

        // Save the folder - note that helper methods above mutated localStatus.
        // Avoid the save if we can, because this creates a lot of noise in the client.
        store->saveFolderStatus(folder.get(), initialLocalStatus);
    }
    
    // We've just unlinked a bunch of messages with PHASE A, now we'll delete the ones
    // with PHASE B. This ensures anything we /just/ discovered was missing gets one
    // cycle to appear in another folder before we decide it's really, really gone.
    unlinkPhase = unlinkPhase == 1 ? 2 : 1;
    logger->info("Sync loop deleting unlinked messages with phase {}.", unlinkPhase);
    processor->deleteMessagesStillUnlinkedFromPhase(unlinkPhase);
    
    logger->info("Sync loop complete.");
    iterationsSinceLaunch += 1;

    return syncAgainImmediately;
}

void SyncWorker::ensureRootMailspringFolder(vector<string> containerFolderComponents, Array * remoteFolders)
{
    auto components = Array::array();
    for (string containerFolderComponent : containerFolderComponents) {
      components->addObject(AS_MCSTR(containerFolderComponent));
    }
    
    String * desiredPath = session.defaultNamespace()->pathForComponents(components);
    
    bool exists = false;
    for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
        IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
        if (remote->path()->isEqual(desiredPath)) {
            exists = true;
        }
    }
    
    if (!exists) {
        ErrorCode err = ErrorCode::ErrorNone;
        session.createFolder(desiredPath, &err);
        if (err) {
            logger->error("Could not create Mailspring container folder: {}. {}", desiredPath->UTF8Characters(), ErrorCodeToTypeMap[err]);
        } else {
            logger->error("Created Mailspring container folder: {}.", desiredPath->UTF8Characters());
        }
    }
}

vector<shared_ptr<Folder>> SyncWorker::syncFoldersAndLabels()
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    string containerFolderPath = account->containerFolder();
    vector<string> containerFolderComponents;
    
    if (containerFolderPath == "" || containerFolderPath == MAILSPRING_FOLDER_PREFIX_V2) {
      logger->info("Syncing folder list...");
      containerFolderComponents.push_back(MAILSPRING_FOLDER_PREFIX_V2);
    } else {
      logger->info("Syncing folder list on custom container folder {} ...", containerFolderPath);
      
      std::stringstream data(containerFolderPath);
      std::string folder;
      while(std::getline(data, folder, '/'))
      {
        containerFolderComponents.push_back(folder);
      }
    }
    
    ErrorCode err = ErrorCode::ErrorNone;
    Array * remoteFolders = session.fetchAllFolders(&err);
    if (err) {
        throw SyncException(err, "syncFoldersAndLabels - fetchAllFolders");
    }

    string mainPrefix = MailUtils::namespacePrefixOrBlank(&session);
    bool ensuredRoot = false;
    
    // create required Mailspring folders if they don't exist
    // TODO: Consolidate this into role association code below, and make it
    // use the same business logic as creating / updating folders from tasks.
    vector<string> mailspringFolders{"Snoozed"};

    for (string mailspringFolder : mailspringFolders) {
        string mailspringRole = mailspringFolder;
        transform(mailspringRole.begin(), mailspringRole.end(), mailspringRole.begin(), ::tolower);

        bool exists = false;
        for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            string remoteRole = MailUtils::roleForFolder(containerFolderPath, mainPrefix, remote);
            if (remoteRole == mailspringRole) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            if (!ensuredRoot) {
                ensureRootMailspringFolder(containerFolderComponents, remoteFolders);
                ensuredRoot = true;
            }
            
            auto components = Array::array();
            for (string containerFolderComponent : containerFolderComponents) {
              components->addObject(AS_MCSTR(containerFolderComponent));
            }
            components->addObject(AS_MCSTR(mailspringFolder));
            String * desiredPath = session.defaultNamespace()->pathForComponents(components);
            session.createFolder(desiredPath, &err);
            if (err) {
                logger->error("Could not create required Mailspring folder: {}. {}", desiredPath->UTF8Characters(), ErrorCodeToTypeMap[err]);
                continue;
            }
            logger->error("Created required Mailspring folder: {}.", desiredPath->UTF8Characters());
            IMAPFolder * fake = new IMAPFolder();
            fake->autorelease();
            fake->setPath(desiredPath);
            fake->setDelimiter(session.defaultNamespace()->mainDelimiter());
            remoteFolders->addObject(fake);
        }
    }
    
    // sync with the local store
    vector<shared_ptr<Folder>> foldersToSync{};

    {
        MailStoreTransaction transaction{store, "syncFoldersAndLabels"};
        
        Query q = Query().equal("accountId", account->id());
        bool isGmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
        auto unusedLocalFolders = store->findAllMap<Folder>(q, "id");
        auto unusedLocalLabels = store->findAllMap<Label>(q, "id");
        map<string, shared_ptr<Folder>> allFoundCategories {};
        
        // Eliminate unselectable folders
        for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            if (remote->flags() & IMAPFolderFlagNoSelect) {
                remoteFolders->removeObjectAtIndex(ii);
                continue;
            }
        }

        // Find / create local folders and labels to match the remote ones
        // Note: We don't assign roles, just create the objects here.
        for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
            string remotePath = remote->path()->UTF8Characters();

            bool isLabel = false;
            if (isGmail) {
                IMAPFolderFlag remoteFlags = remote->flags();
                isLabel = !(remoteFlags & IMAPFolderFlagAll) && !(remoteFlags & IMAPFolderFlagSpam) && !(remoteFlags & IMAPFolderFlagTrash);
            }

            shared_ptr<Folder> local;

            if (isLabel) {
                // Treat as a label
                if (unusedLocalLabels.count(remoteId) > 0) {
                    local = unusedLocalLabels[remoteId];
                    unusedLocalLabels.erase(remoteId);
                } else {
                    local = make_shared<Label>(remoteId, account->id(), 0);
                    local->setPath(remotePath);
                    store->save(local.get());
                }

            } else {
                // Treat as a folder
                if (unusedLocalFolders.count(remoteId) > 0) {
                    local = unusedLocalFolders[remoteId];
                    unusedLocalFolders.erase(remoteId);
                } else {
                    local = make_shared<Folder>(remoteId, account->id(), 0);
                    local->setPath(remotePath);
                    store->save(local.get());
                }
                foldersToSync.push_back(local);
            }
            
            allFoundCategories[remoteId] = local;
        }

        for (auto role : MailUtils::roles()) {
            bool found = false;

            // If the role is already assigned, skip
            for (auto it : allFoundCategories) {
                if (it.second->role() == role) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
            
            // find a folder that matches the flags
            for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
                IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
                string cr = MailUtils::roleForFolderViaFlags(mainPrefix, remote);
                if (cr != role) {
                    continue;
                }
                string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
                if (!allFoundCategories.count(remoteId)) {
                    logger->warn("-X found folder for role, couldn't find local object for {}", role);
                    continue;
                }
                allFoundCategories[remoteId]->setRole(role);
                store->save(allFoundCategories[remoteId].get());
                found = true;
                break;
            }
                    
            if (found) {
                continue;
            }
            
            // find a folder that matches the name
            for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
                IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
                string cr = MailUtils::roleForFolderViaPath(containerFolderPath, mainPrefix, remote);
                if (cr != role) {
                    continue;
                }
                string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
                if (!allFoundCategories.count(remoteId)) {
                    logger->warn("-X found folder for role, couldn't find local object for {}", role);
                    continue;
                }
                allFoundCategories[remoteId]->setRole(role);
                store->save(allFoundCategories[remoteId].get());
                found = true;
                break;
            }
        }
        
        // delete any folders / labels no longer present on the remote
        for (auto const item : unusedLocalFolders) {
            store->remove(item.second.get());
        }
        for (auto const item : unusedLocalLabels) {
            store->remove(item.second.get());
        }
        
        transaction.commit();
    }

    return foldersToSync;
}

void SyncWorker::syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest, vector<shared_ptr<Message>> * syncedMessages)
{
    std::string remotePath = folder.path();
    
    // Safety check: "0" is not a valid start and causes the server to return only the last item
    if (range.location == 0) {
        range.location = 1;
    }
    // Safety check: force an attributes-only sync of the range if the requested UID range is so
    // large the query might never complete if we ask for it all. We might still need to fetch all the
    // bodies, but we'll cap the number we fetch.
    if (range.length > MAX_FULL_HEADERS_REQUEST_SIZE) {
        heavyInitialRequest = false;
    }

    logger->info("syncFolderUIDRange for {}, UIDs: {} - {}, Heavy: {}", remotePath, range.location, range.location + range.length, heavyInitialRequest);

    AutoreleasePool pool;
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IndexSet * heavyNeeded = new IndexSet();
    IMAPProgress cb;
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(remotePath));
    int heavyNeededIdeal = 0;
    
    // Step 1: Fetch the local attributes (unread, starred, etc.)
    // Note: we do this first because the remote fetch may take a long time, and if the data that
    // comes back is already stale, we want to calculate changes (deletes, especially) based on
    // old <> old, not new <> old, since new, freshly downloaded messages will always be missing
    // in the stale server set and will be marked for deletion. Re-downloading is better.
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(range, folder));

    // Step 2: Fetch the remote attributes (unread, starred, etc.) for the same UID range
    time_t syncDataTimestamp = time(0);
    auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), heavyInitialRequest);
    Array * remote = session.fetchMessagesByUID(&path, kind, set, &cb, &err);
    if (err) {
        throw SyncException(err, "syncFolderUIDRange - fetchMessagesByUID");
    }

    clock_t lastSleepClock = clock();

    logger->info("- {}: remote={}, local={}, remoteUID={}", remotePath, remote->count(), local.size(), folder.id());

    for (int ii = ((int)remote->count()) - 1; ii >= 0; ii--) {
        // Never sit in a hard loop inserting things into the database for more than 250ms.
        // This ensures we don't starve another thread waiting for a database connection
        if (((clock() - lastSleepClock) * 4) / CLOCKS_PER_SEC > 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lastSleepClock = clock();
        }
        
        IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
        uint32_t remoteUID = remoteMsg->uid();

        // Step 3: Collect messages that are different or not in our local UID set.
        bool inFolder = (local.count(remoteUID) > 0);
        bool same = inFolder && MessageAttributesMatch(local[remoteUID], MessageAttributesForMessage(remoteMsg));

        if (!inFolder || !same) {
            // Step 4: Attempt to insert the new message. If we get unique exceptions,
            // look for the existing message and do an update instead. This happens whenever
            // a message has moved between folders or it's attributes have changed.
            
            // Note: We could prefetch all changedOrMissingIDs and then decide to update/insert,
            // but we can only query for 500 at a time, it /feels/ nasty, and we /could/ always
            // hit the exception anyway since another thread could be IDLEing and retrieving
            // the messages alongside us.
            if (heavyInitialRequest) {
                auto local = processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
                if (syncedMessages != nullptr) {
                    syncedMessages->push_back(local);
                }
            } else {
                if (heavyNeededIdeal < MAX_FULL_HEADERS_REQUEST_SIZE) {
                    heavyNeeded->addIndex(remoteUID);
                }
                heavyNeededIdeal += 1;
            }
        }
        
        local.erase(remoteUID);
    }
    
    if (!heavyInitialRequest && heavyNeeded->count() > 0) {
        logger->info("- Fetching full headers for {} (of {} needed)", heavyNeeded->count(), heavyNeededIdeal);

        // Note: heavyNeeded could be enormous if the user added a zillion items to a folder, if it's been
        // years since the app was launched, or if a sync bug caused us to delete messages we shouldn't have.
        // (eg the issue with uidnext becoming zero suddenly)
        //
        // We don't re-fetch them all in one request because it could be an impossibly large amount of data.
        // Instead we sync MAX_FULL_HEADERS_REQUEST_SIZE and on the next "deep scan" in 10 minutes, we'll
        // sync X more.
        //
        syncDataTimestamp = time(0);
        auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), true);
        remote = session.fetchMessagesByUID(&path, kind, heavyNeeded, &cb, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "syncFolderUIDRange - fetchMessagesByUID (heavy)");
        }
        for (int ii = ((int)remote->count()) - 1; ii >= 0; ii--) {
            IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
            auto local = processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
            if (syncedMessages != nullptr) {
                syncedMessages->push_back(local);
            }
            remote->removeLastObject();
        }
    }

    // Step 5: Unlink. The messages left in local map are the ones we had in the range,
    // which the server reported were no longer there. Remove their remoteUID.
    // We'll delete them later if they don't appear in another folder during sync.
    if (local.size() > 0) {
        vector<uint32_t> deletedUIDs {};
        for (auto const &ent : local) {
            deletedUIDs.push_back(ent.first);
        }
        for (vector<uint32_t> chunk : MailUtils::chunksOfVector(deletedUIDs, 200)) {
            auto query = Query().equal("remoteFolderId", folder.id()).equal("remoteUID", chunk);
            processor->unlinkMessagesMatchingQuery(query, unlinkPhase);
        }
    }
}

void SyncWorker::syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus, bool mustSyncAll)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    uint32_t uidnext = folder.localStatus()[LS_UIDNEXT].get<uint32_t>();
    uint64_t modseq = folder.localStatus()[LS_HIGHESTMODSEQ].get<uint64_t>();
    uint64_t remoteModseq = remoteStatus.highestModSeqValue();
    uint32_t remoteUIDNext = remoteStatus.uidNext();
    time_t syncDataTimestamp = time(0);
    
    logger->info("syncFolderChangesViaCondstore - {}: modseq {} to {}, uidnext {} to {}",
                 folder.path(), modseq, remoteModseq, uidnext, remoteUIDNext);

    if (modseq == remoteModseq && uidnext == remoteUIDNext) {
        return;
    }

    // if the difference between our stored modseq and highestModseq is very large,
    // we can create a request that takes forever to complete and /blocks/ the foreground
    // worker from performing mailbox actions, which is really bad. To bound the request,
    // we ask for changes within the last 25,000 UIDs only. Our intermittent "deep" scan
    // will recover the rest of the changes so it's safe not to ingest them here.
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(1, UINT64_MAX));
    if (!mustSyncAll && remoteModseq - modseq > MODSEQ_TRUNCATION_THRESHOLD) {
        uint32_t bottomUID = remoteUIDNext > MODSEQ_TRUNCATION_UID_COUNT ? remoteUIDNext - MODSEQ_TRUNCATION_UID_COUNT : 1;
        uids = IndexSet::indexSetWithRange(RangeMake(bottomUID, UINT64_MAX));
        logger->warn("syncFolderChangesViaCondstore - request limited to {}-*, remaining changes will be detected via deep scan", bottomUID);
    }

    IMAPProgress cb;
    ErrorCode err = ErrorCode::ErrorNone;
    String path(AS_MCSTR(folder.path()));
    
    auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), true);
    IMAPSyncResult * result = session.syncMessagesByUID(&path, kind, uids, modseq, &cb, &err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "syncFolderChangesViaCondstore - syncMessagesByUID");
    }

    // for modified messages, fetch local copy and apply changes
    Array * modifiedOrAdded = result->modifiedOrAddedMessages();
    IndexSet * vanished = result->vanishedMessages();
    
    logger->info("syncFolderChangesViaCondstore - Changes since HMODSEQ {}: {} changed, {} vanished",
                 modseq, modifiedOrAdded->count(), (vanished != nullptr) ? vanished->count() : 0);

    for (unsigned int ii = 0; ii < modifiedOrAdded->count(); ii ++) {
        IMAPMessage * msg = (IMAPMessage *)modifiedOrAdded->objectAtIndex(ii);
        string id = MailUtils::idForMessage(folder.accountId(), folder.path(), msg);

        Query query = Query().equal("id", id);
        auto local = store->find<Message>(query);
        
        if (local == nullptr) {
            // Found message with an ID we've never seen in any folder. Add it!
            processor->insertFallbackToUpdateMessage(msg, folder, syncDataTimestamp);
        } else {
            // Found message with an existing ID. Update it's attributes & folderId.
            // Note: Could potentially have moved from another folder!
            processor->updateMessage(local.get(), msg, folder, syncDataTimestamp);
        }
    }
    
    // for deleted messages, collect UIDs and destroy. Note: vanishedMessages is only
    // populated when QRESYNC is available. IMPORTANT: vanished may include an infinite
    // range, like 12:* so we can't convert it to a fixed array.
    if (vanished != NULL) {
        vector<Query> queries = MailUtils::queriesForUIDRangesInIndexSet(folder.id(), vanished);
        for (Query & query : queries) {
            processor->unlinkMessagesMatchingQuery(query, unlinkPhase);
        }
    }

    folder.localStatus()[LS_UIDNEXT] = remoteUIDNext;
    folder.localStatus()[LS_HIGHESTMODSEQ] = remoteModseq;
}

void SyncWorker::cleanMessageCache(Folder & folder) {
    logger->info("Cleaning local cache and updating stats");
    
    // delete bodies we no longer want. Note: you can't do INNER JOINs within a DELETE
    // note: we only delete messages fetchedd more than 14 days ago to avoid deleting
    // old messages you're actively viewing / could still want
    SQLite::Statement purge(store->db(), "DELETE FROM MessageBody WHERE MessageBody.fetchedAt < datetime('now', '-14 days') AND MessageBody.id IN (SELECT Message.id FROM Message WHERE Message.remoteFolderId = ? AND Message.draft = 0 AND Message.date < ?)");
    purge.bind(1, folder.id());
    purge.bind(2, (double)(time(0) - maxAgeForBodySync(folder)));
    int purged = purge.exec();
    logger->info("-- {} message bodies deleted from local cache.", purged);
    // TODO BG: Remove them from the search index and remove attachments

    // update messages body stats
    folder.localStatus()[LS_BODIES_PRESENT] = countBodiesDownloaded(folder);
    folder.localStatus()[LS_BODIES_WANTED] = countBodiesNeeded(folder);
}

// Message Body Sync

time_t SyncWorker::maxAgeForBodySync(Folder & folder) {
    return 24 * 60 * 60 * 30 * 3; // three months TODO pref!
}

bool SyncWorker::shouldCacheBodiesInFolder(Folder & folder) {
    // who needs this stuff? probably nobody.
    if ((folder.role() == "spam") || (folder.role() == "trash")) {
        return false;
    }
    return true;
}

long long SyncWorker::countBodiesDownloaded(Folder & folder) {
    SQLite::Statement count(store->db(), "SELECT COUNT(Message.id) FROM Message INNER JOIN MessageBody ON MessageBody.id = Message.id WHERE MessageBody.value IS NOT NULL AND Message.remoteFolderId = ?");
    count.bind(1, folder.id());
    count.executeStep();
    return count.getColumn(0).getInt64();
}

long long SyncWorker::countBodiesNeeded(Folder & folder) {
    if (!shouldCacheBodiesInFolder(folder)) {
        return 0;
    }
    SQLite::Statement count(store->db(), "SELECT COUNT(Message.id) FROM Message WHERE Message.remoteFolderId = ? AND (Message.date > ? OR Message.draft = 1) AND Message.remoteUID > 0");
    count.bind(1, folder.id());
    count.bind(2, (double)(time(0) - maxAgeForBodySync(folder)));
    count.executeStep();
    return count.getColumn(0).getInt64();
}

/*
 Syncs the top N missing message bodies. Returns true if it did work, false if it did nothing.
 */
bool SyncWorker::syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus) {
    if (!shouldCacheBodiesInFolder(folder)) {
        return false;
    }

    vector<string> ids{};
    vector<shared_ptr<Message>> results{};

    // very slow query = 400ms+
    SQLite::Statement missing(store->db(), "SELECT Message.id, Message.remoteUID FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.accountId = ? AND Message.remoteFolderId = ? AND (Message.date > ? OR Message.draft = 1) AND Message.remoteUID > 0 AND MessageBody.id IS NULL ORDER BY Message.date DESC LIMIT 30");
    missing.bind(1, folder.accountId());
    missing.bind(2, folder.id());
    missing.bind(3, (double)(time(0) - maxAgeForBodySync(folder))); // three months TODO pref!
    while (missing.executeStep()) {
        if (missing.getColumn(1).getUInt() >= UINT32_MAX - 2) {
            continue; // message is scheduled for cleanup
        }
        ids.push_back(missing.getColumn(0).getString());
    }
    
    SQLite::Statement stillMissing(store->db(), "SELECT Message.* FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.id IN (" + MailUtils::qmarks(ids.size()) + ") AND MessageBody.id IS NULL");
    SQLite::Statement insertPlaceholder(store->db(), "INSERT OR IGNORE INTO MessageBody (id, value) VALUES (?, ?)");

    {
        MailStoreTransaction transaction { store, "syncMessageBodies" };

        // very fast query for the messages found during very slow query that still have no message body.
        // Inserting empty message body reserves them for processing here. We do this within a transaction
        // to ensure we don't process the same message twice.
        int ii = 1;
        for (auto id : ids) {
            stillMissing.bind(ii++, id);
        }
        while (stillMissing.executeStep()) {
            results.push_back(make_shared<Message>(stillMissing));
        }
        if (results.size() < ids.size()) {
            logger->info("Body for {} messages already being fetched.", ids.size() - results.size());
        }

        for (auto result : results) {
            // write a blank entry into the MessageBody table so we'll only try to fetch each
            // message once. Otherwise a persistent ErrorFetch or crash for a single message
            // can cause the account to stay "syncing" forever.
            insertPlaceholder.bind(1, result->id());
            insertPlaceholder.bind(2);
            insertPlaceholder.exec();
            insertPlaceholder.reset();
        }

        transaction.commit();
    }

    json & ls = folder.localStatus();
    if (!ls.count(LS_BODIES_PRESENT) || !ls[LS_BODIES_PRESENT].is_number()) {
        ls[LS_BODIES_PRESENT] = 0;
    }
    
    for (auto result : results) {
        // increment local sync state - it's fine if this sometimes fails to save,
        // we recompute the value via COUNT(*) during cleanup
        ls[LS_BODIES_PRESENT] = ls[LS_BODIES_PRESENT].get<long long>() + 1;

        // attempt to fetch the message boy
        syncMessageBody(result.get());
    }
    
    return results.size() > 0;
}

void SyncWorker::syncMessageBody(Message * message) {
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;
    
    IMAPProgress cb;
    ErrorCode err = ErrorCode::ErrorNone;
    string folderPath = message->remoteFolder()["path"].get<string>();
    String path(AS_MCSTR(folderPath));
    
    Data * data = session.fetchMessageByUID(&path, message->remoteUID(), &cb, &err);
    if (err != ErrorNone) {
        logger->error("Unable to fetch body for message \"{}\" ({} UID {}). Error {}",
                      message->subject(), folderPath, message->remoteUID(), ErrorCodeToTypeMap[err]);

        if (err == ErrorFetch) {
            // Syncing message bodies can fail often, because we query our local store
            // and the sync worker may not have updated it yet. Messages, esp. drafts,
            // can just disappear.

            // oh well.
            return;
        }

        throw SyncException(err, "syncMessageBody - fetchMessageByUID");
    }
    if (data == nullptr) {
        logger->error("fetchMessageByUID returned null data for message \"{}\" ({} UID {})",
                      message->subject(), folderPath, message->remoteUID());
        return;
    }
    MessageParser * messageParser = MessageParser::messageParserWithData(data);
    if (messageParser == nullptr) {
        logger->error("MessageParser::messageParserWithData returned null for message \"{}\" ({} UID {})",
                      message->subject(), folderPath, message->remoteUID());
        return;
    }
    processor->retrievedMessageBody(message, messageParser);
}
