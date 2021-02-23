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
#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/models/folder.hpp"

#include "nlohmann/json.hpp"




class File;
class MailStore;
class Message;

// Snapshot concept

struct MessageSnapshot {
    bool unread;
    bool starred;
    bool inAllMail;
    size_t fileCount;
    nlohmann::json remoteXGMLabels;
    std::string clientFolderId;
};

static MessageSnapshot MessageEmptySnapshot = MessageSnapshot{false, false, false, 0, nullptr, ""};

// Message

class Message : public MailModel {

    std::string _bodyForDispatch;
    MessageSnapshot _lastSnapshot;

public:
    static std::string TABLE_NAME;

    static std::shared_ptr<Message> messageWithDeletionPlaceholderFor(std::shared_ptr<Message> draft);

    Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp);
    Message(SQLite::Statement & query);
    Message(nlohmann::json json);

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

    std::string threadId();
    void setThreadId(std::string threadId);

    std::string snippet();
    void setSnippet(std::string s);

    bool plaintext();
    void setPlaintext(bool p);

    std::string replyToHeaderMessageId();
    void setReplyToHeaderMessageId(std::string s);

    std::string forwardedHeaderMessageId();
    void setForwardedHeaderMessageId(std::string s);

    nlohmann::json files();
    void setFiles(std::vector<File> & files);
    int fileCountForThreadList();

    bool isDraft();
    void setDraft(bool d);

    time_t syncedAt();
    void setSyncedAt(time_t t);

    int syncUnsavedChanges();
    void setSyncUnsavedChanges(int t);

    void setBodyForDispatch(std::string s);

    bool isSentByUser();
    bool isInInbox();
    bool _isIn(std::string roleAlsoLabelName);

    nlohmann::json & remoteXGMLabels();
    void setRemoteXGMLabels(nlohmann::json & labels);

    uint32_t remoteUID();
    void setRemoteUID(uint32_t v);

    nlohmann::json clientFolder();
    std::string clientFolderId();
    void setClientFolder(Folder * folder);

    nlohmann::json remoteFolder();
    std::string remoteFolderId();
    void setRemoteFolder(nlohmann::json folder);
    void setRemoteFolder(Folder * folder);

    // immutable attributes

    nlohmann::json & to();
    nlohmann::json & cc();
    nlohmann::json & bcc();
    nlohmann::json & from();
    nlohmann::json & replyTo();

    time_t date();
    std::string subject();
    std::string gMsgId();
    std::string headerMessageId();

    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

    nlohmann::json toJSONDispatch();

    bool _skipThreadUpdatesAfterSave;
};

#endif /* Message_hpp */
