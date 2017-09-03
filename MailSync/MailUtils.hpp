//
//  MailUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static std::string toBase58(const unsigned char * pbegin, size_t len);

    static json merge(const json &a, const json &b);
    static json contactJSONFromAddress(Address * addr);
    static Address * addressFromContactJSON(json & j);

    static string contactKeyForEmail(string email);

    static string timestampForTime(time_t time);
  
    static vector<uint32_t> uidsOfArray(Array * array);
    static vector<uint32_t> uidsOfIndexSet(IndexSet * set);

    static string pathForFile(string root, File * file, bool create);

    static string roleForFolder(IMAPFolder * folder);
    static string idRandomlyGenerated();
    static string idForMessage(string accountId, IMAPMessage * msg);
    static string idForFolder(string accountId, string folderPath);
    static string idForDraftHeaderMessageId(string headerMessageId);
    
    static shared_ptr<Label> labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> & allLabels);

    static string qmarks(size_t count);
    static string qmarkSets(size_t count, size_t perSet);

    static XOAuth2Parts userAndTokenFromXOAuth2(string xoauth2);

    static void configureSessionForAccount(IMAPSession & session, shared_ptr<Account> account);
    static void configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account);
    
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
