//
//  constants.h
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//
#include <map>
#include <libetpan/mailsmtp_types.h>

#ifndef constants_h
#define constants_h

#define AS_MCSTR(X)         mailcore::String::uniquedStringWithUTF8Characters(X.c_str())
#ifdef _MSC_VER
// On Windows with newer ICU, UChar is char16_t not wchar_t, so we need reinterpret_cast
#define AS_WIDE_MCSTR(X)    mailcore::String::stringWithCharacters(reinterpret_cast<const UChar*>(X.c_str()))
#else
#define AS_WIDE_MCSTR(X)    mailcore::String::stringWithCharacters(X.c_str())
#endif

#if defined _WIN32 || defined __CYGWIN__
static string FS_PATH_SEP = "\\";
#else
static string FS_PATH_SEP = "/";
#endif

static string MAILSPRING_FOLDER_PREFIX_V1 = "[Mailspring]";
static string MAILSPRING_FOLDER_PREFIX_V2 = "Mailspring";

static vector<string> ACCOUNT_RESET_QUERIES = {
    "DELETE FROM `ThreadCounts` WHERE `categoryId` IN (SELECT id FROM `Folder` WHERE `accountId` = ?)",
    "DELETE FROM `ThreadCounts` WHERE `categoryId` IN (SELECT id FROM `Label` WHERE `accountId` = ?)",
    "DELETE FROM `ThreadCategory` WHERE `id` IN (SELECT id FROM `Thread` WHERE `accountId` = ?)",
    "DELETE FROM `ThreadSearch` WHERE `content_id` IN (SELECT id FROM `Thread` WHERE `accountId` = ?)",
    "DELETE FROM `ThreadReference` WHERE `accountId` = ?",
    "DELETE FROM `Thread` WHERE `accountId` = ?",
    "DELETE FROM `File` WHERE `accountId` = ?",
    "DELETE FROM `Event` WHERE `accountId` = ?",
    "DELETE FROM `Label` WHERE `accountId` = ?",
    "DELETE FROM `MessageBody` WHERE `id` IN (SELECT id FROM `Message` WHERE `accountId` = ?)",
    "DELETE FROM `Message` WHERE `accountId` = ?",
    "DELETE FROM `Task` WHERE `accountId` = ?",
    "DELETE FROM `Folder` WHERE `accountId` = ?",
    "DELETE FROM `ContactSearch` WHERE `content_id` IN (SELECT id FROM `Contact` WHERE `accountId` = ?)",
    "DELETE FROM `Contact` WHERE `accountId` = ?",
    "DELETE FROM `Calendar` WHERE `accountId` = ?",
    "DELETE FROM `ModelPluginMetadata` WHERE `accountId` = ?",
    "DELETE FROM `DetatchedPluginMetadata` WHERE `accountId` = ?",
    "DELETE FROM `Account` WHERE `id` = ?",
};

