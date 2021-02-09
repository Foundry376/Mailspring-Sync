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
#include <mutex>
#include <condition_variable>
#include "mailsync/models/mail_model.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

#define DELTA_TYPE_METADATA_EXPIRATION  "metadata-expiration"
#define DELTA_TYPE_PERSIST              "persist"
#define DELTA_TYPE_UNPERSIST            "unpersist"

class DeltaStreamItem {
public:
    string type;
    vector<json> modelJSONs;
    string modelClass;
    map<string, size_t> idIndexes;

    DeltaStreamItem(string type, string modelClass, vector<json> modelJSONs);
    DeltaStreamItem(string type, vector<shared_ptr<MailModel>> & models);
    DeltaStreamItem(string type, MailModel * model);

    bool concatenate(const DeltaStreamItem & other);
    void upsertModelJSON(const json & modelJSON);
    string dump() const;
};

class DeltaStream  {
    mutex bufferMtx;
    map<string, vector<DeltaStreamItem>> buffer;

    bool scheduled;
    bool connectionError;
    std::chrono::system_clock::time_point scheduledTime;
    std::mutex bufferFlushMtx;
    std::condition_variable bufferFlushCv;

public:
    DeltaStream();
    ~DeltaStream();

    json waitForJSON();

    void flushBuffer();
    void flushWithin(int ms);

    void queueDeltaForDelivery(DeltaStreamItem item);

    void emit(DeltaStreamItem item, int maxDeliveryDelay);
    void emit(vector<DeltaStreamItem> items, int maxDeliveryDelay);

    void beginConnectionError(string accountId);
    void endConnectionError(string accountId);
};


shared_ptr<DeltaStream> SharedDeltaStream();

#endif /* DeltaStream_hpp */
