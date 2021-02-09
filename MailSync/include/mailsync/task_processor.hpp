/** TaskProcessor [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TaskProcessor_hpp
#define TaskProcessor_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/task.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"
#include "mailsync/models/mail_model.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/models/account.hpp"
#include "MailCore/MailCore.h"

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
    void cleanupOldTasksAtRuntime();

    void performLocal(Task * task);
    void performRemote(Task * task);
    void cancel(string taskId);

private:
    ChangeMailModels inflateMessages(json & data);
    ChangeMailModels inflateThreadsAndMessages(json & data);
    Message inflateClientDraftJSON(json & draftJSON, shared_ptr<Message> existing);

    void performLocalChangeOnMessages(Task * task,  void (*modifyLocalMessage)(Message *, json &));
    void performRemoteChangeOnMessages(Task * task, bool updatesFolder, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data));
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
    void performRemoteSendRSVP(Task * task);

};

#endif /* TaskProcessor_hpp */
