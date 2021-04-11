//
//  DeltaStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "XOAuth2TokenManager.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "DeltaStream.hpp"

using namespace nlohmann;

// Singleton Implementation

shared_ptr<XOAuth2TokenManager> _globalXOAuth2TokenManager = make_shared<XOAuth2TokenManager>();

shared_ptr<XOAuth2TokenManager> SharedXOAuth2TokenManager() {
    return _globalXOAuth2TokenManager;
}

// Class

XOAuth2TokenManager::XOAuth2TokenManager() {
}


XOAuth2TokenManager::~XOAuth2TokenManager() {
}



XOAuth2Parts XOAuth2TokenManager::partsForAccount(shared_ptr<Account> account) {
    string key = account->id();
    
    // There's not much of a point to having two threads request the same token at once.
    // Only allow one thread to access / update the cache and make others wait until it
    // exits.
    lock_guard<mutex> guard(_cacheLock);

    if (_cache.find(key) != _cache.end()) {
        XOAuth2Parts parts = _cache.at(key);
        // buffer of 60 sec since we actually need time to use the token
        if (parts.expiryDate > time(0) + 60) {
            return parts;
        }
    }

    auto refreshClientId = account->refreshClientId();
    json updated {};
    if (refreshClientId != "") {
        spdlog::get("logger")->info("Fetching XOAuth2 access token ({}) for {}", account->provider(), account->id());
        updated = MakeOAuthRefreshRequest(account->provider(), refreshClientId, account->refreshToken());
        updated["expiry_date"] = time(0) + updated["expires_in"].get<int>();
    } else {
        throw SyncException("invalid-xoauth2-resp", "XOAuth2 token expired and Mailspring no longer does server-side token refresh.", false);
    }

    if (updated.count("refresh_token")) {
        auto updatedRefreshToken = updated["refresh_token"].get<string>();
        if (updatedRefreshToken != account->refreshToken()) {
            spdlog::get("logger")->info("Saving updated XOAuth2 refresh token ({}) for {}", account->provider(), account->id());
            account->setRefreshToken(updatedRefreshToken);
            SharedDeltaStream()->sendUpdatedSecrets(account.get());
        } else {
            spdlog::get("logger")->info("XOAuth2 refresh token was returned but matches existing one.");
        }
    }

    XOAuth2Parts parts;
    parts.username = account->IMAPUsername();
    parts.accessToken = updated["access_token"].get<string>();
    parts.expiryDate = updated["expiry_date"].get<int>();
    _cache[key] = parts;
    return parts;
}
