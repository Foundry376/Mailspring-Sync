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
#include <functional>
#include <string>
#include <unordered_set>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/vcard.hpp"
#include "MailCore/MailCore.h"





static std::string CONTACT_SOURCE_MAIL = "mail";
static int CONTACT_MAX_REFS = 100000;

class Message;

class Contact : public MailModel {

public:
    static std::string TABLE_NAME;

    Contact(std::string id, std::string accountId, std::string email, int refs, std::string source);
    Contact(nlohmann::json json);
    Contact(SQLite::Statement & query);

    std::string name();
    void setName(std::string name);
    std::string googleResourceName();
    void setGoogleResourceName(std::string rn);
    std::string email();
    void setEmail(std::string email);
    bool hidden();
    void setHidden(bool b);
    std::string source();
    std::string searchContent();
    nlohmann::json info();
    void setInfo(nlohmann::json info);
    std::string etag();
    void setEtag(std::string etag);
    std::string bookId();
    void setBookId(std::string bookId);

    std::unordered_set<std::string> groupIds();
    void setGroupIds(std::unordered_set<std::string> set);

    int refs();
    void incrementRefs();

    void mutateCardInInfo(std::function<void(std::shared_ptr<VCard>)> yieldBlock);

    std::string tableName();
    std::string constructorName();

    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);
};

#endif /* Contact_hpp */
