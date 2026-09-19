//
//  Query.hpp
//  mailcore2
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 MailCore. All rights reserved.
//

#ifndef Query_hpp
#define Query_hpp

#include <stdio.h>
#include <string>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>

#include "json.hpp"

using namespace nlohmann;
using namespace std;


class Query {
    json _clauses;
    int _limit;
    string _orderBy;
    string _orderDir;

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

    // Bounded range, both ends inclusive. _clauses is keyed by column, so chaining
    // gte() and lte() on one column silently keeps only the last of the two.
    Query & betweenInclusive(string col, double lo, double hi);

    Query & limit(int l);
    Query & orderBy(string col, string dir = "ASC");

    int getLimit();
    std::string getSQL();

    void bind(SQLite::Statement & query);
};


#endif /* Query_hpp */
