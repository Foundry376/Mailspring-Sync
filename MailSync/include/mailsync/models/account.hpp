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





class Account : public MailModel {

public:
    static std::string TABLE_NAME;

    Account(nlohmann::json json);
    Account(SQLite::Statement & query);

    int startDelay();

    std::string valid();

    std::string refreshToken();
    std::string refreshClientId();
    std::string provider();
    std::string emailAddress();

    unsigned int IMAPPort();
    std::string IMAPHost();
    std::string IMAPUsername();
    std::string IMAPPassword();
    std::string IMAPSecurity();
    bool IMAPAllowInsecureSSL();

    unsigned int SMTPPort();
    std::string SMTPHost();
    std::string SMTPUsername();
    std::string SMTPPassword();
    std::string SMTPSecurity();
    bool SMTPAllowInsecureSSL();

    std::string tableName();
    std::string constructorName();

    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Account_hpp */
