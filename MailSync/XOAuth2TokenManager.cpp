//
//  DeltaStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "XOAuth2TokenManager.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"

#include <mailcore2/src/core/basetypes/MCBase64.h>

using json = nlohmann::json;

XOAuth2Parts buildParts(string xoauth2, int expiryDate) {
    int outlen = 0;
    char * out = MCDecodeBase64(xoauth2.c_str(), (int)xoauth2.size(), &outlen);
    std::string sout(out, outlen);
    free(out);
    
    // Parse the xoauth2 string apart
    // user=${username}\x01auth=Bearer ${accessToken}\x01\x01
    string divider = "\001auth=Bearer ";
    size_t split = sout.find(divider);
    
    if (split == string::npos) {
        throw SyncException("invalid-xoauth2", "The provided XOauth2 token could not be parsed.", false);
    }
    
    size_t o = 0;
    XOAuth2Parts parts;
    o = 5;
    parts.username = sout.substr(o, split - o);
    o = split + divider.size();
    parts.accessToken = sout.substr(o, sout.size() - 2 - o);
    parts.expiryDate = expiryDate;
    return parts;
}


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

    spdlog::get("logger")->info("Fetching XOAuth2 access token for {}", account->id());
    json updated = MakeAccountsRequest(account, "/auth/token/refresh", "POST", {
        {"xoauth_refresh_token", account->xoauthRefreshToken()}
    });

    if (updated.is_null() || !updated.count("xoauth2")) {
        throw SyncException("invalid-xoauth2-resp", "XOAuth2 token expired and the server did not provide a valid response to refresh.", false);
    }

    XOAuth2Parts parts = buildParts(updated["xoauth2"].get<string>(), updated["expiry_date"].get<int>());
    _cache[key] = parts;
    return parts;
}
