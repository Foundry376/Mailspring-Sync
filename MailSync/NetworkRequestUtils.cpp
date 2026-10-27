//
//  NetworkRequestUtils.cpp
//  MailSync
//
//  Created by Ben Gotow on 8/6/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Identity.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "Account.hpp"
#include "MailUtils.hpp"

#include <sys/stat.h>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include <ctime>
#include "spdlog/spdlog.h"

#if __APPLE__
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <strings.h>
#endif

// Helper to properly cleanup a curl handle including headers stored in private data
void CleanupCurlRequest(CURL * curl_handle) {
    if (curl_handle == nullptr) return;

    CurlRequestData *data = nullptr;
    curl_easy_getinfo(curl_handle, CURLINFO_PRIVATE, &data);
    if (data != nullptr) {
        if (data->headers != nullptr) {
            curl_slist_free_all(data->headers);
        }
        delete data;
    }
    curl_easy_cleanup(curl_handle);
}

#if __APPLE__

// Why the last request on this thread was rejected by _verifyPeerWithSecTrust, for the error.
static thread_local string _secTrustRejection;

static vector<uint8_t> _derFromCertinfoPEM(const char * pem) {
    static const char * begin = "-----BEGIN CERTIFICATE-----";
    vector<uint8_t> der;
    const char * start = strstr(pem, begin);
    const char * end = strstr(pem, "-----END CERTIFICATE-----");
    if (start == nullptr || end == nullptr || end < start) {
        return der;
    }
    uint32_t bits = 0;
    int bitCount = 0;
    for (const char * c = start + strlen(begin); c < end; c++) {
        int value;
        if (*c >= 'A' && *c <= 'Z') value = *c - 'A';
        else if (*c >= 'a' && *c <= 'z') value = *c - 'a' + 26;
        else if (*c >= '0' && *c <= '9') value = *c - '0' + 52;
        else if (*c == '+') value = 62;
        else if (*c == '/') value = 63;
        else continue; // line breaks and '=' padding
        bits = (bits << 6) | value;
        bitCount += 6;
        if (bitCount >= 8) {
            bitCount -= 8;
            der.push_back((bits >> bitCount) & 0xFF);
        }
    }
    return der;
}

/*
 Apple's system libcurl uses LibreSSL, which trusts /etc/ssl/cert.pem and never consults
 Apple's revocation and distrust data. Apple blocked the compromised eMudhra intermediates
 (https://emudhra.we-have-a-problem.com/) only through that data, so this re-evaluates the
 peer chain with SecTrust after the handshake and before any request bytes, including
 Authorization headers and refresh tokens, are sent. It is the same evaluation mailcore
 applies to IMAP and SMTP on macOS. Failing to obtain the chain or hostname blocks the request.
*/
static int _verifyPeerWithSecTrust(void * clientp, char *, char *, int, int) {
    CURL * curl_handle = (CURL *)clientp;
    _secTrustRejection = "";

    char * scheme = nullptr;
    curl_easy_getinfo(curl_handle, CURLINFO_SCHEME, &scheme);
    if (scheme == nullptr || strcasecmp(scheme, "https") != 0) {
        return CURL_PREREQFUNC_OK;
    }

    // mailsync never sets CURLOPT_FOLLOWLOCATION, so the effective URL names the host we connected to.
    char * url = nullptr;
    char * host = nullptr;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    CURLU * parsed = curl_url();
    if (url != nullptr && curl_url_set(parsed, CURLUPART_URL, url, 0) == CURLUE_OK) {
        curl_url_get(parsed, CURLUPART_HOST, &host, 0);
    }
    string hostname = host ? host : "";
    curl_free(host);
    curl_url_cleanup(parsed);
    if (hostname.size() > 2 && hostname.front() == '[' && hostname.back() == ']') {
        hostname = hostname.substr(1, hostname.size() - 2);
    }

    struct curl_certinfo * certinfo = nullptr;
    curl_easy_getinfo(curl_handle, CURLINFO_CERTINFO, &certinfo);

    CFMutableArrayRef chain = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    for (int i = 0; certinfo != nullptr && i < certinfo->num_of_certs; i++) {
        for (struct curl_slist * field = certinfo->certinfo[i]; field != nullptr; field = field->next) {
            if (strncmp(field->data, "Cert:", 5) != 0) continue;
            vector<uint8_t> der = _derFromCertinfoPEM(field->data + 5);
            CFDataRef data = CFDataCreate(nullptr, der.data(), (CFIndex)der.size());
            SecCertificateRef cert = SecCertificateCreateWithData(nullptr, data);
            if (cert != nullptr) {
                CFArrayAppendValue(chain, cert);
                CFRelease(cert);
            }
            CFRelease(data);
        }
    }

    bool trusted = false;
    if (hostname == "") {
        _secTrustRejection = "could not determine the server's hostname";
    } else if (CFArrayGetCount(chain) == 0) {
        _secTrustRejection = "could not read the server's certificate chain";
    } else {
        CFStringRef hostCF = CFStringCreateWithCString(nullptr, hostname.c_str(), kCFStringEncodingUTF8);
        SecPolicyRef policy = SecPolicyCreateSSL(true, hostCF);
        SecTrustRef trust = nullptr;
        if (SecTrustCreateWithCertificates(chain, policy, &trust) == errSecSuccess) {
            CFErrorRef error = nullptr;
            trusted = SecTrustEvaluateWithError(trust, &error);
            if (error != nullptr) {
                CFStringRef description = CFErrorCopyDescription(error);
                char buffer[512] = "";
                CFStringGetCString(description, buffer, sizeof(buffer), kCFStringEncodingUTF8);
                _secTrustRejection = buffer;
                CFRelease(description);
                CFRelease(error);
            }
            CFRelease(trust);
        } else {
            _secTrustRejection = "could not create a trust evaluation";
        }
        CFRelease(policy);
        CFRelease(hostCF);
    }
    CFRelease(chain);
    return trusted ? CURL_PREREQFUNC_OK : CURL_PREREQFUNC_ABORT;
}

