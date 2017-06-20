//
//  main.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include <iostream>
#include <string>
#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "CommStream.hpp"
#include "SyncWorker.hpp"


using json = nlohmann::json;


int main(int argc, const char * argv[]) {
    SyncWorker * worker = new SyncWorker();
    
    spdlog::set_pattern("%l: [%L] %v");
    
    while(true) {
        worker->syncNow();
        sleep(60);
    }
    return 0;
}
