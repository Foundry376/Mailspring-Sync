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

// debuginfo often carries a server's response body (ValidateRequestResp, DavXML), which need
// not be UTF-8: a Latin-1 error page, a truncated multibyte character. Handlers log
// toJSON().dump(), and nlohmann throws on invalid UTF-8; thrown from inside a catch handler,
// that terminates mailsync. Each byte that isn't part of a valid UTF-8 sequence becomes '?'.
static string validUTF8(const string & in) {
    string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = in[i];
        size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool valid = len > 0 && i + len <= in.size();
        for (size_t k = 1; valid && k < len; k++) {
            valid = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        }
        if (valid && len >= 3) {
            unsigned char c1 = in[i + 1];
            // Overlong forms, UTF-16 surrogates, and code points past U+10FFFF (RFC 3629 §4).
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 > 0x9F) ||
                (c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 > 0x8F)) {
                valid = false;
            }
        }
        if (valid) {
            out.append(in, i, len);
            i += len;
        } else {
            out += '?';
            i += 1;
        }
    }
    return out;
}

SyncException::SyncException(string key, string di, bool retryable, bool offline) :
    key(validUTF8(key)), debuginfo(validUTF8(di)), retryable(retryable), offline(offline), GenericException()
{
    
}

SyncException::SyncException(CURLcode c, string di) :
    key(curl_easy_strerror(c)), debuginfo(validUTF8(di)), GenericException()
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
    key(""), debuginfo(validUTF8(di)), GenericException()
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
}

bool SyncException::isRetryable() {
    return retryable;
}

bool SyncException::isOffline() {
    return offline;
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
