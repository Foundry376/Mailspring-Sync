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

// Token response parsing

static const int XOAUTH2_ASSUMED_EXPIRES_IN = 3600;

// expires_in is optional in RFC 6749 s5.1 and some providers send it as a string. Neither
// is a reason to throw away an access token we were just handed, so assume one hour.
static int expiresInOf(const json & resp) {
    if (!resp.is_object() || !resp.count("expires_in")) {
        return XOAUTH2_ASSUMED_EXPIRES_IN;
    }
    const json & value = resp["expires_in"];
    if (value.is_number()) {
        return value.get<int>();
    }
    if (value.is_string()) {
        try {
            return stoi(value.get<string>());
        } catch (std::exception &) {
            // not a number after all - fall through
        }
    }
    return XOAUTH2_ASSUMED_EXPIRES_IN;
}

static string accessTokenOf(const json & resp) {
    if (!resp.is_object() || !resp.count("access_token") || !resp["access_token"].is_string()) {
        return "";
    }
    return resp["access_token"].get<string>();
}

// Reaches the log and, in --mode test, the client. The body may be a captive portal's
// whole HTML page, so bound it and redact anything credential-shaped.
static string summarizeForLog(const json & resp) {
    static const vector<string> SENSITIVE_KEYS { "access_token", "refresh_token", "id_token" };

    json redacted = resp;
    if (redacted.is_object()) {
        for (auto & key : SENSITIVE_KEYS) {
            if (redacted.count(key)) {
                redacted[key] = "*********";
            }
        }
    }

    string text = redacted.dump();
    if (text.length() > 512) {
        text = text.substr(0, 512) + "... (truncated)";
    }
    return "The token endpoint did not return an access token. Response: " + text;
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
    if (refreshClientId == "") {
        throw SyncException("invalid-xoauth2-resp", "XOAuth2 token expired and Mailspring no longer does server-side token refresh.", false);
    }

    spdlog::get("logger")->info("Fetching XOAuth2 access token ({}) for {}", account->provider(), account->id());
    json updated = MakeOAuthRefreshRequest(account->provider(), refreshClientId, account->refreshToken());

    // A 2xx does not mean the body is a token response: a captive portal or intercepting
    // proxy answers 200 with its own sign-in page, which PerformJSONRequest returns as
    // {"text": ...}. Reading the fields blind threw json::type_error, which is not a
    // SyncException, so callers aborted instead of backing off.
    if (!accessTokenOf(updated).length()) {
        // An OAuth error object is the provider refusing, so retrying cannot help.
        // Anything else did not come from the provider at all - interception, transient.
        bool providerRefused = updated.is_object() && updated.count("error");
        bool retryable = !providerRefused;
        bool offline = !providerRefused;
        throw SyncException("invalid-xoauth2-resp", summarizeForLog(updated), retryable, offline);
    }

    if (updated.count("refresh_token") && updated["refresh_token"].is_string()) {
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
    parts.accessToken = accessTokenOf(updated);
    parts.expiryDate = time(0) + expiresInOf(updated);
    _cache[key] = parts;
    return parts;
}