#endif

CURL * CreateCurlHandle() {
    CURL * curl_handle = curl_easy_init();
#if __APPLE__
    // CURLINFO_CERTINFO is only filled in for a new connection, so a reused one would have no
    // chain to evaluate. Forbidding reuse costs nothing: every handle performs one request.
    bool configured =
        curl_easy_setopt(curl_handle, CURLOPT_CERTINFO, 1L) == CURLE_OK &&
        curl_easy_setopt(curl_handle, CURLOPT_FORBID_REUSE, 1L) == CURLE_OK &&
        curl_easy_setopt(curl_handle, CURLOPT_PREREQFUNCTION, _verifyPeerWithSecTrust) == CURLE_OK &&
        curl_easy_setopt(curl_handle, CURLOPT_PREREQDATA, curl_handle) == CURLE_OK;
    if (!configured) {
        // Never make an HTTPS request without the SecTrust check.
        curl_easy_setopt(curl_handle, CURLOPT_PROTOCOLS_STR, "http");
    }
#endif
    return curl_handle;
}

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp) {
    string * buffer = (string *)userp;
    size_t real_size = length * nmemb;
    
    size_t oldLength = buffer->size();
    size_t newLength = oldLength + real_size;
    
    buffer->resize(newLength);
    std::copy((char*)contents, (char*)contents+real_size, buffer->begin() + oldLength);
    
    return real_size;
}

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken, OAuthTokenAudience audience) {
    CURL * curl_handle = CreateCurlHandle();
    bool microsoft = provider == "office365" || provider == "outlook";
    string microsoftURL = MailUtils::getEnvUTF8("MS_OAUTH_TOKEN_URL");
    if (microsoftURL == "") {
        microsoftURL = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
    }
    string url =
          provider == "gmail" ? "https://www.googleapis.com/oauth2/v4/token"
        : microsoft ? microsoftURL
        : "";
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);

    char * c = curl_easy_escape(curl_handle, clientId.c_str(), 0);
    char * r = curl_easy_escape(curl_handle, refreshToken.c_str(), 0);
    string payload = "grant_type=refresh_token&client_id=" + string(c) + "&refresh_token=" + string(r);
    curl_free(c);
    curl_free(r);

    if (microsoft) {
        // workaround the fact that Microsoft's OAUTH flow allows you to authorize many scopes, but you
        // have to get a separate token for outlook (email + IMAP) and contacts / calendar / Microsoft Graph APIs
        // separately. The same refresh token will give you access tokens, but the access tokens are different.
        if (audience == OAuthTokenAudience::MicrosoftGraphMail) {
            payload += "&scope=https%3A%2F%2Fgraph.microsoft.com%2FMail.ReadWrite%20https%3A%2F%2Fgraph.microsoft.com%2FMail.Send";
        } else if (audience == OAuthTokenAudience::MicrosoftGraphCalendars) {
            payload += "&scope=https%3A%2F%2Fgraph.microsoft.com%2FCalendars.ReadWrite%20https%3A%2F%2Fgraph.microsoft.com%2FCalendars.ReadWrite.Shared";
        } else {
            payload += "&scope=https%3A%2F%2Foutlook.office.com%2FIMAP.AccessAsUser.All%20https%3A%2F%2Foutlook.office.com%2FSMTP.Send";
        }
    }

    string gmailClientId = MailUtils::getEnvUTF8("GMAIL_CLIENT_ID");
    string gmailClientSecret = MailUtils::getEnvUTF8("GMAIL_CLIENT_SECRET");
    if (provider == "gmail" && clientId == gmailClientId) {
        // per https://stackoverflow.com/questions/59416326/safely-distribute-oauth-2-0-client-secret-in-desktop-applications-in-python,
        // we really do need to embed this in the application and it's more an extension of the Client ID than a proper Client Secret.
        // For a full explanation, see onboarding-helpers.ts in Mailspring. Please don't re-use this client id + secret in derivative
        // works or other products.
        payload += "&client_secret=" + gmailClientSecret;
    }

    // Store headers in CurlRequestData for proper cleanup
    CurlRequestData *requestData = new CurlRequestData();
    requestData->headers = curl_slist_append(requestData->headers, "Accept: application/json");
    requestData->headers = curl_slist_append(requestData->headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, requestData->headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, requestData);

    return PerformJSONRequest(curl_handle);
}

