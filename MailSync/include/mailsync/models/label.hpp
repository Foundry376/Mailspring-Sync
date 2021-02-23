/** Label [MailSync]
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

#ifndef Label_hpp
#define Label_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"

#include "mailsync/models/folder.hpp"




class Label : public Folder {

public:
    static std::string TABLE_NAME;

    Label(std::string id, std::string accountId, int version);
    Label(SQLite::Statement & query);

    std::string tableName();
};

#endif /* Folder_hpp */
