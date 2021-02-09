/** Message [MailSync]
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

#ifndef Message_hpp
#define Message_hpp

#include <stdio.h>
#include <vector>
#include <string>
#include <MailCore/MailCore.h>
#include "SQLiteCpp/SQLiteCpp.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/models/folder.hpp"

#include "nlohmann/json.hpp"

using namespace std;
using namespace nlohmann;

class File;
class MailStore;
class Message;

// Snapshot concept

struct MessageSnapshot {
    bool unread;
    bool starred;
    bool inAllMail;
    size_t fileCount;
    json remoteXGMLabels;
    string clientFolderId;
};

static MessageSnapshot MessageEmptySnapshot = MessageSnapshot{false, false, false, 0, nullptr, ""};

// Message

class Message : public MailModel {

    string _bodyForDispatch;
    MessageSnapshot _lastSnapshot;

public:
    static string TABLE_NAME;

    static shared_ptr<Message> messageWithDeletionPlaceholderFor(shared_ptr<Message> draft);

    Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp);
    Message(SQLite::Statement & query);
    Message(json json);

    bool supportsMetadata();

    // mutable attributes

    MessageSnapshot getSnapshot();

    bool isDeletionPlaceholder();
    bool isHiddenReminder();

    bool inAllMail();

    bool isUnread();
    void setUnread(bool u);

    bool isStarred();
    void setStarred(bool s);

    string threadId();
    void setThreadId(string threadId);

    string snippet();
    void setSnippet(string s);

    bool plaintext();
    void setPlaintext(bool p);

    string replyToHeaderMessageId();
    void setReplyToHeaderMessageId(string s);

    string forwardedHeaderMessageId();
    void setForwardedHeaderMessageId(string s);

    json files();
    void setFiles(vector<File> & files);
    int fileCountForThreadList();

    bool isDraft();
    void setDraft(bool d);

    time_t syncedAt();
    void setSyncedAt(time_t t);

    int syncUnsavedChanges();
    void setSyncUnsavedChanges(int t);

    void setBodyForDispatch(string s);

    bool isSentByUser();
    bool isInInbox();
    bool _isIn(string roleAlsoLabelName);

    json & remoteXGMLabels();
    void setRemoteXGMLabels(json & labels);

    uint32_t remoteUID();
    void setRemoteUID(uint32_t v);

    json clientFolder();
    string clientFolderId();
    void setClientFolder(Folder * folder);

    json remoteFolder();
    string remoteFolderId();
    void setRemoteFolder(json folder);
    void setRemoteFolder(Folder * folder);

    // immutable attributes

    json & to();
    json & cc();
    json & bcc();
    json & from();
    json & replyTo();

    time_t date();
    string subject();
    string gMsgId();
    string headerMessageId();

    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

    json toJSONDispatch();

    bool _skipThreadUpdatesAfterSave;
};

#endif /* Message_hpp */
