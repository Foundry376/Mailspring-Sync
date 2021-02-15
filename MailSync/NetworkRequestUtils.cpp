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

string FindLinuxCertsBundle() {
#ifdef __linux__
    std::string certificatePaths[] = {
        // Debian, Ubuntu, Arch: maintained by update-ca-certificates
        "/etc/ssl/certs/ca-certificates.crt",
        // Red Hat 5+, Fedora, Centos
        "/etc/pki/tls/certs/ca-bundle.crt",
        // Red Hat 4
        "/usr/share/ssl/certs/ca-bundle.crt",
        // FreeBSD (security/ca-root-nss package)
        "/usr/local/share/certs/ca-root-nss.crt",
        // FreeBSD (deprecated security/ca-root package, removed 2008)
        "/usr/local/share/certs/ca-root.crt",
        // FreeBSD (optional symlink)
        // OpenBSD
        "/etc/ssl/cert.pem",
        // OpenSUSE
        "/etc/ssl/ca-bundle.pem",
    };
    for (const auto path : certificatePaths) {
        struct stat buffer;
        if (stat (path.c_str(), &buffer) == 0) {
            return path;
        }
    }
#endif
    return "";
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
        : "";
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    
    auto c = curl_easy_escape(curl_handle, clientId.c_str(), 0);
    auto r = curl_easy_escape(curl_handle, refreshToken.c_str(), 0);
    string payload = "grant_type=refresh_token&client_id=" + string(c) + "&refresh_token=" + string(r);
    if (provider == "office365") {
        // workaround the fact that Microsoft's OAUTH flow allows you to authorize many scopes, but you
        // have to get a separate token for outlook (email + IMAP) and contacts / calendar / Microsoft Graph APIs
        // separately. The same refresh token will give you access tokens, but the access tokens are different.
        payload += "&scope=https%3A%2F%2Foutlook.office365.com%2FIMAP.AccessAsUser.All%20https%3A%2F%2Foutlook.office365.com%2FSMTP.Send";
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (provider == "office365") {
        // workaround "AADSTS9002327: Tokens issued for the 'Single-Page Application' client-type
        // may only be redeemed via cross-origin requests"
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_4) AppleWebKit/537.36 (KHTML, like Gecko) Mailspring/1.7.8 Chrome/69.0.3497.128 Electron/4.2.12 Safari/537.36");
        headers = curl_slist_append(headers, "Origin: null");

    }
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payload.c_str());
    
    return PerformJSONRequest(curl_handle);
}

CURL * CreateJSONRequest(string url, string method, string authorization, const char * payloadChars) {
    CURL * curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);

    struct curl_slist *headers = NULL;

    headers = curl_slist_append(headers, "Accept: application/json");

    if (authorization != "") {
        headers = curl_slist_append(headers, ("Authorization: " + authorization).c_str());
    }
    if (payloadChars != nullptr && strlen(payloadChars) > 0) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());


    // Ensure /all/ curl code paths run this code for RHEL 7.6 and other linux distros
    string explicitCertsBundlePath = FindLinuxCertsBundle();
    if (explicitCertsBundlePath != "") {
        curl_easy_setopt(curl_handle, CURLOPT_CAINFO, explicitCertsBundlePath.c_str());
    }

    return curl_handle;
}

const string PerformRequest(CURL * curl_handle) {
    string explicitCertsBundlePath = FindLinuxCertsBundle();
    if (explicitCertsBundlePath != "") {
        curl_easy_setopt(curl_handle, CURLOPT_CAINFO, explicitCertsBundlePath.c_str());
    }
    
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

    string explicitCertsBundlePath = FindLinuxCertsBundle();
    if (explicitCertsBundlePath != "") {
        curl_easy_setopt(curl_handle, CURLOPT_CAINFO, explicitCertsBundlePath.c_str());
    }
    
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
    curl_easy_cleanup(curl_handle);
    return resultJSON;
}

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string resp) {
    char * _url = nullptr;
    if (curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &_url) != CURLE_OK || _url == nullptr) {
        throw SyncException(res, "Unable to get URL");
    }
    string url { _url };

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl_handle);
        throw SyncException(res, url);
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code > 209) {
        curl_easy_cleanup(curl_handle); // note: cleans up _url;
        
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
