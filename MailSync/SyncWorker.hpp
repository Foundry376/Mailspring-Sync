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
    
public:
    SyncWorker();
    
    void syncNow();

    std::vector<std::shared_ptr<Folder>> syncFolders();

    void syncFolderFullScan(Folder & folder, IMAPFolderStatus & remoteStatus);
        
    void syncFolderRange(Folder & folder, Range range);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncFolderChangesViaShallowScan(Folder & folder, IMAPFolderStatus & remoteStatus);
    
    void fetchRangeInFolder(String * folder, std::string folderId, Range range);

    void syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncMessageBody(Folder & folder, Message & message);
};


#endif /* SyncWorker_hpp */