static vector<string> V1_SETUP_QUERIES = {
    "CREATE TABLE IF NOT EXISTS `_State` (id VARCHAR(40) PRIMARY KEY, value TEXT)",

    "CREATE TABLE IF NOT EXISTS `File` (id VARCHAR(40) PRIMARY KEY, version INTEGER, data BLOB, accountId VARCHAR(8), filename TEXT)",
    
    "CREATE TABLE IF NOT EXISTS `Event` (id VARCHAR(40) PRIMARY KEY, data BLOB, accountId VARCHAR(8), calendarId VARCHAR(40), _start INTEGER, _end INTEGER, is_search_indexed INTEGER DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS EventIsSearchIndexedIndex ON `Event` (is_search_indexed, id)",

    "CREATE VIRTUAL TABLE IF NOT EXISTS `EventSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, title, description, location, participants)",

    "CREATE TABLE IF NOT EXISTS Label ("
      "id VARCHAR(40) PRIMARY KEY,"
      "accountId VARCHAR(8),"
      "version INTEGER,"
      "data TEXT,"
      "path VARCHAR(255),"
      "role VARCHAR(255),"
      "createdAt DATETIME,"
      "updatedAt DATETIME)",
    
    "CREATE TABLE IF NOT EXISTS Folder ("
       "id VARCHAR(40) PRIMARY KEY,"
       "accountId VARCHAR(8),"
       "version INTEGER,"
       "data TEXT,"
       "path VARCHAR(255),"
       "role VARCHAR(255),"
       "createdAt DATETIME,"
       "updatedAt DATETIME)",

    "CREATE TABLE IF NOT EXISTS Thread ("
        "id VARCHAR(42) PRIMARY KEY,"
        "accountId VARCHAR(8),"
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
    
    "CREATE INDEX IF NOT EXISTS ThreadDateIndex ON `Thread` (lastMessageReceivedTimestamp DESC)",
    "CREATE INDEX IF NOT EXISTS ThreadUnreadIndex ON `Thread` (accountId, lastMessageReceivedTimestamp DESC) WHERE unread = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadUnifiedUnreadIndex ON `Thread` (lastMessageReceivedTimestamp DESC) WHERE unread = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadStarredIndex ON `Thread` (accountId, lastMessageReceivedTimestamp DESC) WHERE starred = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadUnifiedStarredIndex ON `Thread` (lastMessageReceivedTimestamp DESC) WHERE starred = 1 AND inAllMail = 1",
    "CREATE INDEX IF NOT EXISTS ThreadGmailLookup ON `Thread` (gThrId) WHERE gThrId IS NOT NULL",
    "CREATE INDEX IF NOT EXISTS ThreadIsSearchIndexedIndex ON `Thread` (isSearchIndexed, id)",
    "CREATE INDEX IF NOT EXISTS ThreadIsSearchIndexedLastMessageReceivedIndex ON `Thread` (isSearchIndexed, lastMessageReceivedTimestamp)",

    "CREATE TABLE IF NOT EXISTS ThreadReference ("
        "threadId VARCHAR(42),"
        "accountId VARCHAR(8),"
        "headerMessageId VARCHAR(255),"
        "PRIMARY KEY (threadId, accountId, headerMessageId))",
                                                
    "CREATE TABLE IF NOT EXISTS ThreadCategory ("
        "id VARCHAR(40),"
        "value VARCHAR(40),"
        "inAllMail TINYINT(1),"
        "unread TINYINT(1),"
        "lastMessageReceivedTimestamp DATETIME,"
        "lastMessageSentTimestamp DATETIME,"
        "PRIMARY KEY (id, value))",
    
    "CREATE INDEX IF NOT EXISTS `ThreadCategory_id` ON `ThreadCategory` (`id` ASC)",
    "CREATE UNIQUE INDEX IF NOT EXISTS `ThreadCategory_val_id` ON `ThreadCategory` (`value` ASC, `id` ASC)",
    "CREATE INDEX IF NOT EXISTS ThreadListCategoryIndex ON `ThreadCategory` (lastMessageReceivedTimestamp DESC, value, inAllMail, unread, id)",
    "CREATE INDEX IF NOT EXISTS ThreadListCategorySentIndex ON `ThreadCategory` (lastMessageSentTimestamp DESC, value, inAllMail, unread, id)",

    "CREATE TABLE IF NOT EXISTS `ThreadCounts` (`categoryId` TEXT PRIMARY KEY, `unread` INTEGER, `total` INTEGER)",
    
    "CREATE VIRTUAL TABLE IF NOT EXISTS `ThreadSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, subject, to_, from_, categories, body)",

    "CREATE TABLE IF NOT EXISTS `Account` (id VARCHAR(40) PRIMARY KEY, data BLOB, accountId VARCHAR(8), email_address TEXT)",

    "CREATE TABLE IF NOT EXISTS Message ("
        "id VARCHAR(40) PRIMARY KEY,"
        "accountId VARCHAR(8),"
        "version INTEGER,"
        "data TEXT,"
        "headerMessageId VARCHAR(255),"
        "gMsgId VARCHAR(255),"
        "gThrId VARCHAR(255),"
        "subject VARCHAR(500),"
        "date DATETIME,"
        "draft TINYINT(1),"
        "unread TINYINT(1),"
        "starred TINYINT(1),"
        "remoteUID INTEGER,"
        "remoteXGMLabels TEXT,"
        "remoteFolderId VARCHAR(40),"
        "replyToHeaderMessageId VARCHAR(255),"
        "threadId VARCHAR(40))",
    
    "CREATE INDEX IF NOT EXISTS MessageListThreadIndex ON Message(threadId, date ASC)",
    "CREATE INDEX IF NOT EXISTS MessageListHeaderMsgIdIndex ON Message(headerMessageId)",
    "CREATE INDEX IF NOT EXISTS MessageListDraftIndex ON Message(accountId, date DESC) WHERE draft = 1",
    "CREATE INDEX IF NOT EXISTS MessageListUnifiedDraftIndex ON Message(date DESC) WHERE draft = 1",
    
    "CREATE TABLE IF NOT EXISTS `ModelPluginMetadata` (id VARCHAR(40), `accountId` VARCHAR(8), `objectType` VARCHAR(15), `value` TEXT, `expiration` DATETIME, PRIMARY KEY (`value`, `id`))",
    "CREATE INDEX IF NOT EXISTS `ModelPluginMetadata_id` ON `ModelPluginMetadata` (`id` ASC)",
    "CREATE INDEX IF NOT EXISTS `ModelPluginMetadata_expiration` ON `ModelPluginMetadata` (`expiration` ASC) WHERE expiration IS NOT NULL",

    "CREATE TABLE IF NOT EXISTS `DetatchedPluginMetadata` (objectId VARCHAR(40), objectType VARCHAR(15), accountId VARCHAR(8), pluginId VARCHAR(40), value BLOB, version INTEGER, PRIMARY KEY (`objectId`, `accountId`, `pluginId`))",

    "CREATE TABLE IF NOT EXISTS `MessageBody` (id VARCHAR(40) PRIMARY KEY, `value` TEXT)",
    "CREATE UNIQUE INDEX IF NOT EXISTS MessageBodyIndex ON MessageBody(id)",
    
    "CREATE TABLE IF NOT EXISTS `Contact` (id VARCHAR(40) PRIMARY KEY, data BLOB, accountId VARCHAR(8), email TEXT, version INTEGER, refs INTEGER DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS ContactEmailIndex ON Contact(email)",
    "CREATE INDEX IF NOT EXISTS ContactAccountEmailIndex ON Contact(accountId, email)",

    "CREATE VIRTUAL TABLE IF NOT EXISTS `ContactSearch` USING fts5(tokenize = 'porter unicode61', content_id UNINDEXED, content)",

    "CREATE TABLE IF NOT EXISTS `Calendar` (id VARCHAR(40) PRIMARY KEY, data BLOB, accountId VARCHAR(8))",
    
    "CREATE TABLE IF NOT EXISTS `Task` (id VARCHAR(40) PRIMARY KEY, version INTEGER, data BLOB, accountId VARCHAR(8), status VARCHAR(255))",
};

