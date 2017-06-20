//
//  MailUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef MailUtils_hpp
#define MailUtils_hpp

#include <iostream>
#include <string>
#include <stdio.h>
#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"

using json = nlohmann::json;


class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static json merge(const json &a, const json &b);
    static std::string timestampForTime(time_t time);
  
    static std::vector<uint32_t> uidsOfArray(mailcore::Array * array);
    static std::vector<uint32_t> uidsOfIndexSet(mailcore::IndexSet * set);

    static std::string roleForFolder(mailcore::IMAPFolder * folder);
    static std::string idForFolder(mailcore::IMAPFolder * folder);
    static std::string idForMessage(mailcore::IMAPMessage * msg);
};

#endif /* MailUtils_hpp */
