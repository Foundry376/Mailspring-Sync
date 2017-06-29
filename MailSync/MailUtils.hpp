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
#include "Label.hpp"

using json = nlohmann::json;
using namespace std;
using namespace mailcore;

class MailUtils {

private:
    static int compareEmails(void * a, void * b, void * context);

public:
    static std::string toBase64(const unsigned char *src, size_t len);

    static json merge(const json &a, const json &b);
    static json contactJSONFromAddress(Address * addr);
    static string contactKeyForEmail(string email);

    static string timestampForTime(time_t time);
  
    static vector<uint32_t> uidsOfArray(Array * array);
    static vector<uint32_t> uidsOfIndexSet(IndexSet * set);
    static vector<string> messageIdsOfArray(Array * array);

    static string roleForFolder(IMAPFolder * folder);
    static string idRandomlyGenerated();
    static string idForMessage(IMAPMessage * msg);
    static string idForFolder(string folderPath);
    static string idForDraftHeaderMessageId(string headerMessageId);
    
    static shared_ptr<Label> labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> & allLabels);

    static string qmarks(size_t count);
    static string qmarkSets(size_t count, size_t perSet);
};

#endif /* MailUtils_hpp */
