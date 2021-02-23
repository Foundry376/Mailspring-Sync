/** NetworkRequestUtils [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NetworkRequestUtils_hpp
#define NetworkRequestUtils_hpp

#include <curl/curl.h>
#include <stdio.h>
#include "nlohmann/json.hpp"

class Account;




size_t _onAppendToString(void *contents, size_t length, size_t nmemb, void *userp);

CURL * CreateJSONRequest(std::string url, std::string method = "GET", std::string authorization = "", const char * payloadChars = nullptr);
CURL * CreateCalDavRequest(std::string url, std::string method = "GET", const char * payloadChars = nullptr);

const nlohmann::json MakeOAuthRefreshRequest(std::string provider, std::string clientId, std::string refreshToken);

const std::string PerformRequest(CURL * curl_handle);
const nlohmann::json PerformJSONRequest(CURL * curl_handle);

const std::string PerformExpectedRedirect(std::string url);

void ValidateRequestResp(CURLcode res, CURL * curl_handle, std::string resp);

// Shorthand methods for Identity Server

CURL * CreateIdentityRequest(std::string path, std::string method = "GET", const char * payloadChars = nullptr);
const nlohmann::json PerformIdentityRequest(std::string path, std::string method = "GET", const nlohmann::json & payload = nullptr);

#endif /* NetworkRequestUtils_hpp */
