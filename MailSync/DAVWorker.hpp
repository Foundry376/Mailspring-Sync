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

#ifndef DAVWorker_hpp
#define DAVWorker_hpp

#include "Account.hpp"
#include "Identity.hpp"
#include "MailStore.hpp"
#include "DavXML.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

struct AddressBookResult {
    std::string id;
    std::string url;
};

class DAVWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    string calHost;
    string calPrincipal;
    string cardHost;
    string cardPrincipal;
    
public:
    DAVWorker(shared_ptr<Account> account);
    
    void run();
    
    AddressBookResult resolveAddressBook();
    
    void writeAndResyncContact(shared_ptr<Contact> contact);
    void deleteContact(shared_ptr<Contact> contact);
    
    void runContacts();
    void runForAddressBook(AddressBookResult ab);

    shared_ptr<Contact> ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(shared_ptr<Contact> contact);

    void runCalendars();
    void runForCalendar(string id, string name, string path);

    struct curl_slist * baseHeaders();
    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "");
    string performVCardRequest(string _url, string method, string vcard = "", string existingEtag = "");
    
};

#endif /* DAVWorker_hpp */
