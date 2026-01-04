//
//  MailUtils.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailUtils.hpp"
#include "MetadataExpirationWorker.hpp"
#include "SyncException.hpp"
#include "sha256.h"
#include "constants.h"
#include "File.hpp"
#include "Label.hpp"
#include "Account.hpp"
#include "Query.hpp"

#if defined(_MSC_VER)
#include <direct.h>
#include <codecvt>
#include <locale>
#endif

using namespace std;
using namespace mailcore;
using namespace nlohmann;

static vector<string> unworthyPrefixes = {
    "noreply",
    "no-reply",
    "no_reply",
    "auto-confirm",
    "donotreply",
    "do-not-reply",
    "do_not_reply",
    "auto-reply",
    "inmail-hit-reply",
    "updates@",
    "mailman-owner",
    "email_notifier",
    "announcement",
    "bounce",
    "notification",
    "notify@",
    "support",
    "alert",
    "news",
    "info",
    "automated",
    "list",
    "distribute",
    "catchall",
    "catch-all"
};

static bool calledsrand = false;

bool create_directory(string dir) {
    int c = 0;
#if defined(_WIN32)
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    wstring dirWide = convert.from_bytes(dir);
    c = _wmkdir(dirWide.c_str());
#else
    c = mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
#endif
    return true;
}

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static const char* pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";


// From https://github.com/bitcoin/bitcoin/blob/master/src/base58.cpp
std::string MailUtils::toBase58(const unsigned char * pbegin, size_t len)
{
    const unsigned char * pend = pbegin + len;

    // Skip & count leading zeroes.
    int zeroes = 0;
    int length = 0;
    while (pbegin != pend && *pbegin == 0) {
        pbegin++;
        zeroes++;
    }
    // Allocate enough space in big-endian base58 representation.
    long size = (pend - pbegin) * 138 / 100 + 1; // log(256) / log(58), rounded up.
    std::vector<unsigned char> b58(size);
    // Process the bytes.
    while (pbegin != pend) {
        int carry = *pbegin;
        int i = 0;
        // Apply "b58 = b58 * 256 + ch".
        for (std::vector<unsigned char>::reverse_iterator it = b58.rbegin(); (carry != 0 || i < length) && (it != b58.rend()); it++, i++) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        
        assert(carry == 0);
        length = i;
        pbegin++;
    }
    // Skip leading zeroes in base58 result.
    std::vector<unsigned char>::iterator it = b58.begin() + (size - length);
    while (it != b58.end() && *it == 0)
        it++;
    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58.end() - it));
    str.assign(zeroes, '1');
    while (it != b58.end())
        str += pszBase58[*(it++)];
    return str;
}

