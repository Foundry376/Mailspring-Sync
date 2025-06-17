#ifndef Summary_hpp
#define Summary_hpp

#include <stdio.h>
#include <string>
#include <vector>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"

#include "MailModel.hpp"
#include "Message.hpp"

using namespace std;

class Summary : public MailModel {
    
public:
    static string TABLE_NAME;

    Summary();
    Summary(Message * msg);
    Summary(json json);
    Summary(SQLite::Statement & query);
    
    string constructorName();
    string tableName() override;
    
    string messageId();
    string threadId();
    string accountId();
    void setAccountId(string id);
    
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
    
    vector<string> columnsForQuery() override;
    void bindToQuery(SQLite::Statement * query) override;
};

#endif /* Summary_hpp */ 
