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

CommStream * stream = nullptr;
SyncWorker * bgWorker = nullptr;
SyncWorker * fgWorker = nullptr;

void runBackgroundSyncWorker() {
    
    while(true) {
        // run in a hard loop until it returns false, indicating continuation
        // is not necessary. Then sync and sleep for a bit. Interval can be long
        // because we're idling in another thread.
        while(bgWorker->syncNow()) {}
        sleep(120);
    }
}

void runForegroundSyncWorker() {
    fgWorker->idleOnInbox();
}

void onClientMessage() {
    
}

int main(int argc, const char * argv[]) {
    spdlog::set_pattern("%l: [%L] %v");

    stream = new CommStream((char *)"/tmp/cmail.sock");
    bgWorker = new SyncWorker("bg", stream);
    fgWorker = new SyncWorker("fg", stream);

//    std::thread t1(runBackgroundSyncWorker);
    
    std::thread t2(runForegroundSyncWorker);
    
    while(true) {
        json packet = stream->waitForJSON();
        fgWorker->idleInterrupt();
    }

    //    t1.join();
    t2.join();
    
    return 0;
}
