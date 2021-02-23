/** XOAuth2TokenManager [MailSync]
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

#ifndef XOAUTH2TokenManager_hpp
#define XOAUTH2TokenManager_hpp

#include <stdio.h>
#include <mutex>
#include <condition_variable>
#include "mailsync/models/account.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"




struct XOAuth2Parts {
    std::string username;
    std::string accessToken;
    time_t expiryDate;
};

class XOAuth2TokenManager  {
    std::map<std::string, XOAuth2Parts> _cache;
    std::mutex _cacheLock;

public:
    XOAuth2TokenManager();
    ~XOAuth2TokenManager();
    XOAuth2Parts partsForAccount(std::shared_ptr<Account> account);
};


std::shared_ptr<XOAuth2TokenManager> SharedXOAuth2TokenManager();


#endif /* XOAUTH2TokenManager_hpp */
