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

using json = nlohmann::json;


class Query {
    json clauses;
    
public:
    Query & equal(std::string col, std::string val);
    Query & equal(std::string col, double val);
    Query & equal(std::string col, std::vector<std::string> & val);
    Query & equal(std::string col, std::vector<uint32_t> & val);

    std::string sql();

    void bind(SQLite::Statement & query);
};


#endif /* Query_hpp */
