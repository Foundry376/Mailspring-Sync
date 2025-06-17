#include "ContactRelation.hpp"
#include "MailUtils.hpp"

using namespace std;

string ContactRelation::TABLE_NAME = "ContactRelation";

ContactRelation::ContactRelation() : MailModel(json::object()) {
    _data["email"] = "";
    _data["relation"] = "";
}

ContactRelation::ContactRelation(string accountId, string email, string relation) :
    MailModel(MailUtils::idRandomlyGenerated(), accountId, 0)
{
    _data["email"] = email;
    _data["relation"] = relation;
}

ContactRelation::ContactRelation(json json) : MailModel(json) {
    if (json.contains("email")) _data["email"] = json["email"];
    if (json.contains("relation")) _data["relation"] = json["relation"];
}

ContactRelation::ContactRelation(SQLite::Statement & query) :
    MailModel(query)
{
}

string ContactRelation::constructorName() {
    return _data["__cls"].get<string>();
}

string ContactRelation::tableName() {
    return ContactRelation::TABLE_NAME;
}

string ContactRelation::email() {
    return _data["email"].get<string>();
}

void ContactRelation::setEmail(string s) {
    _data["email"] = s;
}

string ContactRelation::relation() {
    return _data["relation"].get<string>();
}

void ContactRelation::setRelation(string s) {
    _data["relation"] = s;
}

vector<string> ContactRelation::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "email", "relation"};
}

void ContactRelation::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":email", email());
    query->bind(":relation", relation());
} 