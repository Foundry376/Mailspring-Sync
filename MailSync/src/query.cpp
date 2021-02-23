#include "mailsync/query.hpp"
#include "SQLiteCpp/SQLiteCpp.h"

#include "nlohmann/json.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/sync_exception.hpp"





// TODO: figure out how to use templates for this

Query::Query() noexcept : _clauses({}), _limit(0) {
}

Query & Query::equal(std::string col, std::string val) {
    _clauses[col] = {{"op","="}, {"rhs", val}};
    return *this;
}

Query & Query::equal(std::string col, double val) {
    _clauses[col] = {{"op","="}, {"rhs", val}};
    return *this;
}

Query & Query::equal(std::string col, std::vector<std::string> & val) {
    if (val.size() > 999) {
        spdlog::get("logger")->warn("Attempting to construct WHERE {} IN () query with >999 values ({}), this will fail and should be reported.", col, val.size());
    }
    _clauses[col] = {{"op","="}, {"rhs", val}};
    return *this;
}

Query & Query::equal(std::string col, std::vector<uint32_t> & val) {
    _clauses[col] = {{"op","="}, {"rhs", val}};
    return *this;
}

Query & Query::gt(std::string col, double val) {
    _clauses[col] = {{"op",">"}, {"rhs", val}};
    return *this;
}

Query & Query::gte(std::string col, double val) {
    _clauses[col] = {{"op",">="}, {"rhs", val}};
    return *this;
}

Query & Query::lt(std::string col, double val) {
    _clauses[col] = {{"op","<"}, {"rhs", val}};
    return *this;
}

Query & Query::lte(std::string col, double val) {
    _clauses[col] = {{"op","<="}, {"rhs", val}};
    return *this;
}

Query & Query::limit(int l) {
    _limit = l;
    return *this;
}

int Query::getLimit() {
    return _limit;
}

std::string Query::getSQL() {
    std::string result = "";

    if (_clauses.size() > 0) {
        result += " WHERE ";

        for (nlohmann::json::iterator it = _clauses.begin(); it != _clauses.end(); ++it) {
            if (it != _clauses.begin()) {
                result += " AND ";
            }
            std::string op = it.value()["op"].get<std::string>();
            nlohmann::json & rhs = it.value()["rhs"];

            if (rhs.is_array()) {
                if (op != "=") {
                    throw SyncException("query-builder", "Cannot use query operator " + op + " with an array of values", true);
                }
                if (rhs.size() == 0) {
                    result += "0 = 1";
                    continue;
                }
                result += it.key() + " IN (" + MailUtils::qmarks(rhs.size()) + ")";
            } else {
                result += it.key() + " " + op + " ?";
            }
        }
    }
    return result;
}

void Query::bind(SQLite::Statement & query) {
    int ii = 1;
    for (nlohmann::json::iterator it = _clauses.begin(); it != _clauses.end(); ++it) {
        nlohmann::json & rhs = it.value()["rhs"];

        if (rhs.is_array()) {
            for (nlohmann::json::iterator at = rhs.begin(); at != rhs.end(); ++at) {
                if (at->is_number()) {
                    query.bind(ii++, at->get<double>());
                } else if (at->is_string()) {
                    query.bind(ii++, at->get<std::string>());
                } else {
                    throw SyncException("query-builder", "Unsure of how to bind json to sqlite", true);
                }
            }
        } else {
            if (rhs.is_number()) {
                query.bind(ii++, rhs.get<double>());
            } else if (rhs.is_string()) {
                query.bind(ii++, rhs.get<std::string>());
            } else {
                throw SyncException("query-builder", "Unsure of how to bind json to sqlite", true);
            }
        }
    }
}
