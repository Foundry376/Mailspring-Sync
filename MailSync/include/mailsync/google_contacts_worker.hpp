/** GoogleContactsWorker [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GoogleContactsWorker_hpp
#define GoogleContactsWorker_hpp

#include "mailsync/models/account.hpp"
#include "mailsync/models/identity.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/models/contact_group.hpp"
#include "mailsync/models/contact.hpp"
#include "mailsync/dav_xml.hpp"

#include <stdio.h>

#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

static string GOOGLE_SYNC_SOURCE = "gpeople";

class GoogleContactsWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

public:
    GoogleContactsWorker(shared_ptr<Account> account);

    void run();
    void paginateGoogleCollection(string urlRoot, string authorization, string syncTokenKey, std::function<void(json)> yieldBlock);

    void upsertContactGroup(shared_ptr<ContactGroup> group);
    void deleteContactGroup(string groupResourceName);
    void updateContactGroupMembership(shared_ptr<ContactGroup> group, vector<shared_ptr<Contact>> contacts, string direction);
    void deleteContact(shared_ptr<Contact> contact);
    void upsertContact(shared_ptr<Contact> contact);

private:
    void applyJSONToContact(shared_ptr<Contact> local, const json & conn);

};

#endif /* GoogleContactsWorker_hpp */
