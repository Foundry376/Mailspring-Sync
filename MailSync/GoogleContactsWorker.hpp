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
#include "DavXML.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

class GoogleContactsWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

public:
    GoogleContactsWorker(shared_ptr<Account> account);

    void run();
    void paginateGoogleCollection(string urlRoot, string authorization, string syncTokenKey, std::function<void(json)> yieldBlock);
};

#endif /* GoogleContactsWorker_hpp */
