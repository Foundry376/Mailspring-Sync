/** Thread [MailSync]
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

#ifndef Thread_hpp
#define Thread_hpp

#include <stdio.h>
#include <vector>
#include <string>

#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/models/label.hpp"
#include "mailsync/models/message.hpp"

#include "nlohmann/json.hpp"





class Thread : public MailModel {

    time_t _initialLMST;
    time_t _initialLMRT;
    std::map<std::string, bool> _initialCategoryIds;

public:
    static std::string TABLE_NAME;

    Thread(std::string msgId, std::string accountId, std::string subject, uint64_t gThreadId);
    Thread(SQLite::Statement & query);

    bool supportsMetadata();

    std::string subject();
    void setSubject(std::string s);
    int unread();
    void setUnread(int u);
    int starred();
    void setStarred(int s);
    int attachmentCount();
    void setAttachmentCount(int s);

    uint64_t searchRowId();
    void setSearchRowId(uint64_t s);

    nlohmann::json & participants();
    std::string gThrId();
    bool inAllMail();
    time_t lastMessageTimestamp();
    time_t firstMessageTimestamp();
    time_t lastMessageReceivedTimestamp();
    time_t lastMessageSentTimestamp();

    nlohmann::json & folders();
    nlohmann::json & labels();
    std::string categoriesSearchString();

    void resetCountedAttributes();
    void applyMessageAttributeChanges(MessageSnapshot & old, Message * next, std::vector<std::shared_ptr<Label>> allLabels);
    void upsertReferences(SQLite::Database & db, std::string headerMessageId, mailcore::Array * references);

    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

private:
    std::map<std::string, bool> captureCategoryIDs();
    void captureInitialState();
    void addMissingParticipants(std::map<std::string, bool> & existing, nlohmann::json & incoming);

};

#endif /* Thread_hpp */
