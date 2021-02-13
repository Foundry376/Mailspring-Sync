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

#include <stdio.h>

#include <string>
#include <vector>

#include "spdlog/spdlog.h"

#include "mailsync/models/account.hpp"
#include "mailsync/models/identity.hpp"
#include "mailsync/models/contact_book.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/dav_xml.hpp"

typedef std::string ETAG;

class DAVWorker {
    MailStore * store;
    std::shared_ptr<spdlog::logger> logger;

    std::string calHost;
    std::string calPrincipal;

public:
    std::shared_ptr<Account> account;

    DAVWorker(std::shared_ptr<Account> account);

    void run();

    std::shared_ptr<ContactBook> resolveAddressBook();

    void writeAndResyncContact(std::shared_ptr<Contact> contact);
    void deleteContact(std::shared_ptr<Contact> contact);

    void runContacts();
    void runForAddressBook(std::shared_ptr<ContactBook> ab);

    void ingestContactDeletions(std::shared_ptr<ContactBook> ab, std::vector<ETAG> deleted);
    std::shared_ptr<Contact> ingestAddressDataNode(std::shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(std::shared_ptr<Contact> contact);

    void runCalendars();
    void runForCalendar(std::string id, std::string name, std::string path);

    const std::string getAuthorizationHeader();

    std::shared_ptr<DavXML> performXMLRequest(std::string path, std::string method, std::string payload = "");
    std::string performVCardRequest(std::string _url, std::string method, std::string vcard = "", ETAG existingEtag = "");

};

#endif /* DAVWorker_hpp */
