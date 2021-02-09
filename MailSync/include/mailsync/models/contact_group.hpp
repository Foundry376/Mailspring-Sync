/** ContactGroup [MailSync]
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

#ifndef ContactGroup_hpp
#define ContactGroup_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/mail_store.hpp"
#include "MailCore/MailCore.h"

using namespace nlohmann;
using namespace std;
using namespace mailcore;


class ContactGroup : public MailModel {

public:
    static string TABLE_NAME;

    ContactGroup(string id, string accountId);
    ContactGroup(SQLite::Statement & query);

    string name();
    void setName(string name);
    string bookId();
    void setBookId(string bookId);
    string googleResourceName();
    void setGoogleResourceName(string rn);

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterRemove(MailStore * store);

    vector<string> getMembers(MailStore * store);
    void syncMembers(MailStore * store, vector<string> newContactIds);
};

#endif /* ContactGroup_hpp */
