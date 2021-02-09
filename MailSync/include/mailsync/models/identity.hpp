/** Identity [MailSync]
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

#ifndef Identity_hpp
#define Identity_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"

using namespace nlohmann;
using namespace std;

class Identity : public MailModel {

public:
    static shared_ptr<Identity> _global;
    static shared_ptr<Identity> GetGlobal();
    static void SetGlobal(shared_ptr<Identity> i);

    static string TABLE_NAME;

    Identity(json json);

    bool valid();

    time_t createdAt();
    string firstName();
    string lastName();
    string emailAddress();
    string token();


    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Identity_hpp */
