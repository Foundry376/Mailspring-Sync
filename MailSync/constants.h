//
//  constants.h
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
#include <map>

#ifndef constants_h
#define constants_h


// TODO add unique keys

static std::vector<std::string> SETUP_QUERIES = {
    "CREATE TABLE IF NOT EXISTS `File` (id TEXT PRIMARY KEY, version INTEGER, data BLOB, accountId TEXT, filename TEXT)",
    
    "CREATE TABLE IF NOT EXISTS `Event` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT, calendar_id TEXT, _start INTEGER, _end INTEGER, is_search_indexed INTEGER DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS EventIsSearchIndexedIndex ON `Event` (is_search_indexed, id)",

    "CREATE VIRTUAL TABLE IF NOT EXISTS `EventSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, title, description, location, participants)",

    "CREATE TABLE IF NOT EXISTS Label ("
      "id VARCHAR(65) PRIMARY KEY,"
      "accountId VARCHAR(65),"
      "version INTEGER,"
      "data TEXT,"
      "path VARCHAR(255),"
      "role VARCHAR(255),"
      "createdAt DATETIME,"
      "updatedAt DATETIME)",
    
    "CREATE TABLE IF NOT EXISTS Folder ("
       "id VARCHAR(65) PRIMARY KEY,"
       "accountId VARCHAR(65),"
       "version INTEGER,"
       "data TEXT,"
       "path VARCHAR(255),"
       "role VARCHAR(255),"
       "createdAt DATETIME,"
       "updatedAt DATETIME)",

    "CREATE TABLE IF NOT EXISTS Thread ("
        "id VARCHAR(65) PRIMARY KEY,"
        "accountId VARCHAR(65),"
        "version INTEGER,"
        "data TEXT,"
        "gThrId VARCHAR(20),"
        "subject VARCHAR(500),"
        "snippet VARCHAR(255),"
        "unread INTEGER,"
        "starred INTEGER,"
        "firstMessageTimestamp DATETIME,"
        "lastMessageTimestamp DATETIME,"
        "lastMessageReceivedTimestamp DATETIME,"
        "lastMessageSentTimestamp DATETIME,"
        "inAllMail TINYINT(1),"
        "isSearchIndexed TINYINT(1),"
        "participants TEXT,"
        "hasAttachments INTEGER)",
    
    "CREATE TABLE IF NOT EXISTS ThreadReference ("
        "threadId VARCHAR(65),"
        "headerMessageId VARCHAR(255),"
        "PRIMARY KEY (threadId, headerMessageId))",
                                                
    "CREATE TABLE IF NOT EXISTS `ThreadPluginMetadata` (id TEXT KEY, `value` TEXT)",
    "CREATE INDEX IF NOT EXISTS `ThreadPluginMetadata_id` ON `ThreadPluginMetadata` (`id` ASC)",
    "CREATE UNIQUE INDEX IF NOT EXISTS `ThreadPluginMetadata_val_id` ON `ThreadPluginMetadata` (`value` ASC, `id` ASC)",

    "CREATE TABLE IF NOT EXISTS ThreadCategory ("
        "id VARCHAR(65),"
        "value VARCHAR(65),"
        "inAllMail TINYINT(1),"
        "unread TINYINT(1),"
        "lastMessageReceivedTimestamp DATETIME,"
        "lastMessageSentTimestamp DATETIME,"
        "categoryId VARCHAR(65),"
        "PRIMARY KEY (id, value))",
    
    "CREATE INDEX IF NOT EXISTS `ThreadCategory_id` ON `ThreadCategory` (`id` ASC)",
    "CREATE UNIQUE INDEX IF NOT EXISTS `ThreadCategory_val_id` ON `ThreadCategory` (`value` ASC, `id` ASC)",
    "CREATE INDEX IF NOT EXISTS ThreadListCategoryIndex ON `ThreadCategory` (lastMessageReceivedTimestamp DESC, value, inAllMail, unread, id)",
    "CREATE INDEX IF NOT EXISTS ThreadListCategorySentIndex ON `ThreadCategory` (lastMessageSentTimestamp DESC, value, inAllMail, unread, id)",

    "CREATE TABLE IF NOT EXISTS `ThreadContact` (id TEXT KEY, `value` TEXT, lastMessageReceivedTimestamp INTEGER, PRIMARY KEY (value, id))",
    "CREATE INDEX IF NOT EXISTS ThreadContactDateIndex ON `ThreadContact` (lastMessageReceivedTimestamp DESC, value, id)",

    "CREATE TABLE IF NOT EXISTS `ThreadCounts` (`categoryId` TEXT PRIMARY KEY, `unread` INTEGER, `total` INTEGER)",
    "CREATE INDEX IF NOT EXISTS ThreadDateIndex ON `Thread` (lastMessageReceivedTimestamp DESC)",
    "CREATE INDEX IF NOT EXISTS ThreadUnreadIndex ON `Thread` (accountId, lastMessageReceivedTimestamp DESC) WHERE unread = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadUnifiedUnreadIndex ON `Thread` (lastMessageReceivedTimestamp DESC) WHERE unread = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadStarredIndex ON `Thread` (accountId, lastMessageReceivedTimestamp DESC) WHERE starred = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadUnifiedStarredIndex ON `Thread` (lastMessageReceivedTimestamp DESC) WHERE starred = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadIsSearchIndexedIndex ON `Thread` (isSearchIndexed, id)",
    "CREATE INDEX IF NOT EXISTS ThreadIsSearchIndexedLastMessageReceivedIndex ON `Thread` (isSearchIndexed, lastMessageReceivedTimestamp)",
    
    "CREATE VIRTUAL TABLE IF NOT EXISTS `ThreadSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, subject, to_, from_, categories, body)",

    "CREATE TABLE IF NOT EXISTS `Account` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT, email_address TEXT)",
    "CREATE TABLE IF NOT EXISTS `AccountPluginMetadata` (id TEXT, `value` TEXT, PRIMARY KEY (value, id))",
    "CREATE INDEX IF NOT EXISTS `AccountPluginMetadata_id` ON `AccountPluginMetadata` (`id` ASC)",

    "CREATE TABLE IF NOT EXISTS Message ("
        "id VARCHAR(65) PRIMARY KEY,"
        "accountId VARCHAR(65),"
        "version INTEGER,"
        "data TEXT,"
        "headerMessageId VARCHAR(255),"
        "gMsgId VARCHAR(255),"
        "gThrId VARCHAR(255),"
        "subject VARCHAR(500),"
        "date DATETIME,"
        "draft TINYINT(1),"
        "isSent TINYINT(1),"
        "unread TINYINT(1),"
        "starred TINYINT(1),"
        "replyToMessageId VARCHAR(255),"
        "folderImapUID INTEGER,"
        "folderImapXGMLabels TEXT,"
        "folderId VARCHAR(65),"
        "threadId VARCHAR(65))",
    
    "CREATE INDEX IF NOT EXISTS MessageListThreadIndex ON Message(threadId, date ASC)",
    "CREATE INDEX IF NOT EXISTS MessageListDraftIndex ON Message(accountId, date DESC) WHERE draft = 1",
    "CREATE INDEX IF NOT EXISTS MessageListUnifiedDraftIndex ON Message(date DESC) WHERE draft = 1",
    
    "CREATE TABLE IF NOT EXISTS `MessagePluginMetadata` (id TEXT KEY, `value` TEXT, PRIMARY KEY (`value`, `id`))",
    
    "CREATE TABLE IF NOT EXISTS `MessageBody` (id TEXT PRIMARY KEY, `value` TEXT)",
    "CREATE UNIQUE INDEX IF NOT EXISTS MessageBodyIndex ON MessageBody(id)",
    
    "CREATE TABLE IF NOT EXISTS `Contact` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT, name TEXT, email TEXT, version INTEGER, refs INTEGER DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS ContactEmailIndex ON Contact(email)",
    "CREATE INDEX IF NOT EXISTS ContactAccountEmailIndex ON Contact(accountId, email)",

    "CREATE VIRTUAL TABLE IF NOT EXISTS `ContactSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, content)",

    "CREATE TABLE IF NOT EXISTS `Category` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT, role TEXT, path TEXT)",
    
    "CREATE TABLE IF NOT EXISTS `Calendar` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT)",
    
    "CREATE TABLE IF NOT EXISTS `JSONBlob` (id TEXT PRIMARY KEY, data BLOB)",
    
    "CREATE TABLE IF NOT EXISTS `ProviderSyncbackRequest` (id TEXT PRIMARY KEY, data BLOB, accountId TEXT, type TEXT)",
    
    "CREATE TABLE IF NOT EXISTS `Task` (id TEXT PRIMARY KEY, version INTEGER, data BLOB, accountId TEXT, status VARCHAR(255))",
};


