/** Task [MailSync]
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

#ifndef Task_hpp
#define Task_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"





class Task : public MailModel {

public:
    static std::string TABLE_NAME;

    Task(std::string constructorName, std::string accountId, nlohmann::json taskSpecificData);

    Task(nlohmann::json json);
    Task(SQLite::Statement & query);

    nlohmann::json & data();
    std::string tableName();
    std::string constructorName();

    std::string status();
    void setStatus(std::string s);

    bool shouldCancel();
    void setShouldCancel();

    nlohmann::json error();
    void setError(nlohmann::json e);

    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Task_hpp */
