//
//  NetworkRequestUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 8/6/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef NetworkRequestUtils_hpp
#define NetworkRequestUtils_hpp

#include <curl/curl.h>
#include <stdio.h>
#include "json.hpp"

class Account;

using namespace nlohmann;
using namespace std;

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp);

CURL * CreateRequest(string server, string username, string password, string path, string method = "GET", const char * payloadChars = nullptr);
CURL * CreateIdentityRequest(string path, string method = "GET", const char * payloadChars = nullptr);

const json MakeRequest(string server, string username, string password, string path, string method = "GET", const json & payload = nullptr);
const json MakeIdentityRequest(string path, string method = "GET", const json & payload = nullptr);

void ValidateRequestResp(CURLcode res, CURL * curl_handle, string path);

#endif /* NetworkRequestUtils_hpp */
