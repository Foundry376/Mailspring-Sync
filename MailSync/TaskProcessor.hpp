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
#include <MailCore/MailCore.h>

using json = nlohmann::json;
using namespace std;
using namespace mailcore;

struct ChangeMailModels {
    vector<shared_ptr<Message>> messages;
    map<string, shared_ptr<Thread>> threads;
};


class TaskProcessor {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    IMAPSession * session;
    
public:
    TaskProcessor(MailStore * store, shared_ptr<spdlog::logger> logger, IMAPSession * session);

    void performLocal(Task * task);
    void performRemote(Task * task);
    
private:
    ChangeMailModels inflateThreadsAndMessages(json & data);

    void performLocalChangeOnMessages(Task * task,  void (*modifyLocalMessage)(Message *, json &));
    void performRemoteChangeOnMessages(Task * task, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, json & data));
    
};

#endif /* TaskProcessor_hpp */
