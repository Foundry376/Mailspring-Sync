#ifndef Summary_hpp
#define Summary_hpp

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

class Message;

class Summary : public MailModel {
    
public:
    static string TABLE_NAME;

    Summary(Message * msg);
    Summary(json json);
    Summary(SQLite::Statement & query);
  
    string messageId();
    string threadId();
    string briefSummary();
    void setBriefSummary(string s);
    string messageSummary();
    void setMessageSummary(string s);
    string threadSummary();
    void setThreadSummary(string s);
    bool isImportant();
    void setImportant(bool v);
    bool isEmergency();
    void setEmergency(bool v);
    string category();
    void setCategory(string s);

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Summary_hpp */ 