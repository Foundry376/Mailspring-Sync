//
//  Message.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Message.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"

Message::Message(mailcore::IMAPMessage * msg, Folder & folder) :
MailModel(MailUtils::idForMessage(msg), 0)
{
    _folderId = folder.id();
    updateAttributes(msg);
}

Message::Message(SQLite::Statement & query) :
    MailModel(query),
    _date(query.getColumn("date").getDouble()),
    _unread(query.getColumn("unread").getInt()),
    _starred(query.getColumn("starred").getInt()),
    _subject(query.getColumn("subject").getString()),
    _headerMessageId(query.getColumn("headerMessageId").getString()),
    _folderImapUID(query.getColumn("folderImapUID").getUInt()),
    _folderId(query.getColumn("folderId").getString()),
    _threadId(query.getColumn("threadId").getString()),
    _gMsgId(query.getColumn("gMsgId").getString())
{

}

bool Message::isUnread() {
    return _unread;
}

bool Message::isStarred() {
    return _starred;
}

double Message::date() {
    return _date;
}

void Message::setThreadId(std::string threadId) {
    _threadId = threadId;
}

std::string Message::getHeaderMessageId() {
    return _headerMessageId;
}

std::string Message::tableName() {
    return "messages";
}

std::vector<std::string> Message::columnsForQuery() {
    return std::vector<std::string>{"id", "version", "headerMessageId", "subject", "gMsgId", "date", "draft", "isSent", "unread", "starred", "folderImapUID", "folderImapXGMLabels", "folderId", "threadId"};
}

void Message::bindToQuery(SQLite::Statement & query) {
    query.bind(":id", _id);
    query.bind(":version", _version);
    query.bind(":date", _date);
    query.bind(":unread", _unread);
    query.bind(":starred", _starred);
    query.bind(":headerMessageId", _headerMessageId);
    query.bind(":subject", _subject);
    query.bind(":folderImapUID", _folderImapUID);
    query.bind(":folderId", _folderId);
    query.bind(":threadId", _threadId);
    query.bind(":gMsgId", _gMsgId);
}


void Message::updateAttributes(mailcore::IMAPMessage * msg) {
    if (_id != MailUtils::idForMessage(msg)) {
        throw "Assertion Failure: updateAttributes given a msg with a different ID";
    }
    _unread = !(msg->flags() & mailcore::MessageFlagSeen);
    _starred = msg->flags() & mailcore::MessageFlagFlagged;
    _date = (double)msg->header()->receivedDate();
    _headerMessageId = msg->header()->messageID()->UTF8Characters();
    _subject = msg->header()->subject()->UTF8Characters();
    _folderImapUID = msg->uid();
    
    mailcore::String * mgMsgId = msg->header()->extraHeaderValueForName(MCSTR("X-GM-MSGID"));
    if (mgMsgId) {
        _gMsgId = mgMsgId->UTF8Characters();
    }

}