CURL * CreateJSONRequest(string url, string method, string authorization, const char * payloadChars) {
    CURL * curl_handle = CreateCurlHandle();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);

    // Store headers in CurlRequestData for proper cleanup
    CurlRequestData *requestData = new CurlRequestData();

    requestData->headers = curl_slist_append(requestData->headers, "Accept: application/json");

    if (authorization != "") {
        requestData->headers = curl_slist_append(requestData->headers, ("Authorization: " + authorization).c_str());
    }
    if (payloadChars != nullptr && strlen(payloadChars) > 0) {
        requestData->headers = curl_slist_append(requestData->headers, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, requestData->headers);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, requestData);

    return curl_handle;
}

const string PerformRequest(CURL * curl_handle) {
    string result;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    CURLcode res = curl_easy_perform(curl_handle);
    ValidateRequestResp(res, curl_handle, result);
    return result;
}


const string PerformExpectedRedirect(string url) {
    CURL * curl_handle = CreateCurlHandle();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10);

    string result;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    curl_easy_perform(curl_handle);

    long http_code = 0;
    string redirect = "";
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
    // RFC 6764 §5: clients MUST follow redirects on .well-known. 308 postdates the RFC.
    bool isRedirect = http_code == 301 || http_code == 302 || http_code == 303 || http_code == 307 || http_code == 308;
    if (isRedirect) {
        char * _redirect = nullptr;
        if (curl_easy_getinfo(curl_handle, CURLINFO_REDIRECT_URL, &_redirect) == CURLE_OK && _redirect != nullptr) {
            redirect = string(_redirect);
        }
    }
    curl_easy_cleanup(curl_handle);
    return redirect;
}


// A response body as JSON, or {"text": body} when it is not JSON (a captive portal's page).
static json _parseJSONOrText(const string & body) {
    try {
        return json::parse(body);
    } catch (json::exception &) {
        return {{"text", body}};
    }
}

// Throws, freeing the handle first, when curl_easy_perform did not get an HTTP response.
static void _throwIfTransportFailed(CURLcode res, CURL * curl_handle, const string & url) {
#if __APPLE__
    if (res == CURLE_ABORTED_BY_CALLBACK) {
        CleanupCurlRequest(curl_handle);
        throw SyncException(CURLE_PEER_FAILED_VERIFICATION, url + " " + _secTrustRejection);
    }
#endif
    if (res != CURLE_OK) {
        CleanupCurlRequest(curl_handle);
        throw SyncException(res, url);
    }
}