static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string MailUtils::toBase64(const char * pbegin, size_t in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(pbegin++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for(i = 0; (i <4) ; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i)
  {
    for(j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = ( char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];

    while((i++ < 3))
      ret += '=';

  }

  return ret;

}


string MailUtils::getEnvUTF8(string key) {
#if defined(_MSC_VER)
    size_t wKeyLength = MultiByteToWideChar( CP_UTF8, 0, key.c_str(), (int)key.length(), 0, 0 );
    std::wstring wKey( wKeyLength, L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, key.c_str(), (int)key.length(), &wKey[0], (int)wKey.length());
    
    wchar_t wstr[MAX_PATH];
    size_t len = _countof(wstr);
    _wgetenv_s(&len, wstr, len, wKey.c_str());
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    string out = convert.to_bytes(wstr);
    return out;
#else
    char * val = getenv(key.c_str());
    if (val == nullptr) {
        return "";
    }
    return string(val);
#endif
}

json MailUtils::merge(const json &a, const json &b)
{
    json result = a.flatten();
    json tmp = b.flatten();
    
    for (json::iterator it = tmp.begin(); it != tmp.end(); ++it)
    {
        result[it.key()] = it.value();
    }
    
    return result.unflatten();
}

json MailUtils::contactJSONFromAddress(Address * addr) {
    json contact;
    // note: for some reason, using ternarys here doesn't work.
    if (addr->displayName()) {
        contact["name"] = addr->displayName()->UTF8Characters();
    }
    if (addr->mailbox()) {
        contact["email"] = addr->mailbox()->UTF8Characters();
    }
    return contact;
}

Address * MailUtils::addressFromContactJSON(json & j) {
    if (j["name"].is_string()) {
        return Address::addressWithDisplayName(AS_MCSTR(j["name"].get<string>()), AS_MCSTR(j["email"].get<string>()));
    }
    return Address::addressWithMailbox(AS_MCSTR(j["email"].get<string>()));
}

string MailUtils::contactKeyForEmail(string email) {
    
    // lowercase the email
    transform(email.begin(), email.end(), email.begin(), ::tolower);

    // check for anything longer then X - likely autogenerated and not a contact
    if (email.length() > 40) {
        return "";
    }
    // fast check for common prefixes we don't want
    for (string & prefix : unworthyPrefixes) {
        if (equal(email.begin(), email.begin() + min(email.size(), prefix.size()), prefix.begin())) {
            return "";
        }
    }
    
    // check for non-prefix scenarios
    if (email.find("@noreply") != string::npos) { // x@noreply.github.com
        return "";
    }
    if (email.find("@notifications") != string::npos) { // x@notifications.intuit.com
        return "";
    }
    if (email.find("noreply@") != string::npos) { // reservations-noreply@bla.com
        return "";
    }
    
    return email;
}

int MailUtils::compareEmails(void * a, void * b, void * context) {
    return ((String*)a)->compare((String*)b);
}

string MailUtils::localTimestampForTime(time_t time) {
    // Some messages can have date=-1 if no Date: header is present. Win32
    // doesn't allow this value, so we always convert it to one second past 1970.
    if (time == -1) {
        time = 1;
    }

    char buffer[32];
#if defined(_MSC_VER)
    tm ptm;
    localtime_s(&ptm, &time);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", &ptm);
#else
    tm * ptm = localtime(&time);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", ptm);
#endif
    return string(buffer);
}

string MailUtils::formatDateTimeUTC(time_t time) {
    // Format time as iCalendar UTC date-time for CalDAV time-range filters
    // Format: YYYYMMDDTHHMMSSZ (per RFC 4791 section 9.9)
    char buffer[20];
#if defined(_MSC_VER)
    tm ptm;
    gmtime_s(&ptm, &time);
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &ptm);
#else
    tm * ptm = gmtime(&time);
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", ptm);
#endif
    return string(buffer);
}

string MailUtils::namespacePrefixOrBlank(IMAPSession * session) {
    if (!session->defaultNamespace()->mainPrefix()) {
        return "";
    }
    return session->defaultNamespace()->mainPrefix()->UTF8Characters();
}

vector<string> MailUtils::roles() {
    return {"all", "sent", "drafts", "spam", "important", "starred", "archive", "inbox", "trash", "snoozed"};
}

string MailUtils::roleForFolder(string containerFolderPath, string mainPrefix, IMAPFolder * folder) {
    string role = MailUtils::roleForFolderViaFlags(mainPrefix, folder);
    if (role == "") {
        role = MailUtils::roleForFolderViaPath(containerFolderPath, mainPrefix, folder);
    }
    return role;
}

string MailUtils::roleForFolderViaFlags(string mainPrefix, IMAPFolder * folder) {
    IMAPFolderFlag flags = folder->flags();
    
    if (flags & IMAPFolderFlagAll) {
        return "all";
    }
    if (flags & IMAPFolderFlagSentMail) {
        return "sent";
    }
    if (flags & IMAPFolderFlagDrafts) {
        return "drafts";
    }
    if (flags & IMAPFolderFlagJunk) {
        return "spam";
    }
    if (flags & IMAPFolderFlagSpam) {
        return "spam";
    }
    if (flags & IMAPFolderFlagImportant) {
        return "important";
    }
    if (flags & IMAPFolderFlagStarred) {
        return "starred";
    }
    if (flags & IMAPFolderFlagInbox) {
        return "inbox";
    }
    if (flags & IMAPFolderFlagTrash) {
        return "trash";
    }
    return "";
}

string MailUtils::roleForFolderViaPath(string containerFolderPath, string mainPrefix, IMAPFolder * folder) {
    string delimiter {folder->delimiter()};
    string path = string(folder->path()->UTF8Characters());

    // Strip the namespace prefix if it's present
    if ((mainPrefix.size() > 0) && (path.size() > mainPrefix.size()) && (path.substr(0, mainPrefix.size()) == mainPrefix)) {
        path = path.substr(mainPrefix.size());
    }
    
    // Strip the delimiter if the delimiter is the first character after stripping prefix
    if (path.size() > 1 && path.substr(0, 1) == delimiter) {
        path = path.substr(1);
    }

    // Lowercase the path
    transform(path.begin(), path.end(), path.begin(), ::tolower);
    transform(containerFolderPath.begin(), containerFolderPath.end(), containerFolderPath.begin(), ::tolower);

    // In our [Mailspring] subfolder, folder names are roles:
    // [mailspring]/snoozed = snoozed
    // [mailspring]/XXX = xxx
    string mailspringPrefix = MAILSPRING_FOLDER_PREFIX_V1 + delimiter;
    transform(mailspringPrefix.begin(), mailspringPrefix.end(), mailspringPrefix.begin(), ::tolower);
    if (path.size() > mailspringPrefix.size() && path.substr(0, mailspringPrefix.size()) == mailspringPrefix) {
        return path.substr(mailspringPrefix.size());
    }

           mailspringPrefix = MAILSPRING_FOLDER_PREFIX_V2 + delimiter;
    transform(mailspringPrefix.begin(), mailspringPrefix.end(), mailspringPrefix.begin(), ::tolower);
    if (path.size() > mailspringPrefix.size() && path.substr(0, mailspringPrefix.size()) == mailspringPrefix) {
        return path.substr(mailspringPrefix.size());
    }

    if (containerFolderPath != "") {
           mailspringPrefix = containerFolderPath + delimiter;
      transform(mailspringPrefix.begin(), mailspringPrefix.end(), mailspringPrefix.begin(), ::tolower);
      if (path.size() > mailspringPrefix.size() && path.substr(0, mailspringPrefix.size()) == mailspringPrefix) {
         return path.substr(mailspringPrefix.size());
      }
    }

    // Match against a lookup table of common names
    // [Gmail]/Spam => [gmail]/spam => spam
    if (COMMON_FOLDER_NAMES.find(path) != COMMON_FOLDER_NAMES.end()) {
        return COMMON_FOLDER_NAMES[path];
    }

    return "";
}

string MailUtils::pathForFile(string root, File * file, bool create) {
    string id = file->id();
    transform(id.begin(), id.end(), id.begin(), ::tolower);
    
    if (create && !create_directory(root)) { return ""; }
    string path = root + FS_PATH_SEP + id.substr(0, 2);
    if (create && !create_directory(path)) { return ""; }
    path += FS_PATH_SEP + id.substr(2, 2);
    if (create && !create_directory(path)) { return ""; }
    path += FS_PATH_SEP + id;
    if (create && !create_directory(path)) { return ""; }
    
    path += FS_PATH_SEP + file->safeFilename();
    return path;
}

shared_ptr<Label> MailUtils::labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> allLabels) {
    for (const auto & label : allLabels) {
        if (label->path() == mlname) {
            return label;
        }
    }
    
    // \\Inbox should match INBOX
    if (mlname.substr(0, 1) == "\\") {
        mlname = mlname.substr(1, mlname.length() - 1);
        transform(mlname.begin(), mlname.end(), mlname.begin(), ::tolower);

        for (const auto & label : allLabels) {
            string path(label->path());
            transform(path.begin(), path.end(), path.begin(), ::tolower);
            if (path.substr(0, 8) == "[gmail]/") {
                path = path.substr(8, path.length() - 8);
            }
                
            if (path == mlname) {
                return label;
            }
            
            // sent => [Gmail]/Sent Mail (sent), draft => [Gmail]/Drafts (drafts)
            if ((label->role() == mlname) || (label->role() == mlname + "s")) {
                return label;
            }
        }
    }

    cout << "\n\nIMPORTANT --- Label not found: " << mlname;
    return shared_ptr<Label>{};
}

