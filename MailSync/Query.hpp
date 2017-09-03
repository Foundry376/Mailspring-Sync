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
    json clauses;
    
public:
    Query & equal(string col, string val);
    Query & equal(string col, double val);
    Query & equal(string col, vector<string> & val);
    Query & equal(string col, vector<uint32_t> & val);

    std::string sql();

    void bind(SQLite::Statement & query);
};


#endif /* Query_hpp */
