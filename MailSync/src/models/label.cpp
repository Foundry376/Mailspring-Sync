#include "mailsync/models/label.hpp"
#include "mailsync/mail_utils.hpp"

using namespace std;

Label::Label(string id, string accountId, int version) :
    Folder(id, accountId, version)
{
}

Label::Label(SQLite::Statement & query) :
    Folder(query)
{
}

string Label::TABLE_NAME = "Label";

string Label::tableName() {
    return Label::TABLE_NAME;
}
