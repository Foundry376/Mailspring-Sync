/** Query [MailSync]
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

#ifndef Query_hpp
#define Query_hpp

#include <stdio.h>
#include <string>
#include <vector>

#include "SQLiteCpp/SQLiteCpp.h"

#include "nlohmann/json.hpp"

using namespace nlohmann;
using namespace std;


class Query {
    json _clauses;
    int _limit;

public:
    Query() noexcept;

    Query & equal(string col, string val);
    Query & equal(string col, double val);
    Query & equal(string col, vector<string> & val);
    Query & equal(string col, vector<uint32_t> & val);

    Query & gt(string col, double val);
    Query & gte(string col, double val);
    Query & lt(string col, double val);
    Query & lte(string col, double val);

    Query & limit(int l);

    int getLimit();
    std::string getSQL();

    void bind(SQLite::Statement & query);
};


#endif /* Query_hpp */
