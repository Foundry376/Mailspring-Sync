/** Contact [MailSync]
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

#ifndef Contact_hpp
#define Contact_hpp

#include <stdio.h>
#include <string>
#include <unordered_set>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/vcard.hpp"
#include "MailCore/MailCore.h"

using namespace nlohmann;
using namespace std;
using namespace mailcore;

static string CONTACT_SOURCE_MAIL = "mail";
static int CONTACT_MAX_REFS = 100000;

class Message;

class Contact : public MailModel {

public:
    static string TABLE_NAME;

    Contact(string id, string accountId, string email, int refs, string source);
    Contact(json json);
    Contact(SQLite::Statement & query);

    string name();
    void setName(string name);
    string googleResourceName();
    void setGoogleResourceName(string rn);
    string email();
    void setEmail(string email);
    bool hidden();
    void setHidden(bool b);
    string source();
    string searchContent();
    json info();
    void setInfo(json info);
    string etag();
    void setEtag(string etag);
    string bookId();
    void setBookId(string bookId);

    unordered_set<string> groupIds();
    void setGroupIds(unordered_set<string> set);

    int refs();
    void incrementRefs();

    void mutateCardInInfo(function<void(shared_ptr<VCard>)> yieldBlock);

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);
};

#endif /* Contact_hpp */
