//
//  NetworkRequestUtils.cpp
//  MailSync
//
//  Created by Ben Gotow on 8/6/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Identity.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "Account.hpp"

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp) {
    string * buffer = (string *)userp;
    size_t real_size = length * nmemb;
    
    size_t oldLength = buffer->size();
    size_t newLength = oldLength + real_size;
    
    buffer->resize(newLength);
    std::copy((char*)contents, (char*)contents+length, buffer->begin() + oldLength);
    
    return real_size;
}

CURL * CreateRequest(string server, string username, string password, string path, string method, const char * payloadChars) {
    CURL * curl_handle = curl_easy_init();
    string url { string(getenv("ACCOUNTS_SERVER")) + path };
    url.replace(url.find("://"), 3, "://" + username + ":" + password + "@");
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    
    if (method == "POST") {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    }
    return curl_handle;
}

    
CURL * CreateAccountsRequest(shared_ptr<Account> account, string path, string method, const char * payloadChars) {
    return CreateRequest("ACCOUNTS_SERVER", account->cloudToken(), Identity::GetGlobal()->token(), path, method, payloadChars);
}

CURL * CreateIdentityRequest(string path, string method, const char * payloadChars) {
    return CreateRequest("IDENTITY_SERVER", Identity::GetGlobal()->token(), "", path, method, payloadChars);
}

const json MakeRequest(string server, string username, string password, string path, string method, const json & payload) {
    string payloadString = payload.dump();
    CURL * curl_handle = CreateRequest(server, username, password, path, method, payloadString.c_str());
    string result;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    CURLcode res = curl_easy_perform(curl_handle);
    ValidateRequestResp(res, curl_handle, path);
    
    json resultJSON = nullptr;
    try {
        resultJSON = json::parse(result);
    } catch (std::invalid_argument & ex) {
        resultJSON = {"result", result};
    }
    curl_easy_cleanup(curl_handle);
    return resultJSON;
}

const json MakeAccountsRequest(shared_ptr<Account> account, string path, string method, const json & payload) {
    return MakeRequest("ACCOUNTS_SERVER", account->cloudToken(), Identity::GetGlobal()->token(), path, method, payload);
}

const json MakeIdentityRequest(string path, string method, const json & payload) {
    return MakeRequest("IDENTITY_SERVER", Identity::GetGlobal()->token(), "", path, method, payload);
}

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string path) {
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl_handle);
        throw SyncException(res, path);
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200) {
        curl_easy_cleanup(curl_handle);
        bool retryable = ((http_code != 403) && (http_code != 401));
        throw SyncException("Invalid Response Code: " + to_string(http_code), path, retryable);
    }
}


