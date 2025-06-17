#ifndef ContactRelation_hpp
#define ContactRelation_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"
#include "MailStore.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class ContactRelation : public MailModel {
    
public:
    static string TABLE_NAME;

    ContactRelation(string accountId, string email, string relation);
    ContactRelation(json json);
    ContactRelation(SQLite::Statement & query);
  
    string email();
    void setEmail(string s);
    string relation();
    void setRelation(string s);

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* ContactRelation_hpp */ 