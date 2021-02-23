#include "mailsync/sync_exception.hpp"
#include "mailsync/constants.hpp"

SyncException::SyncException(std::string key, std::string di, bool retryable) :
    key(key), debuginfo(di), retryable(retryable), GenericException()
{

}

SyncException::SyncException(CURLcode c, std::string di) :
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
        (c == CURLE_AGAIN)) {
        retryable = true;
        offline = true;
    }
}

SyncException::SyncException(mailcore::ErrorCode c, std::string di) :
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
}

bool SyncException::isRetryable() {
    return retryable;
}

bool SyncException::isOffline() {
    return offline;
}

nlohmann::json SyncException::toJSON() {
    return {
        {"what", what()},
        {"key", key},
        {"debuginfo", debuginfo},
        {"retryable", retryable},
    };
}
