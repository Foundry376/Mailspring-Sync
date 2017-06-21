//
//  Thread.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Thread.hpp"
#include "MailUtils.hpp"

#define DEFAULT_SUBJECT "unassigned"

using namespace std;

string Thread::TABLE_NAME = "Thread";

Thread::Thread(SQLite::Statement & query) :
    MailModel(query),
    _unread(query.getColumn("unread").getInt()),
    _starred(query.getColumn("starred").getInt()),
    _subject(query.getColumn("subject").getString()),
    _firstMessageDate(query.getColumn("firstMessageTimestamp").getDouble()),
    _lastMessageDate(query.getColumn("lastMessageTimestamp").getDouble()),
    _lastMessageReceivedDate(query.getColumn("lastMessageReceivedTimestamp").getDouble()),
    _lastMessageSentDate(query.getColumn("lastMessageSentTimestamp").getDouble())
{
    
}

Thread::Thread(Message msg) :
    MailModel("t:" + msg.id(), msg.accountId(), 0),
    _unread(0),
    _starred(0),
    _subject(DEFAULT_SUBJECT)
{
    addMessage(msg);
}

string Thread::subject() {
    return _subject;
}

void Thread::setSubject(string s) {
    _subject = s;
}

int Thread::unread() {
    return _unread;
}

void Thread::setUnread(int u) {
    _unread = u;
}

int Thread::starred() {
    return _starred;
}

void Thread::setStarred(int s) {
    _starred = s;
}

void Thread::addMessage(Message & msg) {
    if (_subject == DEFAULT_SUBJECT) {
        _subject = msg.subject();
        _lastMessageDate = msg.date();
        _firstMessageDate = msg.date();
        _lastMessageSentDate = msg.date(); // TODO
        _lastMessageReceivedDate = msg.date(); // TODO
    }

    _unread += msg.isUnread();
    _starred += msg.isStarred();
    if (msg.date() > _lastMessageDate) {
        _lastMessageDate = msg.date();
    }
    if (msg.date() > _lastMessageSentDate) {
        _lastMessageSentDate = msg.date();
    }
}

string Thread::tableName() {
    return Thread::TABLE_NAME;
}

vector<string> Thread::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "unread", "starred", "subject", "lastMessageTimestamp", "lastMessageReceivedTimestamp", "lastMessageSentTimestamp", "firstMessageTimestamp"};
}

void Thread::bindToQuery(SQLite::Statement & query) {
    MailModel::bindToQuery(query);
    query.bind(":unread", _unread);
    query.bind(":starred", _starred);
    query.bind(":subject", _subject);
    query.bind(":lastMessageTimestamp", _lastMessageDate);
    query.bind(":lastMessageSentTimestamp", _lastMessageSentDate);
    query.bind(":lastMessageReceivedTimestamp", _lastMessageReceivedDate);
    query.bind(":firstMessageTimestamp", _firstMessageDate);
}


json Thread::toJSON()
{
    return MailUtils::merge(MailModel::toJSON(), {
        {"object", "thread"},
        {"unread", _unread},
        {"starred", _starred},
        {"subject", _subject},
        {"lastMessageTimestamp", _lastMessageDate},
        {"lastMessageSentTimestamp", _lastMessageSentDate},
        {"lastMessageReceivedTimestamp", _lastMessageReceivedDate},
        {"firstMessageTimestamp", _firstMessageDate},
    });
}
