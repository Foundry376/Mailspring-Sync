#include "mailsync/xoauth2_token_manager.hpp"
#include "mailsync/network_request_utils.hpp"
#include "mailsync/sync_exception.hpp"



// Singleton Implementation

std::shared_ptr<XOAuth2TokenManager> _globalXOAuth2TokenManager = std::make_shared<XOAuth2TokenManager>();

std::shared_ptr<XOAuth2TokenManager> SharedXOAuth2TokenManager() {
    return _globalXOAuth2TokenManager;
}

// Class

XOAuth2TokenManager::XOAuth2TokenManager() {
}


XOAuth2TokenManager::~XOAuth2TokenManager() {
}



XOAuth2Parts XOAuth2TokenManager::partsForAccount(std::shared_ptr<Account> account) {
    std::string key = account->id();

    // There's not much of a point to having two threads request the same token at once.
    // Only allow one thread to access / update the cache and make others wait until it
    // exits.
    std::lock_guard<std::mutex> guard(_cacheLock);

    if (_cache.find(key) != _cache.end()) {
        XOAuth2Parts parts = _cache.at(key);
        // buffer of 60 sec since we actually need time to use the token
        if (parts.expiryDate > time(0) + 60) {
            return parts;
        }
    }

    auto refreshClientId = account->refreshClientId();
    nlohmann::json updated {};
    if (refreshClientId != "") {
        spdlog::get("logger")->info("Fetching XOAuth2 access token ({}) for {}", account->provider(), account->id());
        updated = MakeOAuthRefreshRequest(account->provider(), refreshClientId, account->refreshToken());
        updated["expiry_date"] = time(0) + updated["expires_in"].get<int>();
    } else {
        throw SyncException("invalid-xoauth2-resp", "XOAuth2 token expired and Mailspring no longer does server-side token refresh.", false);
    }

    XOAuth2Parts parts;
    parts.username = account->IMAPUsername();
    parts.accessToken = updated["access_token"].get<std::string>();
    parts.expiryDate = updated["expiry_date"].get<int>();
    _cache[key] = parts;
    return parts;
}
