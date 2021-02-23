/** SyncException [MailSync]
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

#ifndef SyncException_hpp
#define SyncException_hpp

#include <stdio.h>
#include "MailCore/MailCore.h"
#include <curl/curl.h>
#include "nlohmann/json.hpp"
#include "mailsync/generic_exception.hpp"






class SyncException : public GenericException {
    bool retryable = false;
    bool offline = false;

public:
    SyncException(std::string key, std::string di, bool retryable);
    SyncException(CURLcode c, std::string di);
    SyncException(mailcore::ErrorCode c, std::string di);
    std::string key;
    std::string debuginfo;
    bool isRetryable();
    bool isOffline();
    nlohmann::json toJSON();
};


#endif /* SyncException_hpp */
