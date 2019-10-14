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
#include "ContactBook.hpp"
#include "MailStore.hpp"
#include "DavXML.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

typedef string ETAG;

class DAVWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

    string calHost;
    string calPrincipal;
    
public:
    shared_ptr<Account> account;

    DAVWorker(shared_ptr<Account> account);
    
    void run();
    
    shared_ptr<ContactBook> resolveAddressBook();
    
    void writeAndResyncContact(shared_ptr<Contact> contact);
    void deleteContact(shared_ptr<Contact> contact);
    
    void runContacts();
    void runForAddressBook(shared_ptr<ContactBook> ab);

    void ingestContactDeletions(shared_ptr<ContactBook> ab, vector<ETAG> deleted);
    shared_ptr<Contact> ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(shared_ptr<Contact> contact);

    void runCalendars();
    void runForCalendar(string id, string name, string path);

    const string getAuthorizationHeader();
    
    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "");
    string performVCardRequest(string _url, string method, string vcard = "", ETAG existingEtag = "");
    
};

#endif /* DAVWorker_hpp */
