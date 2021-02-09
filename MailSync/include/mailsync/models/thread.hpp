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

using namespace nlohmann;
using namespace std;


class Thread : public MailModel {

    time_t _initialLMST;
    time_t _initialLMRT;
    map<string, bool> _initialCategoryIds;

public:
    static string TABLE_NAME;

    Thread(string msgId, string accountId, string subject, uint64_t gThreadId);
    Thread(SQLite::Statement & query);

    bool supportsMetadata();

    string subject();
    void setSubject(string s);
    int unread();
    void setUnread(int u);
    int starred();
    void setStarred(int s);
    int attachmentCount();
    void setAttachmentCount(int s);

    uint64_t searchRowId();
    void setSearchRowId(uint64_t s);

    json & participants();
    string gThrId();
    bool inAllMail();
    time_t lastMessageTimestamp();
    time_t firstMessageTimestamp();
    time_t lastMessageReceivedTimestamp();
    time_t lastMessageSentTimestamp();

    json & folders();
    json & labels();
    string categoriesSearchString();

    void resetCountedAttributes();
    void applyMessageAttributeChanges(MessageSnapshot & old, Message * next, vector<shared_ptr<Label>> allLabels);
    void upsertReferences(SQLite::Database & db, string headerMessageId, mailcore::Array * references);

    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

private:
    map<string, bool> captureCategoryIDs();
    void captureInitialState();
    void addMissingParticipants(std::map<std::string, bool> & existing, json & incoming);

};

#endif /* Thread_hpp */
