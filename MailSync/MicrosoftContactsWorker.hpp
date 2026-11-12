//
//  MicrosoftContactsWorker.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MicrosoftContactsWorker_hpp
#define MicrosoftContactsWorker_hpp

#include "Account.hpp"
#include "Contact.hpp"
#include "ContactBook.hpp"
#include "MailStore.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

static string GRAPH_SYNC_SOURCE = "graph";

/*
 Syncs an Office 365 / Outlook account's contacts through Microsoft Graph. A Contact row's `info`
 is the Graph contact resource as the pass selects it (the client maps it to and from its editor),
 its `etag` the resource's changeKey, and the Graph id is `info.id`. Every contact folder is read,
 each through its own delta link, into the account's one ContactBook. Graph has no contact groups.
 */
class MicrosoftContactsWorker {
    unique_ptr<MailStore> store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    // The account's Graph contacts by Graph id, as of `contactsGeneration` writes.
    map<string, shared_ptr<Contact>> contacts;
    unsigned contactsGeneration = 0;

    std::function<string()> tokenProvider();
    shared_ptr<ContactBook> findOrCreateBook();
    vector<string> listFolderIds();
    void refreshContactsIfWritten(bool force = false);
    void syncFolder(const string & folderId, const string & storedLink, const string & bookId,
                    json & nextLinks, vector<pair<string, string>> & removals);
    void applyRemoteContact(shared_ptr<Contact> contact, const json & remote);
    void restoreContact(shared_ptr<Contact> row, const string & graphId, const json & previousInfo);

public:
    MicrosoftContactsWorker(shared_ptr<Account> account);

    void run();

    // Creates the contact in Graph, or PATCHes the fields `info` changes from `previousInfo`,
    // then stores Graph's copy in its row.
    void upsertContact(shared_ptr<Contact> contact, const json & info, const json & previousInfo);

    // Deletes the contact from Graph and removes its row. On failure the row is shown again.
    void deleteContact(shared_ptr<Contact> contact);

    // Called by a task's local phase before it changes the rows of these Graph ids, so that no pass
    // stores a copy of them from a delta page fetched before the change.
    static void noteLocalChange(const vector<string> & graphIds);
};

#endif /* MicrosoftContactsWorker_hpp */
