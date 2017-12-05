//
//  DeltaStream.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef XOAUTH2TokenManager_hpp
#define XOAUTH2TokenManager_hpp

#include <stdio.h>
#include <mutex>
#include <condition_variable>
#include "Account.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

struct XOAuth2Parts {
    string username;
    string accessToken;
    time_t expiryDate;
};

class XOAuth2TokenManager  {
    map<string, XOAuth2Parts> _cache;
    mutex _cacheLock;

public:
    XOAuth2TokenManager();
    ~XOAuth2TokenManager();
    XOAuth2Parts partsForAccount(shared_ptr<Account> account);
};


shared_ptr<XOAuth2TokenManager> SharedXOAuth2TokenManager();


#endif /* XOAUTH2TokenManager_hpp */
