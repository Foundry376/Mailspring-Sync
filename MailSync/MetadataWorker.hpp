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

#ifndef MetadataWorker_hpp
#define MetadataWorker_hpp

#include "Account.hpp"
#include "Identity.hpp"
#include "MailStore.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

class MetadataWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;
    
    string deltasBuffer;
    string deltasCursor;
    int backoffStep;

public:
    MetadataWorker(shared_ptr<Account> account);
    
    void run();

    bool fetchMetadata(int page);
    void fetchDeltaCursor();
    void fetchDeltasBlocking();

    void setDeltaCursor(string cursor);

    void onDeltaData(void * contents, size_t bytes);
    void onDelta(const json & delta);

    void applyMetadataJSON(const json & metadata);
};

#endif /* MetadataWorker_hpp */
