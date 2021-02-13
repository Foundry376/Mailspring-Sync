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

using namespace std;

class MetadataWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    string deltasBuffer;
    string deltasCursor;
    int backoffStep;

public:
    MetadataWorker(shared_ptr<Account> account);

    void run();

    bool fetchMetadata(int page);
    void fetchDeltaCursor();
    void fetchDeltasBlocking();

    void setDeltaCursor(string cursor);

    void onDeltaData(void * contents, size_t bytes);
    void onDelta(const json & delta);

    void applyMetadataJSON(const json & metadata);
};

#endif /* MetadataWorker_hpp */
