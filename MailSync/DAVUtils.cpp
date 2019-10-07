//
//  DAVUtils.cpp
//  mailsync
//
//  Created by Ben Gotow on 10/6/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//

#include "DAVUtils.hpp"

#include <arpa/inet.h>
#include <resolv.h>

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

vector<string> DAVUtils::srvRecordsForDomain(string domain) {
    unsigned char response[NS_PACKETSZ];
    ns_msg handle;
    ns_rr rr;
    int len;
    char dispbuf[4096];
    vector<string> results;
    
    if (((len = res_search(domain.c_str(), ns_c_in, ns_t_srv, response, sizeof(response))) >= 0) and
        (ns_initparse(response, len, &handle) >= 0) and
        (ns_msg_count(handle, ns_s_an) >= 0)) {
    
        for (int ns_index = 0; ns_index < len; ns_index++) {
            if (ns_parserr(&handle, ns_s_an, ns_index, &rr)) {
                /* WARN: ns_parserr failed */
                continue;
            }
            ns_sprintrr (&handle, &rr, NULL, NULL, dispbuf, sizeof (dispbuf));
            if (ns_rr_class(rr) == ns_c_in and ns_rr_type(rr) == ns_t_srv) {
                const u_char * rdata = ns_rr_rdata(rr);
                
                std::vector<char> dname(NS_MAXDNAME + 1);
                ns_name_uncompress(
                    ns_msg_base(handle), ns_msg_end(handle),
                    rdata + 6, &dname[0], NS_MAXDNAME);
                results.push_back(string(dname.data()));
            }
        }
    }
    return results;
}
