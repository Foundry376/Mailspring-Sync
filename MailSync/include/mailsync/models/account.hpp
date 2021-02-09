/** Account [MailSync]
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

#ifndef Account_hpp
#define Account_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class Account : public MailModel {

public:
    static string TABLE_NAME;

    Account(json json);
    Account(SQLite::Statement & query);

    int startDelay();

    string valid();

    string refreshToken();
    string refreshClientId();
    string provider();
    string emailAddress();

    unsigned int IMAPPort();
    string IMAPHost();
    string IMAPUsername();
    string IMAPPassword();
    string IMAPSecurity();
    bool IMAPAllowInsecureSSL();

    unsigned int SMTPPort();
    string SMTPHost();
    string SMTPUsername();
    string SMTPPassword();
    string SMTPSecurity();
    bool SMTPAllowInsecureSSL();

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Account_hpp */
