#include "mailsync/models/identity.hpp"

#include <time.h>
#include <iomanip>
#include <sstream>




// Singleton implementation

std::shared_ptr<Identity> Identity::_global = nullptr;

std::shared_ptr<Identity> Identity::GetGlobal() {
    return _global;
}

void Identity::SetGlobal(std::shared_ptr<Identity> i) {
    _global = i;
}


// Class implementation

Identity::Identity(nlohmann::json json) : MailModel(json) {

}

bool Identity::valid() {
    if (!_data.count("emailAddress") || !_data.count("token")) {
        return false;
    }
    return true;
}

time_t Identity::createdAt() {
    struct tm timeinfo {};
    memset(&timeinfo, 0, sizeof(struct tm));
    std::istringstream ss(_data["createdAt"].get<std::string>());
    ss >> std::get_time(&timeinfo, "%Y-%m-%dT%H:%M:%S.000Z");
    return mktime(&timeinfo);
}

std::string Identity::firstName() {
    return _data["firstName"].get<std::string>();
}

std::string Identity::lastName() {
    return _data["lastName"].get<std::string>();
}

std::string Identity::emailAddress() {
    return _data["emailAddress"].get<std::string>();
}

std::string Identity::token() {
    return _data["token"].get<std::string>();
}

/* Identity objects are not stored in the database. */

std::string Identity::tableName(){
    assert(false);
	return "";
}
std::string Identity::constructorName() {
    assert(false);
	return "";
}

std::vector<std::string> Identity::columnsForQuery() {
    assert(false);
	return {};
}

void Identity::bindToQuery(SQLite::Statement * query) {
    assert(false);
}
