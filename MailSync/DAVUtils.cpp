//
//  DAVUtils.cpp
//  mailsync
//
//  Created by Ben Gotow on 10/6/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//

#include "DAVUtils.hpp"

using namespace std;


shared_ptr<BelCardProperty> DAVUtils::belPropWithKeyValue(list<shared_ptr<BelCardProperty>> props, string value) {
    for (auto prop : props) {
        if (prop->getValue() == value) {
            return prop;
        }
    }
    return nullptr;
}

void DAVUtils::addMembersToGroupCard(shared_ptr<BelCard> card, vector<shared_ptr<Contact>> contacts) {
    for (auto contact : contacts) {
        string uuid = "urn:uuid:" + contact->id();
        bool found = false;

        // vcard4
        if (card->getKind() != nullptr) {
            for (auto prop : card->getMembers()) {
                if (prop->getValue() == uuid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                auto prop = make_shared<belcard::BelCardMember>();
                prop->setValue(uuid);
                card->addMember(prop);
            }

        // vcard3 / icloud
        } else  {
            for (auto prop : card->getExtendedProperties()) {
                if (prop->getName() == X_VCARD3_MEMBER && prop->getValue() == uuid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                auto prop = make_shared<belcard::BelCardProperty>();
                prop->setName(X_VCARD3_MEMBER);
                prop->setValue(uuid);
                card->addExtendedProperty(prop);
            }
        }
    }
}


void DAVUtils::removeMembersFromGroupCard(shared_ptr<BelCard> card, vector<shared_ptr<Contact>> contacts) {
    for (auto contact : contacts) {
        string uuid = "urn:uuid:" + contact->id();

        // vcard4
        if (card->getKind() != nullptr) {
            for (auto prop : card->getMembers()) {
                if (prop->getValue() == uuid) {
                    card->removeMember(prop);
                    break;
                }
            }

        // vcard3 / icloud
        } else {
            for (auto prop : card->getExtendedProperties()) {
                if (prop->getName() == X_VCARD3_MEMBER && prop->getValue() == uuid) {
                    card->removeExtendedProperty(prop);
                    break;
                }
            }
        }
    }
}

bool DAVUtils::isGroupCard(shared_ptr<BelCard> card) {
    if (card->getKind() && card->getKind()->getValue() == "group") {
        return true; //vcard4
    }
    for (auto prop : card->getExtendedProperties()) {
        if (prop->getName() == X_VCARD3_KIND && prop->getValue() == "group") {
            return true; // vcard3 / iCloud
        }
    }
    return false;
}
