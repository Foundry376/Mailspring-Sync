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

#include "MailStore.hpp"
#include "MailProcessor.hpp"
#include "CommStream.hpp"
#include "Folder.hpp"

using namespace mailcore;

class SyncWorker {
    IMAPSession session;
    MailStore * store;
    CommStream * stream;
    MailProcessor * processor;
    std::shared_ptr<spdlog::logger> logger;
    
    int unlinkPhase;
    bool idleShouldReloop;
    vector<string> idleFetchBodyIDs;

public:
    SyncWorker(string name, CommStream * stream);
    
    bool syncNow();

    void idleInterrupt();
    void idleQueueBodiesToSync(vector<string> & ids);
    void idleCycle();
    
    std::vector<std::shared_ptr<Folder>> syncFoldersAndLabels();

    bool syncFolderFullScanIncremental(Folder & folder, IMAPFolderStatus & remoteStatus);
        
    void syncFolderUIDRange(Folder & folder, Range range);

    void syncFolderRangeResults(Folder & folder, Array * remote);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncFolderChangesViaShallowScan(Folder & folder, IMAPFolderStatus & remoteStatus);
    
    void fetchRangeInFolder(String * folder, std::string folderId, Range range);

    bool syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncMessageBody(string folderPath, Message * message);
};


#endif /* SyncWorker_hpp */
