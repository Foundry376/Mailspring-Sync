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
#include <vector>

class Account;

using namespace nlohmann;
using namespace std;

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp);

// Structure to hold curl request data that needs cleanup
// Stored in CURLOPT_PRIVATE to enable proper cleanup of headers
struct CurlRequestData {
    struct curl_slist *headers;

    CurlRequestData() : headers(nullptr) {}
};

// Use in place of curl_easy_init: on macOS it adds the SecTrust check of the server's certificates.
CURL * CreateCurlHandle();

// Helper to cleanup curl handle and associated header list stored in CURLOPT_PRIVATE
void CleanupCurlRequest(CURL * curl_handle);

CURL * CreateJSONRequest(string url, string method = "GET", string authorization = "", const char * payloadChars = nullptr);
CURL * CreateCalDavRequest(string url, string method = "GET", const char * payloadChars = nullptr);

// Microsoft issues access tokens per resource: one refresh token yields either a token for
// IMAP/SMTP (outlook.office.com) or one for Microsoft Graph, never both at once.
enum class OAuthTokenAudience { Mail, MicrosoftGraph };

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken, OAuthTokenAudience audience = OAuthTokenAudience::Mail);

struct GraphResponse {
    long status;
    json body; // parsed JSON, or {"text": raw} when the body is not JSON
    long retryAfter; // seconds from a Retry-After header, 0 when absent
};

// MS_GRAPH_URL replaces https://graph.microsoft.com/v1.0, for the test harness's fake Graph server.
string MicrosoftGraphURL();

// One Graph request. Throws SyncException only when no HTTP response arrives; the caller
// interprets the status. 429 and 503 are retried after Retry-After (at most twice), since
// Graph returns them without processing the request. An empty accessToken sends neither
// Authorization nor Prefer, as upload session URLs require.
GraphResponse PerformGraphRequest(string method, string url, string accessToken, const string & body = "", string contentType = "application/json", vector<string> headers = {});

// "<code>: <message>" from a Graph error response, for logs and the client's error dialog.
string GraphErrorDescription(const GraphResponse & resp);

const string PerformRequest(CURL * curl_handle);
const json PerformJSONRequest(CURL * curl_handle);

const string PerformExpectedRedirect(string url);

// Throws on a transport error or a non-2xx status, freeing the handle (and its
// CurlRequestData) first. On success the caller frees it with CleanupCurlRequest.
void ValidateRequestResp(CURLcode res, CURL * curl_handle, string resp);

// Shorthand methods for Identity Server

CURL * CreateIdentityRequest(string path, string method = "GET", const char * payloadChars = nullptr);
const json PerformIdentityRequest(string path, string method = "GET", const json & payload = nullptr);

#endif /* NetworkRequestUtils_hpp */