static vector<string> V2_SETUP_QUERIES = {
    "CREATE INDEX IF NOT EXISTS MessageUIDScanIndex ON Message(accountId, remoteFolderId, remoteUID)",
};

static vector<string> V3_SETUP_QUERIES = {
    "ALTER TABLE `MessageBody` ADD COLUMN fetchedAt DATETIME",
    "UPDATE `MessageBody` SET fetchedAt = datetime('now')",
};

static vector<string> V4_SETUP_QUERIES = {
    "DELETE FROM Task WHERE Task.status = \"complete\" OR Task.status = \"cancelled\"",
    "CREATE INDEX IF NOT EXISTS TaskByStatus ON Task(accountId, status)",
};

static vector<string> V6_SETUP_QUERIES = {
    "DROP TABLE IF EXISTS `Event`",
    "CREATE TABLE IF NOT EXISTS `Event` (id VARCHAR(40) PRIMARY KEY, data BLOB, accountId VARCHAR(8), etag VARCHAR(40), calendarId VARCHAR(40), recurrenceStart INTEGER, recurrenceEnd INTEGER)",
    "CREATE INDEX IF NOT EXISTS EventETag ON Event(calendarId, etag)",
};

static vector<string> V7_SETUP_QUERIES = {
    "ALTER TABLE `Event` ADD COLUMN icsuid VARCHAR(150)",
    "CREATE INDEX IF NOT EXISTS EventUID ON Event(accountId, icsuid)",
};

