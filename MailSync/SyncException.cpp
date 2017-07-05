//
//  SyncException.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "SyncException.hpp"
#include "constants.h"

SyncException::SyncException(string key, string di, bool retryable) :
    key(key), debuginfo(di), retryable(retryable)
{
    
}

SyncException::SyncException(mailcore::ErrorCode c, string di) :
    key(ErrorCodeToTypeMap[c]), debuginfo(di)
{
    if (c == mailcore::ErrorConnection) {
        retryable = true;
    }
}

bool SyncException::isRetryable() {
    return retryable;
}

json SyncException::toJSON() {
    return {
        {"key", key},
        {"debuginfo", debuginfo},
        {"retryable", retryable},
    };
}
