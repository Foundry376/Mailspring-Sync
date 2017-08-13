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
    std::copy((char*)contents, (char*)contents+newLength, buffer->begin() + oldLength);
    
    return real_size;
}

CURL * CreateAccountsRequest(shared_ptr<Account> account, string path, string method, const char * payloadChars) {
    CURL * curl_handle = curl_easy_init();
    string username = account->cloudToken();
    string password = Identity::GetGlobal()->token();
    
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

const json MakeAccountsRequest(shared_ptr<Account> account, string path, string method, const json & payload) {
    const char * payloadChars = payload.dump().c_str();
    CURL * curl_handle = CreateAccountsRequest(account, path, method, payloadChars);
    string result;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);
    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        throw SyncException(res, path);
    }
    
    const json resultJSON { json::parse(result) };
    curl_easy_cleanup(curl_handle);
    return resultJSON;
}