const json PerformJSONRequest(CURL * curl_handle) {
    string result = PerformRequest(curl_handle);
    json resultJSON = _parseJSONOrText(result);
    CleanupCurlRequest(curl_handle);
    return resultJSON;
}

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string resp) {
    char * _url = nullptr;
    if (curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &_url) != CURLE_OK || _url == nullptr) {
        CleanupCurlRequest(curl_handle);
        throw SyncException(res, "Unable to get URL");
    }
    string url { _url };
    _throwIfTransportFailed(res, curl_handle, url);

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code > 209) {
        CleanupCurlRequest(curl_handle); // note: cleans up _url;
        
        bool retryable = ((http_code != 403) && (http_code != 401));
        if (resp.find("invalid_grant") != string::npos) {
            retryable = false;
        }
        
        string debuginfo = url + " RETURNED " + resp;
        throw SyncException("Invalid Response Code: " + to_string(http_code), debuginfo, retryable);
    }
}

// Microsoft Graph

string MicrosoftGraphURL() {
    string url = MailUtils::getEnvUTF8("MS_GRAPH_URL");
    return url != "" ? url : "https://graph.microsoft.com/v1.0";
}

static size_t _onAppendHeader(char * buffer, size_t size, size_t nitems, void * userdata) {
    ((string *)userdata)->append(buffer, size * nitems);
    return size * nitems;
}

static GraphResponse _performGraphRequestOnce(const string & method, const string & url, const string & accessToken, const string & body, const string & contentType, const vector<string> & headers) {
    CURL * curl_handle = CreateCurlHandle();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    // Uploads can be large, so bound a stalled transfer rather than the whole request.
    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 60L);

    CurlRequestData *requestData = new CurlRequestData();
    requestData->headers = curl_slist_append(requestData->headers, "Accept: application/json");
    if (accessToken != "") {
        requestData->headers = curl_slist_append(requestData->headers, ("Authorization: Bearer " + accessToken).c_str());
        // Ids that survive the draft moving to Sent Items, so the sent copy can be found by the
        // draft's id. https://learn.microsoft.com/en-us/graph/outlook-immutable-id
        requestData->headers = curl_slist_append(requestData->headers, "Prefer: IdType=\"ImmutableId\"");
    }
    for (auto & header : headers) {
        requestData->headers = curl_slist_append(requestData->headers, header.c_str());
    }
    if (method != "GET" && method != "DELETE") {
        // Always send a body, even an empty one: Graph answers a bodiless POST with 411.
        requestData->headers = curl_slist_append(requestData->headers, ("Content-Type: " + contentType).c_str());
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, requestData->headers);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, requestData);

    string result;
    string responseHeaders;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, _onAppendHeader);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&responseHeaders);
    CURLcode res = curl_easy_perform(curl_handle);
    _throwIfTransportFailed(res, curl_handle, url);

    GraphResponse resp;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &resp.status);
    CleanupCurlRequest(curl_handle);
    resp.body = result.empty() ? json::object() : _parseJSONOrText(result);
    resp.retryAfter = ParseRetryAfter(HTTPHeaderValue(responseHeaders, "Retry-After"));
    return resp;
}

// A throttled (429, or 503 when the request allows it) Graph request is tried this many times.
static const int GRAPH_REQUEST_ATTEMPTS = 3;
// Graph JSON batching takes up to 20 requests (https://learn.microsoft.com/en-us/graph/json-batching).
static const size_t GRAPH_BATCH_LIMIT = 20;

static bool _graphRetries(long status, GraphRetry retry) {
    return status == 429 || (status == 503 && retry == GraphRetry::ThrottledOrUnavailable);
}

// Seconds to wait before retrying a throttled Graph request: its Retry-After, kept to 1-30.
static long _graphRetryWait(long retryAfter) {
    return (std::min)((std::max)(retryAfter, 1L), 30L); // parenthesized for <windows.h>'s min/max macros
}

GraphResponse PerformGraphRequest(const string & method, const string & url, const string & accessToken, const string & body, const string & contentType, const vector<string> & headers, GraphRetry retry) {
    GraphResponse resp;
    for (int attempt = 0; attempt < GRAPH_REQUEST_ATTEMPTS; attempt ++) {
        resp = _performGraphRequestOnce(method, url, accessToken, body, contentType, headers);
        if (!_graphRetries(resp.status, retry)) {
            break;
        }
        long wait = _graphRetryWait(resp.retryAfter);
        spdlog::get("logger")->info("-- Graph returned {}, retrying in {}s", resp.status, wait);
        std::this_thread::sleep_for(std::chrono::seconds(wait));
    }
    return resp;
}

