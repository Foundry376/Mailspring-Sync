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

using json = nlohmann::json;


Query & Query::equal(std::string col, std::string val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(std::string col, double val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(std::string col, std::vector<std::string> & val) {
    clauses[col] = val;
    return *this;
}

Query & Query::equal(std::string col, std::vector<uint32_t> & val) {
    clauses[col] = val;
    return *this;
}

std::string Query::sql() {
    std::string result = "";

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
                std::string qmarks{"?,"};
                for (int i = 1; i < it.value().size(); i ++) {
                    qmarks = qmarks + "?,";
                }
                qmarks.pop_back();
                result += it.key() + " IN (" + qmarks + ")";
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
                    query.bind(ii++, at->get<std::string>());
                } else {
                    throw "Unsure of how to bind json to sqlite";
                }
            }
        } else {
            if (it.value().is_number()) {
                query.bind(ii++, it.value().get<double>());
            } else if (it.value().is_string()) {
                query.bind(ii++, it.value().get<std::string>());
            } else {
                throw "Unsure of how to bind json to sqlite";
            }
        }
    }
}
