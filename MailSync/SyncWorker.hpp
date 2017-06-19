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
#include "CommStream.hpp"
#include "Folder.hpp"

using namespace mailcore;

class SyncWorker {
    IMAPSession session;
    MailStore * store;
    CommStream * stream;
    std::shared_ptr<spdlog::logger> logger;
    
public:
    SyncWorker();
    
    void syncNow();

    std::vector<Folder *> syncFolders();

    void syncFolderFullScan(Folder & folder, IMAPFolderStatus & remoteStatus);
        
    void syncFolderRange(Folder & folder, Range range);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncFolderShallow(Folder & folder, IMAPFolderStatus & remoteStatus);
    
    void fetchMessagesInFolder(String * folder, std::string folderId, Range range);
};


#endif /* SyncWorker_hpp */
