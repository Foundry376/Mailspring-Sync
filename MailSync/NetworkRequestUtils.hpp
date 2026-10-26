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
#include "JSONUtils.hpp"
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
// IMAP/SMTP (outlook.office.com) or one for Microsoft Graph, never both at once. Graph calendars
// get their own token: accounts authorized before Graph sending granted the calendar scopes but
// not the mail ones, and a refresh asking for any ungranted scope is refused outright.
enum class OAuthTokenAudience { Mail, MicrosoftGraphMail, MicrosoftGraphCalendars };

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken, OAuthTokenAudience audience = OAuthTokenAudience::Mail);

struct GraphResponse {
    long status = 0;
    json body; // parsed JSON, or {"text": raw} when the body is not JSON
    long retryAfter = 0; // seconds from a Retry-After header, 0 when absent

    bool ok() const { return status >= 200 && status <= 299; }
};

// MS_GRAPH_URL replaces https://graph.microsoft.com/v1.0, for the test harness's fake Graph server.
string MicrosoftGraphURL();

// A throttled (429 or 503) Graph request is tried this many times, waiting out its Retry-After.
const int GRAPH_REQUEST_ATTEMPTS = 3;
// Seconds to wait before retrying a throttled Graph request: its Retry-After, kept to 1-30.
long GraphRetryWait(long retryAfter);

// One Graph request. Throws SyncException only when no HTTP response arrives; the caller
// interprets the status. 429 and 503 are retried after Retry-After (at most twice), since
// Graph returns them without processing the request. An empty accessToken sends neither
// Authorization nor Prefer, as upload session URLs require. Separate from CreateJSONRequest
// because bodies may be binary (upload chunks, base64 MIME) and requests carry extra headers.
GraphResponse PerformGraphRequest(const string & method, const string & url, const string & accessToken, const string & body = "", const string & contentType = "application/json", const vector<string> & headers = {});

// "<code>: <message>" from a Graph error response, for logs and the client's error dialog.
string GraphErrorDescription(const GraphResponse & resp);

// The id of the signed-in mailbox's Sent Items folder, or "" when Graph does not return one
// (the response is copied into `failure` when given).
string GraphSentItemsId(const string & accessToken, GraphResponse * failure = nullptr);

// HTTP helpers

string URLEscape(const string & value);

// The value of a header in a block of raw response headers, matched case-insensitively.
string HTTPHeaderValue(const string & headers, const string & headerName);

// Seconds to wait from a Retry-After value: delay-seconds or an HTTP-date (RFC 7231 §7.1.3).
int ParseRetryAfter(const string & retryAfter);

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
