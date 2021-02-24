/** MailUtils [MailSync]
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

#ifndef MailUtils_hpp
#define MailUtils_hpp

#include <memory>
#include <string>
#include <vector>

#include <stdio.h>
#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"
#include "nlohmannjson.hpp"
#include "mailsync/xoauth2_token_manager.hpp"

class File;
class Label;
class Account;
class Message;
class Query;

class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static std::string toBase58(const unsigned char * pbegin, size_t len);
    static std::string toBase64(const char * pbegin, size_t len);

    static std::string getEnvUTF8(std::string key);

    static nlohmann::json merge(const nlohmann::json &a, const nlohmann::json &b);
    static nlohmann::json contactJSONFromAddress(mailcore::Address * addr);
    static mailcore::Address * addressFromContactJSON(nlohmann::json & j);

    static std::string contactKeyForEmail(std::string email);

    static std::string localTimestampForTime(time_t time);

    static std::vector<uint32_t> uidsOfArray(mailcore::Array * array);

    static std::vector<Query> queriesForUIDRangesInIndexSet(std::string remoteFolderId, mailcore::IndexSet * set);

    static std::string pathForFile(std::string root, File * file, bool create);

    static std::string namespacePrefixOrBlank(mailcore::IMAPSession * session);

    static std::vector<std::string> roles();
    static std::string roleForFolder(std::string mainPrefix, mailcore::IMAPFolder * folder);
    static std::string roleForFolderViaFlags(std::string mainPrefix, mailcore::IMAPFolder * folder);
    static std::string roleForFolderViaPath(std::string mainPrefix, mailcore::IMAPFolder * folder);

    static void setBaseIDVersion(time_t identityCreationDate);

    static std::string idRandomlyGenerated();
    static std::string idForEvent(std::string accountId, std::string calendarId, std::string etag);
    static std::string idForCalendar(std::string accountId, std::string url);
    static std::string idForMessage(std::string accountId, std::string folderPath, mailcore::IMAPMessage * msg);
    static std::string idForFolder(std::string accountId, std::string folderPath);
    static std::string idForFile(Message * message, mailcore::Attachment * attachment);
    static std::string idForDraftHeaderMessageId(std::string accountId, std::string headerMessageId);

    static std::shared_ptr<Label> labelForXGMLabelName(std::string mlname, std::vector<std::shared_ptr<Label>> allLabels);

    static std::string qmarks(size_t count);
    static std::string qmarkSets(size_t count, size_t perSet);

    static XOAuth2Parts userAndTokenFromXOAuth2(std::string xoauth2);

    static void enableVerboseLogging();
    static void configureSessionForAccount(mailcore::IMAPSession & session, std::shared_ptr<Account> account);
    static void configureSessionForAccount(mailcore::SMTPSession & session, std::shared_ptr<Account> account);

    static mailcore::IMAPMessagesRequestKind messagesRequestKindFor(mailcore::IndexSet * capabilities, bool heavyOrNeedToComputeIDs);

    static void sleepWorkerUntilWakeOrSec(int sec);
    static void wakeAllWorkers();

    template<typename T>
    static std::vector<std::vector<T>> chunksOfVector(std::vector<T> & v, size_t chunkSize) {
        std::vector<std::vector<T>> results{};

        while (v.size() > 0) {
            auto from = v.begin();
            auto to = v.size() > chunkSize ? from + chunkSize : v.end();

            results.push_back(std::vector<T>{std::make_move_iterator(from), std::make_move_iterator(to)});
            v.erase(from, to);
        }
        return results;
    }
};

#endif /* MailUtils_hpp */