// A batched response's headers arrive as a JSON object rather than a header block.
static string _batchedHeaderValue(const json & response, const string & headerName) {
    string block;
    for (const auto & header : JSONObject(response, "headers").items()) {
        if (header.value().is_string()) {
            block += header.key() + ": " + header.value().get<string>() + "\r\n";
        }
    }
    return HTTPHeaderValue(block, headerName);
}

vector<GraphResponse> PerformGraphBatch(const vector<json> & requests, const std::function<string()> & tokenProvider, GraphRetry retry) {
    vector<GraphResponse> responses(requests.size());
    for (size_t first = 0; first < requests.size(); first += GRAPH_BATCH_LIMIT) {
        vector<size_t> pending {};
        for (size_t i = first; i < requests.size() && i < first + GRAPH_BATCH_LIMIT; i++) {
            pending.push_back(i);
        }
        for (int attempt = 0; attempt < GRAPH_REQUEST_ATTEMPTS && !pending.empty(); attempt++) {
            json batch = json::array();
            for (size_t i : pending) {
                json request = requests[i];
                request["id"] = to_string(i);
                batch.push_back(request);
            }
            GraphResponse resp = PerformGraphRequest("POST", MicrosoftGraphURL() + "/$batch", tokenProvider(),
                                                     json{{"requests", batch}}.dump(), "application/json", {}, retry);
            if (!resp.ok()) {
                for (size_t i : pending) {
                    responses[i] = resp;
                }
                break;
            }

            vector<size_t> throttled {};
            long retryAfter = 0;
            for (const auto & answer : JSONArray(resp.body, "responses")) {
                size_t i = (size_t)atoi(JSONString(answer, "id").c_str());
                if (find(pending.begin(), pending.end(), i) == pending.end()) {
                    continue;
                }
                GraphResponse & response = responses[i];
                response.status = JSONInt(answer, "status");
                response.body = JSONObject(answer, "body");
                response.retryAfter = ParseRetryAfter(_batchedHeaderValue(answer, "Retry-After"));
                if (_graphRetries(response.status, retry)) {
                    throttled.push_back(i);
                    retryAfter = (std::max)(retryAfter, response.retryAfter);
                }
            }
            pending = throttled;
            if (!pending.empty() && attempt + 1 < GRAPH_REQUEST_ATTEMPTS) {
                long wait = _graphRetryWait(retryAfter);
                spdlog::get("logger")->info("-- Graph throttled {} batched requests, retrying in {}s", pending.size(), wait);
                std::this_thread::sleep_for(std::chrono::seconds(wait));
            }
        }
    }
    return responses;
}

void ThrowForGraphResponse(const string & url, const GraphResponse & resp) {
    bool retryable = resp.status != 401 && resp.status != 403;
    throw SyncException("Invalid Response Code: " + to_string(resp.status),
                        url + " RETURNED " + GraphErrorDescription(resp), retryable);
}

void PaginateGraphCollection(const string & url, const std::function<string()> & tokenProvider, const std::function<void(json &)> & yieldPage) {
    string next = url;
    while (next != "") {
        GraphResponse resp = PerformGraphRequest("GET", next, tokenProvider());
        if (!resp.ok()) {
            ThrowForGraphResponse(next, resp);
        }
        json & page = resp.body;
        if (!page.count("value") || !page["value"].is_array()) {
            throw SyncException("invalid-graph-resp", next + " returned no value array", true);
        }
        yieldPage(page);
        next = JSONString(page, "@odata.nextLink");
    }
}

string GraphErrorDescription(const GraphResponse & resp) {
    const json & body = resp.body;
    if (body.is_object() && body.count("error") && body["error"].is_object()) {
        return JSONString(body["error"], "code") + ": " + JSONString(body["error"], "message");
    }
    string text = body.dump();
    if (text.length() > 512) {
        text = text.substr(0, 512) + "... (truncated)";
    }
    return "HTTP " + to_string(resp.status) + " " + text;
}

string GraphSentItemsId(const string & accessToken, GraphResponse * failure) {
    auto resp = PerformGraphRequest("GET", MicrosoftGraphURL() + "/me/mailFolders/sentitems?$select=id", accessToken);
    string id = resp.ok() ? JSONString(resp.body, "id") : "";
    if (id.empty() && failure != nullptr) {
        *failure = resp;
    }
    return id;
}

// HTTP helpers

string URLEscape(const string & value) {
    char * escaped = curl_easy_escape(nullptr, value.c_str(), (int)value.size());
    string result = escaped ? escaped : value;
    curl_free(escaped);
    return result;
}

