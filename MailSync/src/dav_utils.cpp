#include "mailsync/dav_utils.hpp"


using namespace std;


void DAVUtils::addMembersToGroupCard(shared_ptr<VCard> card, vector<shared_ptr<Contact>> contacts) {
    for (auto contact : contacts) {
        string uuid = "urn:uuid:" + contact->id();
        bool found = false;

        // vcard4
        if (card->getVersion()->getValue() == "4.0" && card->getKind()->getValue() != "") {
            for (auto prop : card->getMembers()) {
                if (prop->getValue() == uuid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                card->addProperty(make_shared<VCardProperty>("MEMBER", uuid));
            }

        // vcard3 / icloud / fastmail
        } else  {
            for (auto prop : card->getExtendedProperties()) {
                if (prop->getName() == X_VCARD3_MEMBER && prop->getValue() == uuid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                card->addProperty(make_shared<VCardProperty>(X_VCARD3_MEMBER, uuid));
            }
        }
    }
}


void DAVUtils::removeMembersFromGroupCard(shared_ptr<VCard> card, vector<shared_ptr<Contact>> contacts) {
    for (auto contact : contacts) {
        string uuid = "urn:uuid:" + contact->id();

        // vcard4
        if (card->getVersion()->getValue() == "4.0" && card->getKind()->getValue() != "") {
            for (auto prop : card->getMembers()) {
                if (prop->getValue() == uuid) {
                    card->removeProperty(prop);
                    break;
                }
            }

        // vcard3 / icloud / fastmail
        } else {
            for (auto prop : card->getExtendedProperties()) {
                if (prop->getName() == X_VCARD3_MEMBER && prop->getValue() == uuid) {
                    card->removeProperty(prop);
                    break;
                }
            }
        }
    }
}

bool DAVUtils::isGroupCard(shared_ptr<VCard> card) {
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
