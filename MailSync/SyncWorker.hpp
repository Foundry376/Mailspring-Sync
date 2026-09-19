//
//  SyncWorker.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef SyncWorker_hpp
#define SyncWorker_hpp

#include <stdio.h>

#include <atomic>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <MailCore/MailCore.h>

#include "Account.hpp"
#include "MailStore.hpp"
#include "MailProcessor.hpp"
#include "DeltaStream.hpp"
#include "Folder.hpp"

using namespace mailcore;

class SyncWorker {
    IMAPSession session;
    
    MailStore * store;
    MailProcessor * processor;
    shared_ptr<spdlog::logger> logger;

    int unlinkPhase;
    std::atomic<bool> idleShouldReloop{false};
    int iterationsSinceLaunch = 0;

    // Per-folder count of messages the last truncated full-folder scan still needed, used to tell
    // a draining backlog (count falls each pass) from one that can never drain (count stays put).
    // Not persisted: it only has to survive between iterations of the same process.
    std::map<std::string, size_t> lastTruncatedScanNeeded {};
    vector<string> idleFetchBodyIDs;
    std::mutex idleMtx;
    std::condition_variable idleCv;

public:
    
    shared_ptr<Account> account;

    SyncWorker(shared_ptr<Account> account);
    void configure();

#pragma mark Foreground Worker

public:
    
    void idleInterrupt();
    void idleQueueBodiesToSync(vector<string> & ids);
    void idleCycleIteration();

    
#pragma mark Background Worker

public:
    
    bool syncNow();

    void markAllFoldersBusy();

    std::vector<std::shared_ptr<Folder>> syncFoldersAndLabels();

private:
    
    void ensureRootMailspringFolder(vector<string> containerFolderComponents, Array * remoteFolders);
    void removeDuplicateFolders(Array * remoteFolders);

    bool initialSyncFolderIncremental(Folder & folder, IMAPFolderStatus & remoteStatus);
        
    // Result of syncing a UID range. `truncated` is set when the range contained more
    // messages needing full headers than we were willing to request at once; in that case
    // `syncedMinUID` is the lowest UID we actually ingested and everything below it in the
    // requested range still needs to be fetched. Callers must not record the range as
    // synced past `syncedMinUID`, or those messages are lost until UIDVALIDITY changes.
    struct UIDRangeSyncResult {
        bool truncated = false;
        uint32_t syncedMinUID = 1;
        // Total messages in the range that needed full headers, before any truncation.
        size_t needed = 0;
    };

    // True if a full-folder scan left work behind AND the backlog is still shrinking, meaning the
    // caller should come straight back for the next batch. Also clears the folder's bookkeeping
    // once a scan completes cleanly, so it must be called for every full-folder scan result.
    bool shouldRetryTruncatedScan(Folder & folder, UIDRangeSyncResult const & scan);

    UIDRangeSyncResult syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest, vector<shared_ptr<Message>> * syncedMessages = nullptr);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus, bool mustSyncAll);

    void fetchRangeInFolder(String * folder, std::string folderId, Range range);

    void cleanMessageCache(Folder & folder);

    void unlinkVanishedUIDs(Folder & folder, IndexSet * vanished, const char * source);
    
    long long countBodiesDownloaded(Folder & folder);
    long long countBodiesNeeded(Folder & folder);
    time_t maxAgeForBodySync(Folder & folder);
    bool shouldCacheBodiesInFolder(Folder & folder);
    bool syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus);
    void syncMessageBody(Message * message);
};


#endif /* SyncWorker_hpp */
