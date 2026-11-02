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
class Folder;
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
    static bool setEnvUTF8(string key, string value);

    static json merge(const json &a, const json &b);
    static json contactJSONFromAddress(Address * addr);
    static Address * addressFromContactJSON(json & j);

    static string contactKeyForEmail(string email);
    static bool isAutomatedSender(string name, string email);
    static string contactNameFromHeader(string name, string email);

    static string localTimestampForTime(time_t time);
    static string formatDateTimeUTC(time_t time);

    static vector<uint32_t> uidsOfArray(Array * array);
    
    // Builds queries over MessageFolder (folderId, remoteUID) covering every UID in the set.
    static vector<Query> queriesForUIDRangesInIndexSet(string folderId, IndexSet * set);

    static string filesRootPath();
    static string directoryForFile(string root, string fileId);
    static string pathForFile(string root, File * file, bool create);

    static string namespacePrefixOrBlank(IMAPSession * session);

    static vector<string> roles();
    static string roleForFolder(string containerFolderPath, string mainPrefix, IMAPFolder * folder);
    static bool isDuplicateAllMail(Account * account, Folder * folder, bool isGmail);
    static string roleForFolderViaFlags(string mainPrefix, IMAPFolder * folder);
    static string roleForFolderViaPath(string containerFolderPath, string mainPrefix, IMAPFolder * folder);
    static string roleForMailspringFolder(string containerFolderPath, string mainPrefix, IMAPFolder * folder);
    static vector<string> rolesForGmailFolders(string containerFolderPath, string mainPrefix, Array * folders);
    static string roleForGmailLabelName(const string & xgmLabel);

    static void setBaseIDVersion(time_t identityCreationDate);

    static string idRandomlyGenerated();
    static string idForEvent(string accountId, string calendarId, string icsUID, string recurrenceId = "");
    static string idForCalendar(string accountId, string url);
    static string idForMessage(string accountId, string folderPath, IMAPMessage * msg);
    static string idForFolder(string accountId, string folderPath);
    static string idForFile(Message * message, Attachment * attachment);
    static string idForDraftHeaderMessageId(string accountId, string headerMessageId);
    static long long searchRowIdForId(const string & id);
    
    static shared_ptr<Label> labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> allLabels);

    static string qmarks(size_t count);
    static string qmarkSets(size_t count, size_t perSet);

    static XOAuth2Parts userAndTokenFromXOAuth2(string xoauth2);

    static void enableVerboseLogging();
    static bool isVerboseLoggingEnabled();
    static void configureSessionForAccount(IMAPSession & session, shared_ptr<Account> account);
    static void configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account);

    // Lowercase hex SHA-256 of a DER certificate: the form the client stores an accepted
    // certificate in, and the one mailcore compares when the certificate fails validation.
    static string certificateSHA256(Data * der);

    // Explains a connection that failed during the TLS handshake, e.g. because
    // the server offers a DH group or certificate that OpenSSL rejects. When the
    // account has not enabled "Allow insecure SSL", the advice mentions it,
    // because that is what lets the connection fall back far enough to succeed.
    //
    // Returns an empty string unless `err` says the connection itself failed, so
    // it can be appended unconditionally. That check belongs here rather than in
    // the caller: a recorded reason outlives a successful fallback by design, so
    // callers that bundle connect with a later step - SMTPSession::loginIfNeeded
    // bundles it with authentication - would otherwise blame outdated encryption
    // for a wrong password.
    //
    // A certificate rejection names the certificate the server presented, and says so when
    // it is not the one the user accepted for the account, since that failure otherwise looks
    // like any other connection error once the account is syncing.
    //
    // Pass the error from the call and the session that made it.
    static string tlsFailureAdvice(mailcore::ErrorCode err, IMAPSession & session);
    static string tlsFailureAdvice(mailcore::ErrorCode err, SMTPSession & session);
    
    static IMAPMessagesRequestKind messagesRequestKindFor(IndexSet * capabilities, bool heavyOrNeedToComputeIDs);

    // Each wakeAllWorkers() advances the generation. A worker that reads it before a pass and
    // sleeps with `sinceWake` returns at once if a wake arrived during the pass, which a bare
    // notify would have delivered to nobody.
    static uint64_t workerWakeGeneration();
    static void sleepWorkerUntilWakeOrSec(int sec);
    static void sleepWorkerUntilWakeOrSec(int sec, uint64_t sinceWake);
    static void wakeAllWorkers();

    // Consumes `v`. Slices by index: erasing consumed elements from the front is
    // quadratic on the account-sized id lists that folder removal and expunge-all produce.
    template<typename T>
    static vector<vector<T>> chunksOfVector(vector<T> & v, size_t chunkSize) {
        vector<vector<T>> results{};
        results.reserve((v.size() + chunkSize - 1) / chunkSize);

        for (size_t offset = 0; offset < v.size(); offset += chunkSize) {
            auto from = v.begin() + offset;
            auto to = (v.size() - offset > chunkSize) ? from + chunkSize : v.end();
            results.push_back(vector<T>{std::make_move_iterator(from), std::make_move_iterator(to)});
        }
        v.clear();
        return results;
    }
};

#endif /* MailUtils_hpp */
