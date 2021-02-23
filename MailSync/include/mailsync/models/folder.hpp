/** Folder [MailSync]
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

#ifndef Folder_hpp
#define Folder_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"

#include "mailsync/models/mail_model.hpp"




class Folder : public MailModel {

public:
    static std::string TABLE_NAME;

    Folder(nlohmann::json & json);
    Folder(std::string id, std::string accountId, int version);
    Folder(SQLite::Statement & query);

    nlohmann::json & localStatus();

    std::string path();
    void setPath(std::string path);

    std::string role() const;
    void setRole(std::string role);

    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void beforeSave(MailStore * store);
    void afterRemove(MailStore * store);
};

#endif /* Folder_hpp */
