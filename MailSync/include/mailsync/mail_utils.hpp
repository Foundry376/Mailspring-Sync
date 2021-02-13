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

#include <string>
#include <stdio.h>
#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"
#include "nlohmann/json.hpp"
#include "mailsync/xoauth2_token_manager.hpp"

class File;
class Label;
class Account;
class Message;
class Query;

using namespace nlohmann;
using namespace std;
using namespace mailcore;


class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static string toBase58(const unsigned char * pbegin, size_t len);
    static string toBase64(const char * pbegin, size_t len);

    static string getEnvUTF8(string key);

    static json merge(const json &a, const json &b);
    static json contactJSONFromAddress(Address * addr);
    static Address * addressFromContactJSON(json & j);

    static string contactKeyForEmail(string email);

    static string localTimestampForTime(time_t time);

    static vector<uint32_t> uidsOfArray(Array * array);

    static vector<Query> queriesForUIDRangesInIndexSet(string remoteFolderId, IndexSet * set);

    static string pathForFile(string root, File * file, bool create);

    static string namespacePrefixOrBlank(IMAPSession * session);

    static vector<string> roles();
    static string roleForFolder(string mainPrefix, IMAPFolder * folder);
    static string roleForFolderViaFlags(string mainPrefix, IMAPFolder * folder);
    static string roleForFolderViaPath(string mainPrefix, IMAPFolder * folder);

    static void setBaseIDVersion(time_t identityCreationDate);

    static string idRandomlyGenerated();
    static string idForEvent(string accountId, string calendarId, string etag);
    static string idForCalendar(string accountId, string url);
    static string idForMessage(string accountId, string folderPath, IMAPMessage * msg);
    static string idForFolder(string accountId, string folderPath);
    static string idForFile(Message * message, Attachment * attachment);
    static string idForDraftHeaderMessageId(string accountId, string headerMessageId);

    static shared_ptr<Label> labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> allLabels);

    static string qmarks(size_t count);
    static string qmarkSets(size_t count, size_t perSet);

    static XOAuth2Parts userAndTokenFromXOAuth2(string xoauth2);

    static void enableVerboseLogging();
    static void configureSessionForAccount(IMAPSession & session, shared_ptr<Account> account);
    static void configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account);

    static IMAPMessagesRequestKind messagesRequestKindFor(IndexSet * capabilities, bool heavyOrNeedToComputeIDs);

    static void sleepWorkerUntilWakeOrSec(int sec);
    static void wakeAllWorkers();

    template<typename T>
    static vector<vector<T>> chunksOfVector(vector<T> & v, size_t chunkSize) {
        vector<vector<T>> results{};

        while (v.size() > 0) {
            auto from = v.begin();
            auto to = v.size() > chunkSize ? from + chunkSize : v.end();

            results.push_back(vector<T>{std::make_move_iterator(from), std::make_move_iterator(to)});
            v.erase(from, to);
        }
        return results;
    }
};

#endif /* MailUtils_hpp */
