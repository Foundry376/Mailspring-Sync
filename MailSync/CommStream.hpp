//
//  CommStream.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef CommStream_hpp
#define CommStream_hpp

#include <stdio.h>
#include <mutex>
#include "json.hpp"
#include "MailStore.hpp"
#include "spdlog/spdlog.h"

using json = nlohmann::json;

class CommStream : public MailStoreObserver {
    mutex mtx_;
public:
    CommStream();
    ~CommStream();

    void sendJSON(json & msg);
    json waitForJSON();

    void didPersistModel(MailModel * model);
    void didUnpersistModel(MailModel * model);
};

#endif /* CommStream_hpp */
