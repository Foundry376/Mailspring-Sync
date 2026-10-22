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

#if __APPLE__
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <strings.h>
#endif

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

const json MakeOAuthRefreshRequest(string provider, string clientId, string refreshToken) {
    CURL * curl_handle = CreateCurlHandle();
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
