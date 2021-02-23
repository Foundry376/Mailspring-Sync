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





class Query {
    nlohmann::json _clauses;
    int _limit;

public:
    Query() noexcept;

    Query & equal(std::string col, std::string val);
    Query & equal(std::string col, double val);
    Query & equal(std::string col, std::vector<std::string> & val);
    Query & equal(std::string col, std::vector<uint32_t> & val);

    Query & gt(std::string col, double val);
    Query & gte(std::string col, double val);
    Query & lt(std::string col, double val);
    Query & lte(std::string col, double val);

    Query & limit(int l);

    int getLimit();
    std::string getSQL();

    void bind(SQLite::Statement & query);
};


#endif /* Query_hpp */
