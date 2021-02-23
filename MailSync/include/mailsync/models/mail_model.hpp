/** MailModel [MailSync]
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

#ifndef MailModel_hpp
#define MailModel_hpp

#include <stdio.h>
#include <vector>
#include <string>

#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"

#include "nlohmann/json.hpp"




class MailStore;

class MailModel {
public:
    nlohmann::json _data;

    std::map<std::string, int> _initialMetadataPluginIds;

    static std::string TABLE_NAME;
    virtual std::string tableName();

    MailModel(std::string id, std::string accountId, int version = 0);
    MailModel(SQLite::Statement & query);
    MailModel(nlohmann::json json);

    void captureInitialMetadataState();

    std::string id();
    std::string accountId();
    int version();
    void incrementVersion();

    int upsertMetadata(std::string pluginId, const nlohmann::json & value, int version = -1);
    nlohmann::json & metadata();

    virtual bool supportsMetadata();

    virtual void bindToQuery(SQLite::Statement * query);

    virtual void beforeSave(MailStore * store);
    virtual void afterSave(MailStore * store);
    virtual void afterRemove(MailStore * store);

    virtual std::vector<std::string> columnsForQuery() = 0;

    virtual nlohmann::json toJSON();
    virtual nlohmann::json toJSONDispatch();
};

#endif /* MailModel_hpp */
