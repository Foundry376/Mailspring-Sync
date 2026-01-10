//
//  NetworkRequestUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 8/6/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef NetworkRequestUtils_hpp
#define NetworkRequestUtils_hpp

#include <curl/curl.h>
#include <stdio.h>
#include "json.hpp"

class Account;

using namespace nlohmann;
using namespace std;

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp);

// Helper to cleanup curl handle and associated header list stored in CURLOPT_PRIVATE
void CleanupCurlRequest(CURL * curl_handle);

CURL * CreateJSONRequest(string url, string method = "GET", string authorization = "", const char * payloadChars = nullptr);
CURL * CreateCalDavRequest(string url, string method = "GET", const char * payloadChars = nullptr);

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken);

const string PerformRequest(CURL * curl_handle);
const json PerformJSONRequest(CURL * curl_handle);

const string PerformExpectedRedirect(string url);

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string resp);

// Shorthand methods for Identity Server

CURL * CreateIdentityRequest(string path, string method = "GET", const char * payloadChars = nullptr);
const json PerformIdentityRequest(string path, string method = "GET", const json & payload = nullptr);

#endif /* NetworkRequestUtils_hpp */
