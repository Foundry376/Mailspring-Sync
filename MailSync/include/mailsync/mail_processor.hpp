/** MailProcessor [MailSync]
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

#ifndef MailProcessor_hpp
#define MailProcessor_hpp

#include <stdio.h>

#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"

#include "mailsync/models/folder.hpp"
#include "mailsync/models/message.hpp"
#include "mailsync/query.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/contact.hpp"
#include "mailsync/models/account.hpp"

#include "mailsync/mail_store.hpp"

using namespace mailcore;
using namespace std;

class MailProcessor {
    MailStore * store;
    shared_ptr<Account> account;
    shared_ptr<spdlog::logger> logger;

public:
    MailProcessor(shared_ptr<Account> account, MailStore * store);
    shared_ptr<Message> insertFallbackToUpdateMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp);
    shared_ptr<Message> insertMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp);
    void updateMessage(Message * local, IMAPMessage * remote, Folder & folder, time_t syncDataTimestamp);
    void retrievedMessageBody(Message * message, MessageParser * parser);
    bool retrievedFileData(File * file, Data * data);
    void unlinkMessagesMatchingQuery(Query & query, int phase);
    void deleteMessagesStillUnlinkedFromPhase(int phase);

private:
    void appendToThreadSearchContent(Thread * thread, Message * messageToAppendOrNull, String * bodyToAppendOrNull);
    void upsertThreadReferences(string threadId, string accountId, string headerMessageId, Array * references);
    void upsertContacts(Message * message);
    shared_ptr<Label> labelForXGMLabelName(string mlname);
};

#endif /* MailProcessor_hpp */
