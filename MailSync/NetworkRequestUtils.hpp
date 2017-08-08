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

using json = nlohmann::json;
using namespace std;

size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp);

CURL * CreateAccountsRequest(shared_ptr<Account> account, string path, string method = "GET", const json & payload = nullptr);

const json MakeAccountsRequest(shared_ptr<Account> account, string path, string method = "GET", const json & payload = nullptr);

#endif /* NetworkRequestUtils_hpp */