static std::map<std::string, std::string> COMMON_FOLDER_NAMES = {
    {"gel\xc3\xb6scht", "trash"},
    {"papierkorb", "trash"},
    {"\xd0\x9a\xd0\xbe\xd1\x80\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0", "trash"},
    {"[imap]/trash", "trash"},
    {"papelera", "trash"},
    {"borradores", "trash"},
    {"[imap]/\xd0\x9a\xd0\xbe\xd1\x80", "trash"},
    {"\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0", "trash"},
    {"deleted items", "trash"},
    {"\xd0\xa1\xd0\xbc\xd1\x96\xd1\x82\xd1\x82\xd1\x8f", "trash"},
    {"papierkorb/trash", "trash"},
    {"gel\xc3\xb6schte elemente", "trash"},
    {"deleted messages", "trash"},
    {"[gmail]/trash", "trash"},
    {"inbox/trash", "trash"},
    {"trash", "trash"},
    {"mail/trash", "trash"},
    {"inbox.trash", "trash"},
    
    {"roskaposti", "spam"},
    {"inbox.spam", "spam"},
    {"inbox.spam", "spam"},
    {"skr\xc3\xa4ppost", "spam"},
    {"spamverdacht", "spam"},
    {"spam", "spam"},
    {"spam", "spam"},
    {"[gmail]/spam", "spam"},
    {"[imap]/spam", "spam"},
    {"\xe5\x9e\x83\xe5\x9c\xbe\xe9\x82\xae\xe4\xbb\xb6", "spam"},
    {"junk", "spam"},
    {"junk mail", "spam"},
    {"junk e-mail", "spam"},
    
    {"inbox", "inbox"},
    
    {"archive", "archive"},
    
    {"postausgang", "sent"},
    {"inbox.gesendet", "sent"},
    {"[gmail]/sent mail", "sent"},
    {"\xeb\xb3\xb4\xeb\x82\xbc\xed\x8e\xb8\xec\xa7\x80\xed\x95\xa8", "sent"},
    {"elementos enviados", "sent"},
    {"sent", "sent"},
    {"sent items", "sent"},
    {"sent messages", "sent"},
    {"inbox.papierkorb", "sent"},
    {"odeslan\xc3\xa9", "sent"},
    {"mail/sent-mail", "sent"},
    {"ko\xc5\xa1", "sent"},
    {"inbox.sentmail", "sent"},
    {"gesendet", "sent"},
    {"ko\xc5\xa1/sent items", "sent"},
    {"gesendete elemente", "sent"},

    {"drafts", "drafts"},
    {"draft", "drafts"},
    {"brouillons", "drafts"},

};

#endif /* constants_h */
