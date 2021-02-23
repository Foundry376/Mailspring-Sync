#include "mailsync/models/label.hpp"
#include "mailsync/mail_utils.hpp"



Label::Label(std::string id, std::string accountId, int version) :
    Folder(id, accountId, version)
{
}

Label::Label(SQLite::Statement & query) :
    Folder(query)
{
}

std::string Label::TABLE_NAME = "Label";

std::string Label::tableName() {
    return Label::TABLE_NAME;
}