vector<Query> MailUtils::queriesForUIDRangesInIndexSet(string remoteFolderId, IndexSet * set) {
    vector<Query> results {};
    vector<uint32_t> uids {};
    
    for (int ii = 0; ii < set->rangesCount(); ii++) {
        uint64_t left = RangeLeftBound(set->allRanges()[ii]);
        uint64_t right = RangeRightBound(set->allRanges()[ii]);

        spdlog::get("logger")->info("- Building queries for range {}-{}", left, right);

        if (left == UINT64_MAX) {
            // an empty range
            continue;
        }

        if (left > right) {
            // an invalid range
            spdlog::get("logger")->error("- IndexSet UID range ({}-{}) has a lower bound greater than it's upper bound.", left, right);
            continue;
        }

        if (right == UINT64_MAX) {
            // this range has a * upper bound, we need to represent it as a "uid > X" query.
            results.push_back(Query().equal("remoteFolderId", remoteFolderId).gte("remoteUID", left));
        } else if (right - left > 50) {
            // this range has many items, just express it as a bounded range query
            results.push_back(Query().equal("remoteFolderId", remoteFolderId).gte("remoteUID", left).lt("remoteUID", right));
        } else {
            // this range has a few items, throw them in a pile and we'll make a few queries for these specific UIDs
            for (uint64_t x = left; x <= right; x ++) {
                uids.push_back((uint32_t)x);
            }
        }
    }

    if (uids.size() > 0) {
        for (vector<uint32_t> chunk : MailUtils::chunksOfVector(uids, 200)) {
            results.push_back(Query().equal("remoteFolderId", remoteFolderId).equal("remoteUID", chunk));
        }
    }

    return results;
}

