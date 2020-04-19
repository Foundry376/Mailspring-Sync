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
    
public:
    SyncException(string key, string di, bool retryable);
    SyncException(CURLcode c, string di);
    SyncException(mailcore::ErrorCode c, string di);
    string key;
    string debuginfo;
    bool isRetryable();
    bool isOffline();
    json toJSON();
};


#endif /* SyncException_hpp */
