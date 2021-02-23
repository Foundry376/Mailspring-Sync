/** MetadataWorker [MailSync]
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

#ifndef MetadataWorker_hpp
#define MetadataWorker_hpp

#include "mailsync/models/account.hpp"
#include "mailsync/models/identity.hpp"
#include "mailsync/mail_store.hpp"

#include <stdio.h>

#include <string>
#include <vector>

#include "spdlog/spdlog.h"



class MetadataWorker {
    MailStore * store;
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<Account> account;

    std::string deltasBuffer;
    std::string deltasCursor;
    int backoffStep;

public:
    MetadataWorker(std::shared_ptr<Account> account);

    void run();

    bool fetchMetadata(int page);
    void fetchDeltaCursor();
    void fetchDeltasBlocking();

    void setDeltaCursor(std::string cursor);

    void onDeltaData(void * contents, size_t bytes);
    void onDelta(const nlohmann::json & delta);

    void applyMetadataJSON(const nlohmann::json & metadata);
};

#endif /* MetadataWorker_hpp */
