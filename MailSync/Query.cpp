//
//  Query.cpp
//  mailcore2
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 MailCore. All rights reserved.
//

#include "Query.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

#include "json.hpp"
#include "MailUtils.hpp"

using namespace nlohmann;
using namespace std;


Query & Query::equal(string col, string val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(string col, double val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(string col, vector<string> & val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(string col, vector<uint32_t> & val) {
    clauses[col] = val;
    return *this;
}

string Query::sql() {
    string result = "";

    if (clauses.size() > 0) {
        result += " WHERE ";

        for (json::iterator it = clauses.begin(); it != clauses.end(); ++it) {
            if (it != clauses.begin()) {
                result += " AND ";
            }
            if (it.value().is_array()) {
                if (it.value().size() == 0) {
                    result += "0 = 1";
                    continue;
                }
                result += it.key() + " IN (" + MailUtils::qmarks(it.value().size()) + ")";
            } else {
                result += it.key() + " = ?";
            }
        }
    }
    return result;
}

void Query::bind(SQLite::Statement & query) {
    int ii = 1;
    for (json::iterator it = clauses.begin(); it != clauses.end(); ++it) {
        if (it.value().is_array()) {
            for (json::iterator at = it.value().begin(); at != it.value().end(); ++at) {
                if (at->is_number()) {
                    query.bind(ii++, at->get<double>());
                } else if (at->is_string()) {
                    query.bind(ii++, at->get<string>());
                } else {
                    throw "Unsure of how to bind json to sqlite";
                }
            }
        } else {
            if (it.value().is_number()) {
                query.bind(ii++, it.value().get<double>());
            } else if (it.value().is_string()) {
                query.bind(ii++, it.value().get<string>());
            } else {
                throw "Unsure of how to bind json to sqlite";
            }
        }
    }
}
