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

CURL * CreateAccountsRequest(shared_ptr<Account> account, string path, string method, const json & payload) {
    CURL * curl_handle = curl_easy_init();
    string username = account->cloudToken();
    string password = Identity::GetGlobal()->token();
    
    const char * payloadChars = nullptr;
    const char * payloadType = string("application/json").c_str();
    
    string url { string(getenv("ACCOUNTS_SERVER")) + path };
    url.replace(url.find("://"), 3, "://" + username + ":" + password + "@");
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    if (method == "POST") {
        curl_httppost data;
        payloadChars = payload.dump().c_str();
        data.next = nullptr;
        data.name = nullptr;
        data.namelength = 0;
        data.contents = (char *)payloadChars;
        data.contentslength = strlen(payloadChars);
        data.buffer = nullptr;
        data.bufferlength = 0;
        data.contenttype = (char *)payloadType;
        data.contentheader = nullptr;
        data.more = nullptr;
        data.flags = CURL_HTTPPOST_PTRCONTENTS;
        curl_easy_setopt(curl_handle, CURLOPT_HTTPPOST, data);
    }
    return curl_handle;
}

const json MakeAccountsRequest(shared_ptr<Account> account, string path, string method, const json & payload) {
    CURL * curl_handle = CreateAccountsRequest(account, path, method, payload);
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