// HTTP Protocol Quirk: Header Case Sensitivity
// While HTTP specifications (RFC 7230) require case-insensitive header handling, some
// server implementations or proxies treat headers as case-sensitive. The Python caldav
// library uses CaseInsensitiveDict for all header operations. We achieve the same effect
// by normalizing both the headers and search term to lowercase before matching.
string HTTPHeaderValue(const string& headers, const string& headerName) {
    string searchFor = headerName + ":";
    size_t pos = 0;

    // Case-insensitive search (handles server inconsistencies)
    string headersLower = headers;
    string searchLower = searchFor;
    transform(headersLower.begin(), headersLower.end(), headersLower.begin(), ::tolower);
    transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    pos = headersLower.find(searchLower);
    if (pos == string::npos) {
        return "";
    }

    // Find the value after the colon
    size_t valueStart = pos + searchFor.length();
    size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == string::npos) {
        valueEnd = headers.find("\n", valueStart);
    }
    if (valueEnd == string::npos) {
        valueEnd = headers.length();
    }

    string value = headers.substr(valueStart, valueEnd - valueStart);

    // Trim whitespace
    size_t start = value.find_first_not_of(" \t");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == string::npos) return "";
    return value.substr(start, end - start + 1);
}

#ifdef _MSC_VER
// Windows doesn't have strptime, provide a minimal implementation for HTTP-date parsing
static char* win32_strptime(const char* s, const char* format, struct tm* tm) {
    // Only supports the HTTP-date format: "%a, %d %b %Y %H:%M:%S"
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char weekday[4], month[4];
    int day, year, hour, min, sec;

    if (sscanf_s(s, "%3s, %d %3s %d %d:%d:%d",
                 weekday, (unsigned)sizeof(weekday),
                 &day,
                 month, (unsigned)sizeof(month),
                 &year, &hour, &min, &sec) != 7) {
        return nullptr;
    }

    int mon = -1;
    for (int i = 0; i < 12; i++) {
        if (_stricmp(month, months[i]) == 0) {
            mon = i;
            break;
        }
    }
    if (mon < 0) return nullptr;

    tm->tm_mday = day;
    tm->tm_mon = mon;
    tm->tm_year = year - 1900;
    tm->tm_hour = hour;
    tm->tm_min = min;
    tm->tm_sec = sec;

    // Return pointer past the parsed portion (approximate)
    return const_cast<char*>(s + strlen(s));
}
#define strptime win32_strptime
#define timegm _mkgmtime
#endif

// Parse Retry-After header value (RFC 7231)
// Supports both delay-seconds (integer) and HTTP-date formats
int ParseRetryAfter(const string& retryAfter) {
    if (retryAfter.empty()) {
        return 0;
    }

    // Try parsing as integer (delay-seconds format)
    try {
        int seconds = stoi(retryAfter);
        if (seconds > 0) {
            return seconds;
        }
    } catch (...) {
        // Not an integer, try parsing as HTTP-date
    }

    // Try parsing as HTTP-date (e.g., "Fri, 31 Dec 2024 23:59:59 GMT")
    struct tm tm = {};
    if (strptime(retryAfter.c_str(), "%a, %d %b %Y %H:%M:%S", &tm) != nullptr) {
        time_t targetTime = timegm(&tm);
        time_t now = time(nullptr);
        if (targetTime > now) {
            return static_cast<int>(targetTime - now);
        }
    }

    return 0;
}

// Shorthand methods for Identity Server

CURL * CreateIdentityRequest(string path, string method, const char * payloadChars) {
    string url { MailUtils::getEnvUTF8("IDENTITY_SERVER") + path };

    if (Identity::GetGlobal() == nullptr) {
        // Note: almost all the backend APIs require auth except for /api/resolve-dav-hosts
        // and calls to this method should be avoided if no identity is present.
        return CreateJSONRequest(url, method, "", payloadChars);
    }

    string plain = Identity::GetGlobal()->token() + ":";
    string encoded = MailUtils::toBase64(plain.c_str(), strlen(plain.c_str()));
    string authorization = "Basic " + encoded;
    return CreateJSONRequest(url, method, authorization, payloadChars);
}

const json PerformIdentityRequest(string path, string method, const json & payload) {
    string payloadString = payload.dump();
    return PerformJSONRequest(CreateIdentityRequest(path, method, payloadString.c_str()));
}
