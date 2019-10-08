//
//  MetadataWorker.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef GoogleContactsWorker_hpp
#define GoogleContactsWorker_hpp

#include "Account.hpp"
#include "Identity.hpp"
#include "MailStore.hpp"
#include "ContactGroup.hpp"
#include "Contact.hpp"
#include "DavXML.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

static string GOOGLE_SYNC_SOURCE = "gpeople";

class GoogleContactsWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

public:
    GoogleContactsWorker(shared_ptr<Account> account);

    void run();
    void paginateGoogleCollection(string urlRoot, string authorization, string syncTokenKey, std::function<void(json)> yieldBlock);
    
    void upsertContactGroup(shared_ptr<ContactGroup> group);
    void deleteContactGroup(string groupResourceName);
    void updateContactGroupMembership(shared_ptr<ContactGroup> group, vector<shared_ptr<Contact>> contacts, string direction);
    void deleteContact(shared_ptr<Contact> contact);
    void upsertContact(shared_ptr<Contact> contact);

private:
    void applyJSONToContact(shared_ptr<Contact> local, const json & conn);

};

#endif /* GoogleContactsWorker_hpp */
