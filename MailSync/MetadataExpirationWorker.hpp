//
//  MetadataExpirationManager.hpp
//  MailSync
//
//  Created by Ben Gotow on 9/11/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MetadataExpirationWorker_hpp
#define MetadataExpirationWorker_hpp

#include <stdio.h>
#include <mutex>
#include <condition_variable>

#include "MailModel.hpp"
#include "MailStore.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;


class MetadataExpirationWorker  {
    string _accountId;
    std::chrono::system_clock::time_point _wakeTime;
    std::condition_variable _wakeCv;
    std::mutex _wakeMtx;

public:
    MetadataExpirationWorker(string accountId);
    
    void isSavingMetadataWithExpiration(long e);
    void run();
};


MetadataExpirationWorker * MetadataExpirationWorkerForAccountId(string aid);
void WakeAllMetadataExpirationWorkers();

#endif /* MetadataExpirationWorker_hpp */
