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
