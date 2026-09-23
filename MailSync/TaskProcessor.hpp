//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
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
#include "Placement.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;

struct ChangeMailModels {
    vector<shared_ptr<Message>> messages;
};

// One physical copy a task addresses on the server and, after the remote phase, what
// became of it. The row is captured before any network I/O; the confirm step reloads
// the message and applies the outcome to the row that still matches (folder, UID).
struct TaskPlacement {
    shared_ptr<Message> message;
    Placement placement;
    string destFolderId; // moves: where this copy should end up
    bool moved = false;  // the copy is now at (destFolderId, movedUID)
    uint32_t movedUID = 0;
};

// The local variant runs inside a transaction with the message's placements in hand;
// the remote variant runs once per server folder holding a copy, with the items there.
typedef void (*LocalChangeFn)(MailStore * store, Message * msg, const vector<Placement> & placements, json & data);
typedef void (*RemoteChangeFn)(IMAPSession * session, MailStore * store, string accountId, Folder & source, vector<TaskPlacement *> & items, json & data);


class TaskProcessor {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;
    IMAPSession * session;
    
public:
    TaskProcessor(shared_ptr<Account> account, MailStore * store, IMAPSession * session);

    void cleanupTasksAfterLaunch();
    void cleanupOldTasksAtRuntime();
    
    void performLocal(Task * task);
    void performRemote(Task * task);
    void cancel(string taskId);
    
private:
    ChangeMailModels inflateMessages(json & data);
    ChangeMailModels inflateThreadsAndMessages(json & data);
    Message inflateClientDraftJSON(json & draftJSON, shared_ptr<Message> existing);

    shared_ptr<Folder> draftsFolder();
    void ensureDraftPlacement(Message & draft);

    void performLocalChangeOnMessages(Task * task, LocalChangeFn modifyLocalMessage);
    void performRemoteChangeOnMessages(Task * task, bool isMove, RemoteChangeFn applyInFolder);
    string confirmPlacementChange(Message & msg, TaskPlacement & item, const vector<Placement> & rows);
    void performLocalSaveDraft(Task * task);
    void performLocalDestroyDraft(Task * task);
    void performRemoteDestroyDraft(Task * task);

    void performLocalDestroyContact(Task * task);
    void performRemoteDestroyContact(Task * task);
    
    void performLocalSyncbackContact(Task * task);
    void performRemoteSyncbackContact(Task * task);
    
    void performLocalChangeContactGroupMembership(Task * task);
    void performRemoteChangeContactGroupMembership(Task * task);

    void performLocalSyncbackContactGroup(Task * task);
    void performRemoteSyncbackContactGroup(Task * task);

    void performLocalDestroyContactGroup(Task * task);
    void performRemoteDestroyContactGroup(Task * task);

    void performLocalSyncbackEvent(Task * task);
    void performRemoteSyncbackEvent(Task * task);

    void performLocalDestroyEvent(Task * task);
    void performRemoteDestroyEvent(Task * task);

    void performLocalSyncbackCategory(Task * task);
    void performRemoteSyncbackCategory(Task * task);

    void performLocalSyncbackMetadata(Task * task);
    void performRemoteSyncbackMetadata(Task * task);
    
    void performRemoteDestroyCategory(Task * task);
    void performRemoteSendDraft(Task * task);

    void performRemoteSendFeatureUsageEvent(Task * task);

    void performLocalChangeRoleMapping(Task * task);

    void performRemoteExpungeAllInFolder(Task * task);
    void performRemoteGetMessageRFC2822(Task * task);
    void performRemoteGetManyRFC2822(Task * task);
    void performRemoteSendRSVP(Task * task);

public:
    static std::string sanitizeEmlFilename(const std::string & subject, time_t date, int index);

};

#endif /* TaskProcessor_hpp */