static vector<string> V8_SETUP_QUERIES = {
    "DELETE FROM Contact WHERE refs = 0;",
    "ALTER TABLE `Contact` ADD COLUMN hidden TINYINT(1) DEFAULT 0",
    "ALTER TABLE `Contact` ADD COLUMN source VARCHAR(10) DEFAULT 'mail'",
    "ALTER TABLE `Contact` ADD COLUMN bookId VARCHAR(40)",
    "ALTER TABLE `Contact` ADD COLUMN etag VARCHAR(40)",
    "CREATE INDEX IF NOT EXISTS ContactBrowseIndex ON Contact(hidden,refs,accountId)",
    "CREATE TABLE `ContactGroup` (`id` varchar(40),`accountId` varchar(40),`bookId` varchar(40), `data` BLOB, `version` INTEGER, `name` varchar(300), PRIMARY KEY (id))",
    "CREATE TABLE `ContactContactGroup` (`id` varchar(40),`value` varchar(40), PRIMARY KEY (id, value));",
    "CREATE TABLE `ContactBook` (`id` varchar(40),`accountId` varchar(40), `data` BLOB, `version` INTEGER, PRIMARY KEY (id));",
};

// V9: Add recurrenceId column for recurring event exception support
static vector<string> V9_SETUP_QUERIES = {
    "ALTER TABLE `Event` ADD COLUMN recurrenceId VARCHAR(50) DEFAULT ''",
    "CREATE INDEX IF NOT EXISTS EventRecurrenceId ON Event(calendarId, icsuid, recurrenceId)",
};

static map<string, string> COMMON_FOLDER_NAMES = {
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
    {"trash", "trash"},
    {"удаленные", "trash"},
    {"kosz", "trash"},
    {"yдалённые", "trash"},
    
    {"roskaposti", "spam"},
    {"skr\xc3\xa4ppost", "spam"},
    {"spamverdacht", "spam"},
    {"spam", "spam"},
    {"[gmail]/spam", "spam"},
    {"[imap]/spam", "spam"},
    {"\xe5\x9e\x83\xe5\x9c\xbe\xe9\x82\xae\xe4\xbb\xb6", "spam"},
    {"junk", "spam"},
    {"junk mail", "spam"},
    {"junk e-mail", "spam"},
    {"junk email", "spam"},
    {"bulk mail", "spam"},
    {"спам", "spam"},

    {"inbox", "inbox"},
    
    {"dateneintrag", "archive"},
    {"archivio", "archive"},
    {"archive", "archive"},
    
    {"postausgang", "sent"},
    {"sent", "sent"},
    {"[gmail]/sent mail", "sent"},
    {"\xeb\xb3\xb4\xeb\x82\xbc\xed\x8e\xb8\xec\xa7\x80\xed\x95\xa8", "sent"},
    {"elementos enviados", "sent"},
    {"sent items", "sent"},
    {"sent messages", "sent"},
    {"odeslan\xc3\xa9", "sent"},
    {"sent-mail", "sent"},
    {"ko\xc5\xa1", "sent"},
    {"sentmail", "sent"},
    {"papierkorb", "sent"},
    {"gesendet", "sent"},
    {"ko\xc5\xa1/sent items", "sent"},
    {"gesendete elemente", "sent"},
    {"отправленные", "sent"},
    {"sentbox", "sent"},
    {"wys&AUI-ane", "sent"},
    
    {"drafts", "drafts"},
    {"draft", "drafts"},
    {"brouillons", "drafts"},
    {"черновики", "drafts"},
    {"draftbox", "drafts"},
    {"robocze", "drafts"},

    {"Mailspring/Snoozed", "snoozed"},
    {"Mailspring.Snoozed", "snoozed"},
};

