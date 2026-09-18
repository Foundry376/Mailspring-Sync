//
//  SyncException.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "SyncException.hpp"
#include "constants.h"

SyncException::SyncException(string key, string di, bool retryable) :
    key(key), debuginfo(di), retryable(retryable), GenericException()
{
    
}

SyncException::SyncException(CURLcode c, string di) :
    key(curl_easy_strerror(c)), debuginfo(di), GenericException()
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
        (c == CURLE_HTTP2) ||
        (c == CURLE_HTTP2_STREAM) ||
        (c == CURLE_AGAIN)) {
        retryable = true;
        offline = true;
    }
}

SyncException::SyncException(mailcore::ErrorCode c, string di) :
    key(""), debuginfo(di.c_str()), GenericException()
{
    if (ErrorCodeToTypeMap.count(c)) {
        key = ErrorCodeToTypeMap[c];
    }
    if (c == mailcore::ErrorConnection) {
        retryable = true;
        offline = true;
    }
    if (c == mailcore::ErrorParse) {
        // It seems that parsing errors are caused by abrupt connection termination?
        retryable = true;
    }
    if (c == mailcore::ErrorFetch) {
        // It seems that parsing errors are caused by abrupt connection termination?
        retryable = true;
    }
    if (c == mailcore::ErrorTemporarilyUnavailable) {
        // The server told us it could not serve the request right now (RFC 5530
        // [UNAVAILABLE] / [INUSE] / [LIMIT] / [SERVERBUG]). Nothing about the account
        // is wrong, so sleep and try again rather than terminating the process.
        retryable = true;
        offline = true;
    }
    if ((c == mailcore::ErrorGmailTooManySimultaneousConnections) ||
        (c == mailcore::ErrorGmailExceededBandwidthLimit) ||
        (c == mailcore::ErrorYahooUnavailable)) {
        // Provider-imposed limits that clear on their own. Treating these as fatal
        // used to terminate the process on every launch, which the client counts as
        // repeated crashes and answers by disabling the account - for a condition
        // that usually resolves within minutes and often isn't even caused by us
        // (other IMAP clients signed into the same mailbox consume the connection
        // cap too). Back off hard instead: retrying every two minutes is precisely
        // what keeps an account pinned against the limit.
        retryable = true;
        offline = true;
        retryDelaySec = 60 * 10;
    }
    if (c == mailcore::ErrorAuthentication) {
        authentication = true;
    }
}

bool SyncException::isRetryable() {
    return retryable;
}

bool SyncException::isOffline() {
    return offline;
}

bool SyncException::isAuthentication() {
    return authentication;
}

int SyncException::retryDelay() {
    return retryDelaySec;
}

json SyncException::toJSON() {
    return {
        {"what", what()},
        {"key", key},
        {"debuginfo", debuginfo},
        {"retryable", retryable},
        {"offline", offline},
    };
}
