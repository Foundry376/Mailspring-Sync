//
//  SyncWorker.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef SyncWorker_hpp
#define SyncWorker_hpp

#include <stdio.h>

#include <iostream>
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
    shared_ptr<Account> account;

    int unlinkPhase;
    bool idleShouldReloop;
    int iterationsSinceLaunch;
    vector<string> idleFetchBodyIDs;
    std::mutex idleMtx;
    std::condition_variable idleCv;

public:
    SyncWorker(string name, shared_ptr<Account> account);
    
#pragma mark Foreground Worker
    void idleInterrupt();
    void idleQueueBodiesToSync(vector<string> & ids);
    void idleCycle();
    void idleCycleIteration();
    
#pragma mark Background Worker
    bool syncNow();

    std::vector<std::shared_ptr<Folder>> syncFoldersAndLabels();

    bool initialSyncFolderIncremental(Folder & folder, IMAPFolderStatus & remoteStatus);
        
    void syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus);

    IMAPMessagesRequestKind fetchRequestKind(bool heavy);
    
    void fetchRangeInFolder(String * folder, std::string folderId, Range range);

    bool syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncMessageBody(Message * message);
};


#endif /* SyncWorker_hpp */
