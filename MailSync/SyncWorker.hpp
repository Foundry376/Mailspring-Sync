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


class SyncWorker {
    mailcore::IMAPSession session;
    MailStore * store;
    CommStream * stream;
    
public:
    SyncWorker();
    
    void syncNow();

    std::vector<Folder *> syncFolders();

    void syncFolderFullScan(Folder & folder, mailcore::IMAPFolderStatus & remoteStatus);
        
    void syncFolderRange(Folder & folder, mailcore::Range range);

    void syncFolderChangesViaCondstore(Folder & folder, mailcore::IMAPFolderStatus & remoteStatus);

    void syncFolderShallow(Folder & folder);
    
    void fetchMessagesInFolder(mailcore::String * folder, std::string folderId, mailcore::Range range);
};


#endif /* SyncWorker_hpp */
