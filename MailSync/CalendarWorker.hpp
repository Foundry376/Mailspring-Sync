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

#ifndef CalendarWorker_hpp
#define CalendarWorker_hpp

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

class CalendarWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    string server;
    string principalPath;
    
public:
    CalendarWorker(shared_ptr<Account> account);
    
    void run();
    void runForCalendar(string id, string name, string path);

    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "");

    void print_xpath_nodes(xmlNodeSetPtr nodes, FILE* output);
};

#endif /* CalendarWorker_hpp */
