/** DAVWorker [MailSync]
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

#ifndef DAVWorker_hpp
#define DAVWorker_hpp

#include "mailsync/models/account.hpp"
#include "mailsync/models/identity.hpp"
#include "mailsync/models/contact_book.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/dav_xml.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

typedef string ETAG;

class DAVWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

    string calHost;
    string calPrincipal;

public:
    shared_ptr<Account> account;

    DAVWorker(shared_ptr<Account> account);

    void run();

    shared_ptr<ContactBook> resolveAddressBook();

    void writeAndResyncContact(shared_ptr<Contact> contact);
    void deleteContact(shared_ptr<Contact> contact);

    void runContacts();
    void runForAddressBook(shared_ptr<ContactBook> ab);

    void ingestContactDeletions(shared_ptr<ContactBook> ab, vector<ETAG> deleted);
    shared_ptr<Contact> ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(shared_ptr<Contact> contact);

    void runCalendars();
    void runForCalendar(string id, string name, string path);

    const string getAuthorizationHeader();

    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "");
    string performVCardRequest(string _url, string method, string vcard = "", ETAG existingEtag = "");

};

#endif /* DAVWorker_hpp */