static map<int, string> LibEtPanCodeToTypeMap = {
    {MAILSMTP_NO_ERROR, "MAILSMTP_NO_ERROR"},
    {MAILSMTP_ERROR_UNEXPECTED_CODE, "MAILSMTP_ERROR_UNEXPECTED_CODE"},
    {MAILSMTP_ERROR_SERVICE_NOT_AVAILABLE, "MAILSMTP_ERROR_SERVICE_NOT_AVAILABLE"},
    {MAILSMTP_ERROR_STREAM, "MAILSMTP_ERROR_STREAM"},
    {MAILSMTP_ERROR_HOSTNAME, "MAILSMTP_ERROR_HOSTNAME"},
    {MAILSMTP_ERROR_NOT_IMPLEMENTED, "MAILSMTP_ERROR_NOT_IMPLEMENTED"},
    {MAILSMTP_ERROR_ACTION_NOT_TAKEN, "MAILSMTP_ERROR_ACTION_NOT_TAKEN"},
    {MAILSMTP_ERROR_EXCEED_STORAGE_ALLOCATION, "MAILSMTP_ERROR_EXCEED_STORAGE_ALLOCATION"},
    {MAILSMTP_ERROR_IN_PROCESSING, "MAILSMTP_ERROR_IN_PROCESSING"},
    {MAILSMTP_ERROR_INSUFFICIENT_SYSTEM_STORAGE, "MAILSMTP_ERROR_INSUFFICIENT_SYSTEM_STORAGE"},
    {MAILSMTP_ERROR_MAILBOX_UNAVAILABLE, "MAILSMTP_ERROR_MAILBOX_UNAVAILABLE"},
    {MAILSMTP_ERROR_MAILBOX_NAME_NOT_ALLOWED, "MAILSMTP_ERROR_MAILBOX_NAME_NOT_ALLOWED"},
    {MAILSMTP_ERROR_BAD_SEQUENCE_OF_COMMAND, "MAILSMTP_ERROR_BAD_SEQUENCE_OF_COMMAND"},
    {MAILSMTP_ERROR_USER_NOT_LOCAL, "MAILSMTP_ERROR_USER_NOT_LOCAL"},
    {MAILSMTP_ERROR_TRANSACTION_FAILED, "MAILSMTP_ERROR_TRANSACTION_FAILED"},
    {MAILSMTP_ERROR_MEMORY, "MAILSMTP_ERROR_MEMORY"},
    {MAILSMTP_ERROR_AUTH_NOT_SUPPORTED, "MAILSMTP_ERROR_AUTH_NOT_SUPPORTED"},
    {MAILSMTP_ERROR_AUTH_LOGIN, "MAILSMTP_ERROR_AUTH_LOGIN"},
    {MAILSMTP_ERROR_AUTH_REQUIRED, "MAILSMTP_ERROR_AUTH_REQUIRED"},
    {MAILSMTP_ERROR_AUTH_TOO_WEAK, "MAILSMTP_ERROR_AUTH_TOO_WEAK"},
    {MAILSMTP_ERROR_AUTH_TRANSITION_NEEDED, "MAILSMTP_ERROR_AUTH_TRANSITION_NEEDED"},
    {MAILSMTP_ERROR_AUTH_TEMPORARY_FAILTURE, "MAILSMTP_ERROR_AUTH_TEMPORARY_FAILTURE"},
    {MAILSMTP_ERROR_AUTH_ENCRYPTION_REQUIRED, "MAILSMTP_ERROR_AUTH_ENCRYPTION_REQUIRED"},
    {MAILSMTP_ERROR_STARTTLS_TEMPORARY_FAILURE, "MAILSMTP_ERROR_STARTTLS_TEMPORARY_FAILURE"},
    {MAILSMTP_ERROR_STARTTLS_NOT_SUPPORTED, "MAILSMTP_ERROR_STARTTLS_NOT_SUPPORTED"},
    {MAILSMTP_ERROR_CONNECTION_REFUSED, "MAILSMTP_ERROR_CONNECTION_REFUSED"},
    {MAILSMTP_ERROR_AUTH_AUTHENTICATION_FAILED, "MAILSMTP_ERROR_AUTH_AUTHENTICATION_FAILED"},
    {MAILSMTP_ERROR_SSL, "MAILSMTP_ERROR_SSL"},
};

