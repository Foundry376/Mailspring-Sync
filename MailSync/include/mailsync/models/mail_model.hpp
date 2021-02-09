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

using namespace std;
using namespace nlohmann;

class MailStore;

class MailModel {
public:
    json _data;

    map<string, int> _initialMetadataPluginIds;

    static string TABLE_NAME;
    virtual string tableName();

    MailModel(string id, string accountId, int version = 0);
    MailModel(SQLite::Statement & query);
    MailModel(json json);

    void captureInitialMetadataState();

    string id();
    string accountId();
    int version();
    void incrementVersion();

    int upsertMetadata(string pluginId, const json & value, int version = -1);
    json & metadata();

    virtual bool supportsMetadata();

    virtual void bindToQuery(SQLite::Statement * query);

    virtual void beforeSave(MailStore * store);
    virtual void afterSave(MailStore * store);
    virtual void afterRemove(MailStore * store);

    virtual vector<string> columnsForQuery() = 0;

    virtual json toJSON();
    virtual json toJSONDispatch();
};

#endif /* MailModel_hpp */
