/** File [MailSync]
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

#ifndef File_hpp
#define File_hpp

#include <stdio.h>
#include <string>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/mail_store.hpp"
#include "MailCore/MailCore.h"





class Message;

class File : public MailModel {

public:
    static std::string TABLE_NAME;

    File(Message * msg, mailcore::Attachment * a);
    File(nlohmann::json json);
    File(SQLite::Statement & query);

    std::string filename();
    std::string safeFilename();
    std::string partId();
    nlohmann::json & contentId();
    void setContentId(std::string s);
    std::string contentType();

    std::string tableName();
    std::string constructorName();

    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* File_hpp */
