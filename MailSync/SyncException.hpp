//
//  SyncException.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef SyncException_hpp
#define SyncException_hpp

#include <stdio.h>
#include <MailCore/MailCore.h>
#include <curl/curl.h>
#include "json.hpp"
#include "GenericException.hpp"

using namespace nlohmann;

using namespace std;
using namespace mailcore;

class SyncException : public GenericException {
    bool retryable = false;
    bool offline = false;
    bool authentication = false;
    int retryDelaySec = 120;
    
public:
    SyncException(string key, string di, bool retryable);
    SyncException(CURLcode c, string di);
    SyncException(mailcore::ErrorCode c, string di);
    string key;
    string debuginfo;
    bool isRetryable();
    bool isOffline();

    // True when the server rejected our credentials. These are not retryable, but
    // a worker that has already authenticated successfully treats the first couple
    // of them as a server hiccup rather than terminating the process. (See main.cpp)
    bool isAuthentication();

    // How long a worker should wait before trying again. Server-imposed quota and
    // connection limits get a much longer backoff than the default, because retrying
    // every two minutes is what keeps an account pinned against the limit.
    int retryDelay();

    json toJSON();
};


#endif /* SyncException_hpp */
