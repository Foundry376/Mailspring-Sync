/** DeltaStream [MailSync]
 *
 * The DeltaStream is a singleton class that manages broadcasting database
 * events on stdout. It implements various buffering and "repeated save
 * collapsing" logic to avoid spamming the Mailspring client with unnecessary
 * events or waking it too often.
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

#ifndef DeltaStream_hpp
#define DeltaStream_hpp

#include <stdio.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "mailsync/models/mail_model.hpp"

#define DELTA_TYPE_METADATA_EXPIRATION  "metadata-expiration"
#define DELTA_TYPE_PERSIST              "persist"
#define DELTA_TYPE_UNPERSIST            "unpersist"

class DeltaStreamItem {
public:
    std::string type;
    std::vector<nlohmann::json> modelJSONs;
    std::string modelClass;
    std::map<std::string, size_t> idIndexes;

    DeltaStreamItem(std::string type, std::string modelClass, std::vector<nlohmann::json> modelJSONs);
    DeltaStreamItem(std::string type, std::vector<std::shared_ptr<MailModel>> & models);
    DeltaStreamItem(std::string type, MailModel * model);

    bool concatenate(const DeltaStreamItem & other);
    void upsertModelJSON(const nlohmann::json & modelJSON);
    std::string dump() const;
};

class DeltaStream  {
    std::mutex bufferMtx;
    std::map<std::string, std::vector<DeltaStreamItem>> buffer;

    bool scheduled;
    bool connectionError;
    std::chrono::system_clock::time_point scheduledTime;
    std::mutex bufferFlushMtx;
    std::condition_variable bufferFlushCv;

public:
    DeltaStream();
    ~DeltaStream();

    nlohmann::json waitForJSON();

    void flushBuffer();
    void flushWithin(int ms);

    void queueDeltaForDelivery(DeltaStreamItem item);

    void emit(DeltaStreamItem item, int maxDeliveryDelay);
    void emit(std::vector<DeltaStreamItem> items, int maxDeliveryDelay);

    void beginConnectionError(std::string accountId);
    void endConnectionError(std::string accountId);
};


std::shared_ptr<DeltaStream> SharedDeltaStream();

#endif /* DeltaStream_hpp */
