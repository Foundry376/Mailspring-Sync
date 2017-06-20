//
//  MailStore.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef MailStore_hpp
#define MailStore_hpp

#include <stdio.h>
#include <vector>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"

#include "Folder.hpp"
#include "Message.hpp"

using json = nlohmann::json;

struct MessageAttributes {
    uint32_t uid;
    int version;
    bool seen;
    bool flagged;
};

// Base class
class MailStoreObserver {
public:
    virtual void didPersistModel(MailModel * model) = 0;
    virtual void didUnpersistModel(MailModel * model) = 0;
};


class MailStore {
    SQLite::Database _db;
    std::vector<MailStoreObserver*> _observers;
    
public:
    MailStore();
    
    std::map<std::string, Folder *> getFoldersById();
    
    void insertMessage(mailcore::IMAPMessage * mMsg, Folder & folder);
        
    void save(MailModel * model);

    void remove(MailModel * model);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, int depth);

    std::map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);
    
    std::map<uint32_t, Message *> fetchMessagesWithUIDs(std::vector<uint32_t> & uids, Folder & folder);
    
    void updateMessageAttributes(MessageAttributes local, mailcore::IMAPMessage * remoteMsg, Folder & folder);
    
    void deleteMessagesWithUIDs(std::vector<uint32_t> & uids, Folder & folder);

    void addObserver(MailStoreObserver * observer);
    
private:
    void notify(std::string type);
};


#endif /* MailStore_hpp */
