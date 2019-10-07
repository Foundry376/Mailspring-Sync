//
//  DAVUtils.hpp
//  mailsync
//
//  Created by Ben Gotow on 10/6/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//

#ifndef DAVUtils_hpp
#define DAVUtils_hpp

#include <stdio.h>
#include <string>
#include <vector>
#include <belr/belr.h>
#include <belcard/belcard.hpp>

#include "Contact.hpp"

static string X_VCARD3_KIND = "X-ADDRESSBOOKSERVER-KIND";
static string X_VCARD3_MEMBER = "X-ADDRESSBOOKSERVER-MEMBER";
static string CARDDAV_SYNC_SOURCE = "carddav";

using namespace belcard;

class DAVUtils {

public:
shared_ptr<BelCardProperty> belPropWithKeyValue(list<shared_ptr<BelCardProperty>> props, string value);

static void addMembersToGroupCard(shared_ptr<BelCard> card, vector<shared_ptr<Contact>> contacts);
static void removeMembersFromGroupCard(shared_ptr<BelCard> card, vector<shared_ptr<Contact>> contacts);

static bool isGroupCard(shared_ptr<BelCard> card);
static vector<string> srvRecordsForDomain(string domain);
};

#endif /* DAVUtils_hpp */
