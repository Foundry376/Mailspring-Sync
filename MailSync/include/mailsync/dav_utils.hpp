/** DAVUtils [MailSync]
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

#ifndef DAVUtils_hpp
#define DAVUtils_hpp

#include <stdio.h>
#include <string>
#include <vector>

#include "mailsync/models/contact.hpp"
#include "mailsync/vcard.hpp"

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
