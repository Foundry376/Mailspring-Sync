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

const json MakeGmailOAuthRequest(string clientId, string refreshToken) {
    CURL * curl_handle = curl_easy_init();
    const char * url = "https://www.googleapis.com/oauth2/v4/token";
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    
    auto c = curl_easy_escape(curl_handle, clientId.c_str(), 0);
    auto r = curl_easy_escape(curl_handle, refreshToken.c_str(), 0);
    string payload = "grant_type=refresh_token&client_id=" + string(c) + "&refresh_token=" + string(r);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
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

    if (authorization != "") {
        headers = curl_slist_append(headers, ("Authorization: " + authorization).c_str());
    }
    if (method == "POST") {
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);


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
    ValidateRequestResp(res, curl_handle);
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
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 301 && http_code != 302) {
        return "";
    }
    char * redirect;
    if (curl_easy_getinfo(curl_handle, CURLINFO_REDIRECT_URL, &redirect) != CURLE_OK) {
        return "";
    }

    return redirect;
}


const json PerformJSONRequest(CURL * curl_handle) {
    string result = PerformRequest(curl_handle);
    json resultJSON = nullptr;
    try {
        resultJSON = json::parse(result);
    } catch (json::exception & ex) {
        resultJSON = {{"text", result}};
    }
    curl_easy_cleanup(curl_handle);
    return resultJSON;
}

void ValidateRequestResp(CURLcode res, CURL * curl_handle) {
    char * url;
    if (curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url) != CURLE_OK) {
        throw SyncException(res, "Unable to get URL");
    }

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl_handle);
        throw SyncException(res, string(url));
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code > 209) {
        curl_easy_cleanup(curl_handle);
        bool retryable = ((http_code != 403) && (http_code != 401));
        throw SyncException("Invalid Response Code: " + to_string(http_code), string(url), retryable);
    }
}

// Shorthand methods for Identity Server

CURL * CreateIdentityRequest(string path, string method, const char * payloadChars) {
    string url { MailUtils::getEnvUTF8("IDENTITY_SERVER") + path };
    string plain = Identity::GetGlobal()->token() + ":";
    string encoded = MailUtils::toBase64(plain.c_str(), strlen(plain.c_str()));
    string authorization = "Basic " + encoded;
    return CreateJSONRequest(url, method, authorization, payloadChars);
}

const json PerformIdentityRequest(string path, string method, const json & payload) {
    string payloadString = payload.dump();
    return PerformJSONRequest(CreateIdentityRequest(path, method, payloadString.c_str()));
}
