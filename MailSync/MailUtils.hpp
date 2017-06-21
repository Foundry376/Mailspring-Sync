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
using namespace std;
using namespace mailcore;

class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static json merge(const json &a, const json &b);
    static string timestampForTime(time_t time);
  
    static vector<uint32_t> uidsOfArray(Array * array);
    static vector<uint32_t> uidsOfIndexSet(IndexSet * set);

    static string roleForFolder(IMAPFolder * folder);
    static string idForFolder(IMAPFolder * folder);
    static string idForMessage(IMAPMessage * msg);
};

#endif /* MailUtils_hpp */
