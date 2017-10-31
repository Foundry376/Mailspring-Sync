//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef TaskProcessor_hpp
#define TaskProcessor_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "Task.hpp"
#include "Thread.hpp"
#include "Message.hpp"
#include "MailModel.hpp"
#include "MailStore.hpp"
#include "Account.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;

struct ChangeMailModels {
    vector<shared_ptr<Message>> messages;
};


class TaskProcessor {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;
    IMAPSession * session;
    
public:
    TaskProcessor(shared_ptr<Account> account, MailStore * store, IMAPSession * session);

    void cleanupTasksAfterLaunch();
    
    void performLocal(Task * task);
    void performRemote(Task * task);
    void cancel(string taskId);
    
private:
    ChangeMailModels inflateMessages(json & data);
    ChangeMailModels inflateThreadsAndMessages(json & data);
    Message inflateDraft(json & draftJSON);

    void performLocalChangeOnMessages(Task * task,  void (*modifyLocalMessage)(Message *, json &));
    void performRemoteChangeOnMessages(Task * task, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data));
    void performLocalSaveDraft(Task * task);
    void performLocalDestroyDraft(Task * task);
    void performRemoteDestroyDraft(Task * task);

    void performLocalSyncbackCategory(Task * task);
    void performRemoteSyncbackCategory(Task * task);

    void performLocalSyncbackMetadata(Task * task);
    void performRemoteSyncbackMetadata(Task * task);
    
    void performRemoteDestroyCategory(Task * task);
    void performRemoteSendDraft(Task * task);

    void performRemoteSendFeatureUsageEvent(Task * task);

    void performLocalChangeRoleMapping(Task * task);

    void performRemoteExpungeAllInFolder(Task * task);
};

#endif /* TaskProcessor_hpp */
