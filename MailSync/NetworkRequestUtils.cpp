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

// Structure to hold curl request data that needs cleanup
// Stored in CURLOPT_PRIVATE to enable proper cleanup of headers
struct CurlRequestData {
    struct curl_slist *headers;

    CurlRequestData() : headers(nullptr) {}
};

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

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp) {
    string * buffer = (string *)userp;
    size_t real_size = length * nmemb;
    
    size_t oldLength = buffer->size();
    size_t newLength = oldLength + real_size;
    
    buffer->resize(newLength);
    std::copy((char*)contents, (char*)contents+real_size, buffer->begin() + oldLength);
    
    return real_size;
}

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken) {
    CURL * curl_handle = curl_easy_init();
    const char * url =
          provider == "gmail" ? "https://www.googleapis.com/oauth2/v4/token"
        : provider == "office365" ? "https://login.microsoftonline.com/common/oauth2/v2.0/token"
        : provider == "outlook" ? "https://login.microsoftonline.com/common/oauth2/v2.0/token"
        : "";
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);

    char * c = curl_easy_escape(curl_handle, clientId.c_str(), 0);
    char * r = curl_easy_escape(curl_handle, refreshToken.c_str(), 0);
    string payload = "grant_type=refresh_token&client_id=" + string(c) + "&refresh_token=" + string(r);
    curl_free(c);
    curl_free(r);

    if (provider == "office365" || provider == "outlook") {
        // workaround the fact that Microsoft's OAUTH flow allows you to authorize many scopes, but you
        // have to get a separate token for outlook (email + IMAP) and contacts / calendar / Microsoft Graph APIs
        // separately. The same refresh token will give you access tokens, but the access tokens are different.
        payload += "&scope=https%3A%2F%2Foutlook.office.com%2FIMAP.AccessAsUser.All%20https%3A%2F%2Foutlook.office.com%2FSMTP.Send";
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
    CURL * curl_handle = curl_easy_init();
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
    CURL * curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10);

    string result;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    curl_easy_perform(curl_handle);

    long http_code = 0;
    string redirect = "";
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code == 301 || http_code == 302) {
        char * _redirect = nullptr;
        if (curl_easy_getinfo(curl_handle, CURLINFO_REDIRECT_URL, &_redirect) == CURLE_OK && _redirect != nullptr) {
            redirect = string(_redirect);
        }
    }
    curl_easy_cleanup(curl_handle);
    return redirect;
}


const json PerformJSONRequest(CURL * curl_handle) {
    string result = PerformRequest(curl_handle);
    json resultJSON = nullptr;
    try {
        resultJSON = json::parse(result);
    } catch (json::exception &) {
        resultJSON = {{"text", result}};
    }
    CleanupCurlRequest(curl_handle);
    return resultJSON;
}

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string resp) {
    char * _url = nullptr;
    if (curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &_url) != CURLE_OK || _url == nullptr) {
        throw SyncException(res, "Unable to get URL");
    }
    string url { _url };

    if (res != CURLE_OK) {
        CleanupCurlRequest(curl_handle);
        throw SyncException(res, url);
    }

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
