#include <regex>

#include "mailsync/models/file.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"




std::string File::TABLE_NAME = "File";

File::File(Message * msg, mailcore::Attachment * a) :
    MailModel(MailUtils::idForFile(msg, a), msg->accountId(), 0)
{
    _data["messageId"] = msg->id();
    _data["partId"] = a->partID()->UTF8Characters();

    if (a->isInlineAttachment() && a->contentID()) {
        _data["contentId"] = a->contentID()->UTF8Characters();
    }
    if (a->mimeType()) {
        _data["contentType"] = a->mimeType()->UTF8Characters();
    }

    std::string name = "";
    if (a->filename()) {
        name = a->filename()->UTF8Characters();
    }
    if (name == "") {
        name = "Unnamed mailcore::Attachment";

        std::string type = _data["contentType"];
        if (type == "text/calendar") {
            name = "Event.ics";
        }
        if (type == "image/png" || type == "image/x-png") {
            name = "Unnamed Image.png";
        }
        if (type == "image/jpg") {
            name = "Unnamed Image.jpg";
        }
        if (type == "image/jpeg") {
            name = "Unnamed Image.jpg";
        }
        if (type == "image/gif") {
            name = "Unnamed Image.gif";
        }
        if (type == "message/delivery-status") {
            name = "Delivery Status.txt";
        }
        if (type == "message/feedback-report") {
            name = "Feedback Report.txt";
        }
    }

    _data["filename"] = name;
    _data["size"] = a->data()->length();
}

File::File(nlohmann::json json) : MailModel(json) {

}

File::File(SQLite::Statement & query) :
    MailModel(query)
{
}

std::string File::constructorName() {
    return _data["__cls"].get<std::string>();
}

std::string File::tableName() {
    return File::TABLE_NAME;
}

std::string File::filename() {
    return _data["filename"].get<std::string>();
}

std::string File::safeFilename() {
    std::regex e ("[\\/:|?*><\"#]");
    return std::regex_replace (filename(), e, "-");
}

std::string File::partId() {
    return _data["partId"].get<std::string>();
}

nlohmann::json & File::contentId() {
    return _data["contentId"];
}

void File::setContentId(std::string s) {
    _data["contentId"] = s;
}

std::string File::contentType() {
    return _data["contentType"].get<std::string>();
}

std::vector<std::string> File::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId", "version", "filename"};
}

void File::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":filename", filename());
}
