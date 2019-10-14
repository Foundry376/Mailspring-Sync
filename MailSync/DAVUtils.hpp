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

#include "Contact.hpp"
#include "VCard.hpp"

static string X_VCARD3_KIND = "X-ADDRESSBOOKSERVER-KIND";
static string X_VCARD3_MEMBER = "X-ADDRESSBOOKSERVER-MEMBER";
static string CARDDAV_SYNC_SOURCE = "carddav";

class DAVUtils {

public:

static void addMembersToGroupCard(shared_ptr<VCard> card, vector<shared_ptr<Contact>> contacts);
static void removeMembersFromGroupCard(shared_ptr<VCard> card, vector<shared_ptr<Contact>> contacts);
static bool isGroupCard(shared_ptr<VCard> card);
    
};

#endif /* DAVUtils_hpp */
