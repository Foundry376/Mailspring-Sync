//
//  DeltaStream.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef DeltaStream_hpp
#define DeltaStream_hpp

#include <stdio.h>
#include <mutex>
#include <condition_variable>
#include "MailModel.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using json = nlohmann::json;
using namespace std;

class DeltaStream  {
    mutex bufferMtx;
    map<string, vector<json>> buffer;

    bool scheduled;
    std::chrono::system_clock::time_point scheduledTime;
    std::mutex bufferFlushMtx;
    std::condition_variable bufferFlushCv;

public:
    DeltaStream();
    ~DeltaStream();

    void sendJSON(const json & msg);
    json waitForJSON();

    void flushBuffer();
    void flushWithin(int ms);
    
    void bufferMessage(string klass, string type, MailModel * model);

    void didPersistModel(MailModel * model, int maxDeliveryDelay);
    void didUnpersistModel(MailModel * model, int maxDeliveryDelay);
};


shared_ptr<DeltaStream> SharedDeltaStream();

#endif /* DeltaStream_hpp */