static map<ErrorCode, string> ErrorCodeToTypeMap = {
    {ErrorNone, "ErrorNone"}, // 0
    {ErrorRename, "ErrorRename"},
    {ErrorDelete, "ErrorDelete"},
    {ErrorCreate, "ErrorCreate"},
    {ErrorSubscribe, "ErrorSubscribe"},
    {ErrorAppend, "ErrorAppend"},
    {ErrorCopy, "ErrorCopy"},
    {ErrorExpunge, "ErrorExpunge"},
    {ErrorFetch, "ErrorFetch"},
    {ErrorIdle, "ErrorIdle"}, // 20
    {ErrorIdentity, "ErrorIdentity"},
    {ErrorNamespace, "ErrorNamespace"},
    {ErrorStore, "ErrorStore"},
    {ErrorCapability, "ErrorCapability"},
    {ErrorSendMessageIllegalAttachment, "ErrorSendMessageIllegalAttachment"},
    {ErrorStorageLimit, "ErrorStorageLimit"},
    {ErrorSendMessageNotAllowed, "ErrorSendMessageNotAllowed"},
    {ErrorSendMessage, "ErrorSendMessage"}, // 30
    {ErrorFetchMessageList, "ErrorFetchMessageList"},
    {ErrorDeleteMessage, "ErrorDeleteMessage"},
    {ErrorFile, "ErrorFile"},
    {ErrorCompression, "ErrorCompression"},
    {ErrorNoSender, "ErrorNoSender"},
    {ErrorNoRecipient, "ErrorNoRecipient"},
    {ErrorNoop, "ErrorNoop"},
    {ErrorServerDate, "ErrorServerDate"},
    {ErrorCustomCommand, "ErrorCustomCommand"},
    {ErrorYahooSendMessageSpamSuspected, "ErrorYahooSendMessageSpamSuspected"},
    {ErrorYahooSendMessageDailyLimitExceeded, "ErrorYahooSendMessageDailyLimitExceeded"},
    {ErrorOutlookLoginViaWebBrowser, "ErrorOutlookLoginViaWebBrowser"},
    {ErrorTiscaliSimplePassword, "ErrorTiscaliSimplePassword"},
    {ErrorConnection, "ErrorConnection"},
    {ErrorInvalidAccount, "ErrorInvalidAccount"},
    {ErrorInvalidRelaySMTP, "ErrorInvalidRelaySMTP"},
    {ErrorNoImplementedAuthMethods, "ErrorNoImplementedAuthMethods"},
    {ErrorTLSNotAvailable, "ErrorTLSNotAvailable"},
    {ErrorParse, "ErrorParse"},
    {ErrorCertificate, "ErrorCertificate"},
    {ErrorAuthentication, "ErrorAuthentication"},
    {ErrorGmailIMAPNotEnabled, "ErrorGmailIMAPNotEnabled"},
    {ErrorGmailExceededBandwidthLimit, "ErrorGmailExceededBandwidthLimit"},
    {ErrorGmailTooManySimultaneousConnections, "ErrorGmailTooManySimultaneousConnections"},
    {ErrorMobileMeMoved, "ErrorMobileMeMoved"},
    {ErrorYahooUnavailable, "ErrorYahooUnavailable"},
    {ErrorNonExistantFolder, "ErrorNonExistantFolder"},
    {ErrorStartTLSNotAvailable, "ErrorStartTLSNotAvailable"},
    {ErrorGmailApplicationSpecificPasswordRequired, "ErrorGmailApplicationSpecificPasswordRequired"},
    {ErrorOutlookLoginViaWebBrowser, "ErrorOutlookLoginViaWebBrowser"},
    {ErrorNeedsConnectToWebmail, "ErrorNeedsConnectToWebmail"},
    {ErrorNoValidServerFound, "ErrorNoValidServerFound"},
    {ErrorAuthenticationRequired, "ErrorAuthenticationRequired"},
};

#endif /* constants_h */
