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
#include "MailModel.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using json = nlohmann::json;
using namespace std;

class DeltaStream  {
    mutex mtx_;
    map<string, vector<json>> buffer;
    bool scheduled;
    
public:
    DeltaStream();
    ~DeltaStream();

    void sendJSON(const json & msg);
    json waitForJSON();

    void flushBuffer();
    void bufferMessage(string klass, string type, MailModel * model);

    void didPersistModel(MailModel * model, clock_t maxDeliveryLatency);
    void didUnpersistModel(MailModel * model, clock_t maxDeliveryLatency);
};

#endif /* DeltaStream_hpp */