vector<uint32_t> MailUtils::uidsOfArray(Array * array) {
    vector<uint32_t> uids {};
    uids.reserve(array->count());
    for (int ii = 0; ii < array->count(); ii++) {
        uids.push_back(((IMAPMessage*)array->objectAtIndex(ii))->uid());
    }
    return uids;
}

string MailUtils::idForFolder(string accountId, string folderPath) {
    vector<unsigned char> hash(32);
    string src_str = accountId + ":" + folderPath;
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idForFile(Message * message, Attachment * attachment) {
    vector<unsigned char> hash(32);
    string src_str = message->id() + ":" + message->accountId();
    bool has_something_unique = false;
    
    if (attachment->partID() != nullptr) {
        src_str = src_str + ":" + attachment->partID()->UTF8Characters();
        has_something_unique = true;
    }
    if (attachment->uniqueID() != nullptr) {
        src_str = src_str + ":" + attachment->uniqueID()->UTF8Characters();
        has_something_unique = true;
    }
    
    if (has_something_unique == false) {
        string description = attachment->description()->UTF8Characters();
        spdlog::get("logger")->warn("Encountered an attachment with no partID or uniqueID to form a unique ID. Falling back to description. Debug Info:\n" + description);
        src_str = src_str + ":" + description;
    }
    
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idRandomlyGenerated() {
    static string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    string result;
    result.resize(40);
    
    if (!calledsrand) {
        srand((unsigned int)time(0));
        calledsrand = true;
    }
    for (int i = 0; i < 40; i++) {
        result[i] = charset[rand() % charset.length()];
    }
    return result;
}

string MailUtils::idForDraftHeaderMessageId(string accountId, string headerMessageId)
{
    vector<unsigned char> hash(32);
    string src_str = accountId + ":" + headerMessageId;
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

#define SCHEMA_1_START_DATE 1518652800 // 2/15/2018

static int _baseIDSchemaVersion = 0;

void MailUtils::setBaseIDVersion(time_t identityCreationDate) {
    if (identityCreationDate > SCHEMA_1_START_DATE) {
        _baseIDSchemaVersion = 1;
    } else {
        _baseIDSchemaVersion = 0;
    }
    spdlog::get("logger")->info("Identity created at {} - using ID Schema {}", identityCreationDate, _baseIDSchemaVersion);
}

string MailUtils::idForEvent(string accountId, string calendarId, string icsUID, string recurrenceId) {
    // Use icsUID (iCalendar UID) for stable IDs across event modifications.
    // The etag changes on every modification, but icsUID is the stable identifier.
    // For recurring event exceptions, include recurrenceId to distinguish from master.
    string src_str = accountId;
    src_str = src_str.append("-");
    src_str = src_str.append(calendarId);
    src_str = src_str.append("-");
    src_str = src_str.append(icsUID);
    if (!recurrenceId.empty()) {
        src_str = src_str.append("-");
        src_str = src_str.append(recurrenceId);
    }
    vector<unsigned char> hash(32);
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idForCalendar(string accountId, string url) {
    string src_str = accountId;
    src_str = src_str.append("-");
    src_str = src_str.append(url);
    vector<unsigned char> hash(32);
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idForMessage(string accountId, string folderPath, IMAPMessage * msg) {
    
    /* I want to correct flaws in the ID algorithm, but changing this will cause
     duplicate messages to appear in threads and message metadata to be lost.
     
     - Use the new scheme for ANY messages dated > 2/15/2018
     - Use the new scheme for ALL messages if identity was created after 2/15/2018
     
     This should ensure that:
     - old users get old IDs on old mail, new IDs on new mail
     - new users get new IDs on all mail
     
     Scheme is 0 or 1
    */
    int scheme = _baseIDSchemaVersion;
    if (msg->header()->date() > SCHEMA_1_START_DATE || msg->header()->date() <= 0) {
        scheme = 1;
    }
    
    Array * addresses = new Array();
    addresses->addObjectsFromArray(msg->header()->to());
    addresses->addObjectsFromArray(msg->header()->cc());
    addresses->addObjectsFromArray(msg->header()->bcc());
    
    Array * emails = new Array();
    for (int i = 0; i < addresses->count(); i ++) {
        Address * addr = (Address*)addresses->objectAtIndex(i);
        emails->addObject(addr->mailbox());
    }
    
    emails->sortArray(compareEmails, NULL);
    
    String * participants = emails->componentsJoinedByString(MCSTR(""));

    addresses->release();
    emails->release();
    
    String * messageID = msg->header()->isMessageIDAutoGenerated() ? MCSTR("") : msg->header()->messageID();
    String * subject = msg->header()->subject();
    
    string src_str = accountId;
    src_str = src_str.append("-");
    if (scheme == 1) {
        time_t date = msg->header()->date();
        if (date == -1) {
            date = msg->header()->receivedDate();
        }
        if (date > 0) {
            // Use the unix timestamp, not a formatted (localized) date
            src_str = src_str.append(to_string(date));
        } else {
            // This message has no date information and subject + recipients alone are not enough
            // to build a stable ID across the mailbox.
            
            // As a fallback, we use the Folder + UID. The UID /will/ change when UIDInvalidity
            // occurs and if the message is moved to another folder, but seeing it as a delete +
            // create (and losing metadata) is better than sync thrashing caused by it thinking
            // many UIDs are all the same message.
            src_str = src_str.append(folderPath + ":" + to_string(msg->uid()));
        }
    } else {
        src_str = src_str.append(localTimestampForTime(msg->header()->date()));
    }
    if (subject) {
        src_str = src_str.append(subject->UTF8Characters());
    }
    src_str = src_str.append("-");
    src_str = src_str.append(participants->UTF8Characters());
    src_str = src_str.append("-");
    src_str = src_str.append(messageID->UTF8Characters());

    vector<unsigned char> hash(32);
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::qmarks(size_t count) {
    if (count == 0) {
        return "";
    }
    string qmarks{"?"};
    for (int i = 1; i < count; i ++) {
        qmarks = qmarks + ",?";
    }
    return qmarks;
}

string MailUtils::qmarkSets(size_t count, size_t perSet) {
    if (count == 0) {
        return "";
    }
    string one = "(" + MailUtils::qmarks(perSet) + ")";
    string qmarks{one};
    for (int i = 1; i < count; i ++) {
        qmarks = qmarks + "," + one;
    }
    return qmarks;
}

// *******************************
// Sessions and Logging

static bool _verboseLogging = false;

void MailUtils::enableVerboseLogging() {
    _verboseLogging = true;
}

class MailcoreSPDLogger : public ConnectionLogger {
  public:
    void log(string str) {
        spdlog::get("logger")->info(str);
    }

    void log(void *sender, ConnectionLogType logType, Data *buffer) {
        string logTypeString{"unknown"};
        switch (logType)
        {
        case ConnectionLogTypeReceived:
            logTypeString = "recv";
            break;

        case ConnectionLogTypeSent:
            logTypeString = "sent";
            break;

        case ConnectionLogTypeSentPrivate:
            logTypeString = "sent-private";
            break;

        case ConnectionLogTypeErrorParse:
            logTypeString = "error-parse";
            break;

        case ConnectionLogTypeErrorReceived:
            logTypeString = "error-received";
            break;

        case ConnectionLogTypeErrorSent:
            logTypeString = "error-sent";
            break;

        default:
            break;
        }
        string msg = buffer != nullptr ? buffer->stringWithCharset("UTF-8")->UTF8Characters() : "";
        spdlog::get("logger")->info("{} {}", logTypeString, msg);
    }
};

void MailUtils::configureSessionForAccount(IMAPSession &session, shared_ptr<Account> account) {
    if (account->refreshToken() != "") {
        XOAuth2Parts parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        session.setUsername(AS_MCSTR(parts.username));
        session.setOAuth2Token(AS_MCSTR(parts.accessToken));
        session.setAuthType(AuthTypeXOAuth2);
    } else {
        session.setUsername(AS_MCSTR(account->IMAPUsername()));
        session.setPassword(AS_MCSTR(account->IMAPPassword()));
    }
    session.setHostname(AS_MCSTR(account->IMAPHost()));
    session.setPort(account->IMAPPort());
    if (account->IMAPSecurity() == "SSL / TLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    } else if (account->IMAPSecurity() == "STARTTLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeStartTLS);
    } else {
        session.setConnectionType(ConnectionType::ConnectionTypeClear);
    }
    if (account->IMAPAllowInsecureSSL()) {
        session.setCheckCertificateEnabled(false);
    }

    if (_verboseLogging) {
        session.setConnectionLogger(new MailcoreSPDLogger());
    }
}

void MailUtils::configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account) {
    if (account->refreshToken() != "") {
        XOAuth2Parts parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        session.setUsername(AS_MCSTR(parts.username));
        session.setOAuth2Token(AS_MCSTR(parts.accessToken));
        session.setAuthType(AuthTypeXOAuth2);
    } else {
        session.setUsername(AS_MCSTR(account->SMTPUsername()));
        session.setPassword(AS_MCSTR(account->SMTPPassword()));
    }
    session.setHostname(AS_MCSTR(account->SMTPHost()));
    session.setPort(account->SMTPPort());
    if (account->SMTPSecurity() == "SSL / TLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    } else if (account->SMTPSecurity() == "STARTTLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeStartTLS);
    } else {
        session.setConnectionType(ConnectionType::ConnectionTypeClear);
    }
    if (account->SMTPAllowInsecureSSL()) {
        session.setCheckCertificateEnabled(false);
    }

    if (_verboseLogging) {
        session.setConnectionLogger(new MailcoreSPDLogger());
    }
}

IMAPMessagesRequestKind MailUtils::messagesRequestKindFor(IndexSet * capabilities, bool heavyOrNeedToComputeIDs) {
    bool gmail = capabilities->containsIndex(IMAPCapabilityGmail);
    
    if (heavyOrNeedToComputeIDs) {
        if (gmail) {
            return IMAPMessagesRequestKind(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindInternalDate | IMAPMessagesRequestKindFlags | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);
        }
        return IMAPMessagesRequestKind(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindInternalDate | IMAPMessagesRequestKindFlags);
    }
    
    if (gmail) {
        return IMAPMessagesRequestKind(IMAPMessagesRequestKindFlags | IMAPMessagesRequestKindGmailLabels);
    }
    return IMAPMessagesRequestKind(IMAPMessagesRequestKindFlags);
}


// *******************************
// Worker Sleep Implementation

std::mutex workerSleepMtx;
std::condition_variable workerSleepCV;

void MailUtils::sleepWorkerUntilWakeOrSec(int sec) {
    auto desiredTime = std::chrono::system_clock::now();
    desiredTime += chrono::milliseconds(sec * 1000);
    unique_lock<mutex> lck(workerSleepMtx);
    workerSleepCV.wait_until(lck, desiredTime);
}

void MailUtils::wakeAllWorkers() {
    // wake sync workers that sleep between cycles
    {
        lock_guard<mutex> lck(workerSleepMtx);
        workerSleepCV.notify_all();
    }
    
    // wake the metadata expiration thread to look for metadata
    // that may have missed it's expiration
    WakeAllMetadataExpirationWorkers();
}
