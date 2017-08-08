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

SyncException::SyncException(CURLcode c, string di) :
    key(curl_easy_strerror(c)), debuginfo(di)
{
    if ((c == CURLE_COULDNT_RESOLVE_PROXY) ||
        (c == CURLE_COULDNT_RESOLVE_HOST) ||
        (c == CURLE_COULDNT_CONNECT) ||
        (c == CURLE_HTTP_RETURNED_ERROR) ||
        (c == CURLE_OPERATION_TIMEDOUT) ||
        (c == CURLE_PARTIAL_FILE) ||
        (c == CURLE_HTTP_POST_ERROR) ||
        (c == CURLE_SSL_CONNECT_ERROR) ||
        (c == CURLE_TOO_MANY_REDIRECTS) ||
        (c == CURLE_PEER_FAILED_VERIFICATION) ||
        (c == CURLE_GOT_NOTHING) ||
        (c == CURLE_SEND_ERROR) ||
        (c == CURLE_RECV_ERROR) ||
        (c == CURLE_AGAIN)) {
        retryable = true;
    }
}

SyncException::SyncException(mailcore::ErrorCode c, string di) :
    key(ErrorCodeToTypeMap[c]), debuginfo(di)
{
    if (c == mailcore::ErrorConnection) {
        retryable = true;
    }
    if (c == mailcore::ErrorParse) {
        // It seems that parsing errors are caused by abrupt connection termination?
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
