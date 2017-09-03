//
//  SyncException.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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
    string debuginfo;
    string key;
    bool retryable = false;
    
public:
    SyncException(string key, string di, bool retryable);
    SyncException(CURLcode c, string di);
    SyncException(mailcore::ErrorCode c, string di);
    bool isRetryable();
    json toJSON();
};


#endif /* SyncException_hpp */
