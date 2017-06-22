//
//  MailUtils.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailUtils.hpp"
#include "sha256.h"
#include "constants.h"

using namespace std;
using namespace mailcore;

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
    } else {
        contact["name"] = nullptr;
    }
    if (addr->mailbox()) {
        contact["email"] = addr->mailbox()->UTF8Characters();
    } else {
        contact["email"] = nullptr;
    }
    return contact;
}


int MailUtils::compareEmails(void * a, void * b, void * context) {
    return ((String*)a)->compare((String*)b);
}

string MailUtils::timestampForTime(time_t time) {
    tm * ptm = localtime(&time);
    char buffer[32];
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", ptm);
    return string(buffer);
}

string MailUtils::roleForFolder(IMAPFolder * folder) {
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
    
    string path = string(folder->path()->UTF8Characters());
    transform(path.begin(), path.end(), path.begin(), ::tolower);
    
    if (COMMON_FOLDER_NAMES.find(path) != COMMON_FOLDER_NAMES.end()) {
        return COMMON_FOLDER_NAMES[path];
    }
    return "";
}

shared_ptr<Label> MailUtils::labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> & allLabels) {
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


vector<uint32_t> MailUtils::uidsOfIndexSet(IndexSet * set) {
    vector<uint32_t> uids {};
    Range * range = set->allRanges();
    for (int ii = 0; ii < set->rangesCount(); ii++) {
        for (int x = 0; x < range->length; x ++) {
            uids.push_back((uint32_t)(range->location + x));
        }
        range += sizeof(Range *);
    }
    return uids;
}

vector<uint32_t> MailUtils::uidsOfArray(Array * array) {
    vector<uint32_t> uids {};
    uids.reserve(array->count());
    for (int ii = 0; ii < array->count(); ii++) {
        uids.push_back(((IMAPMessage*)array->objectAtIndex(ii))->uid());
    }
    return uids;
}

vector<string> MailUtils::messageIdsOfArray(Array * array) {
    vector<string> ids {};
    ids.reserve(array->count());
    for (int ii = 0; ii < array->count(); ii++) {
        ids.push_back(MailUtils::idForMessage((IMAPMessage*)array->objectAtIndex(ii)));
    }
    return ids;
}

string MailUtils::idForFolder(IMAPFolder * folder) {
    vector<unsigned char> hash(32);
    string src_str = string(folder->path()->UTF8Characters());
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

string MailUtils::idForMessage(IMAPMessage * msg) {
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
    
    string src_str = timestampForTime(msg->header()->date());
    if (subject) {
        src_str = src_str.append(subject->UTF8Characters());
    }
    src_str = src_str.append("-");
    src_str = src_str.append(participants->UTF8Characters());
    src_str = src_str.append("-");
    src_str = src_str.append(messageID->UTF8Characters());
    
    vector<unsigned char> hash(32);
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
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
