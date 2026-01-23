//
//  MailUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MailUtils_hpp
#define MailUtils_hpp

#include <iostream>
#include <string>
#include <stdio.h>
#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"
#include "XOAuth2TokenManager.hpp"

class File;
class Label;
class Account;
class Message;
class Query;

using namespace nlohmann;
using namespace std;
using namespace mailcore;


class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static string toBase58(const unsigned char * pbegin, size_t len);
    static string toBase64(const char * pbegin, size_t len);
    
    static string getEnvUTF8(string key);
    
    static json merge(const json &a, const json &b);
    static json contactJSONFromAddress(Address * addr);
    static Address * addressFromContactJSON(json & j);

    static string contactKeyForEmail(string email);

    static string localTimestampForTime(time_t time);
    static string formatDateTimeUTC(time_t time);

    static vector<uint32_t> uidsOfArray(Array * array);
    
    static vector<Query> queriesForUIDRangesInIndexSet(string remoteFolderId, IndexSet * set);

    static string pathForFile(string root, File * file, bool create);

    static string namespacePrefixOrBlank(IMAPSession * session);

    static vector<string> roles();
    static string roleForFolder(string containerFolderPath, string mainPrefix, IMAPFolder * folder);
    static string roleForFolderViaFlags(string mainPrefix, IMAPFolder * folder);
    static string roleForFolderViaPath(string containerFolderPath, string mainPrefix, IMAPFolder * folder);
    static int priorityForFolderRole(const string & role);

    static void setBaseIDVersion(time_t identityCreationDate);

    static string idRandomlyGenerated();
    static string idForEvent(string accountId, string calendarId, string icsUID, string recurrenceId = "");
    static string idForCalendar(string accountId, string url);
    static string idForMessage(string accountId, string folderPath, IMAPMessage * msg);
    static string idForFolder(string accountId, string folderPath);
    static string idForFile(Message * message, Attachment * attachment);
    static string idForDraftHeaderMessageId(string accountId, string headerMessageId);
    
    static shared_ptr<Label> labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> allLabels);

    static string qmarks(size_t count);
    static string qmarkSets(size_t count, size_t perSet);

    static XOAuth2Parts userAndTokenFromXOAuth2(string xoauth2);

    static void enableVerboseLogging();
    static void configureSessionForAccount(IMAPSession & session, shared_ptr<Account> account);
    static void configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account);
    
    static IMAPMessagesRequestKind messagesRequestKindFor(IndexSet * capabilities, bool heavyOrNeedToComputeIDs);

    static void sleepWorkerUntilWakeOrSec(int sec);
    static void wakeAllWorkers();

    template<typename T>
    static vector<vector<T>> chunksOfVector(vector<T> & v, size_t chunkSize) {
        vector<vector<T>> results{};
        
        while (v.size() > 0) {
            auto from = v.begin();
            auto to = v.size() > chunkSize ? from + chunkSize : v.end();
            
            results.push_back(vector<T>{std::make_move_iterator(from), std::make_move_iterator(to)});
            v.erase(from, to);
        }
        return results;
    }
};

#endif /* MailUtils_hpp */
