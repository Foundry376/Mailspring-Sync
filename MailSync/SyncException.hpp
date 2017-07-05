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
#include "json.hpp"

using json = nlohmann::json;

using namespace std;
using namespace mailcore;

class SyncException : public std::exception {
    string debuginfo;
    string key;
    bool retryable = false;
    
public:
    SyncException(string key, string di, bool retryable);
    SyncException(mailcore::ErrorCode c, string di);
    bool isRetryable();
    json toJSON();
};


#endif /* SyncException_hpp */
