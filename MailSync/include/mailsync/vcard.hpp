/** VCard [MailSync]
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

#ifndef VCard_hpp
#define VCard_hpp

#include <vector>
#include <memory>
#include <string>

using namespace std;

class VCardProperty
{
    string _name;
    string _value;
    string _attrs;

    public:
    VCardProperty(string name, string value, string attrs = "");
    VCardProperty(string line);

    string getName();
    void setName(string name);
    string getValue();
    void setValue(string value);
    string serialize();
};

class VCard
{
    vector<shared_ptr<VCardProperty>> _properties;
public:
    explicit VCard(string vcf);

    bool incomplete();

    shared_ptr<VCardProperty> getUniqueId();
    shared_ptr<VCardProperty> getVersion();
    vector<shared_ptr<VCardProperty>> getEmails();
    shared_ptr<VCardProperty> getFormattedName();
    shared_ptr<VCardProperty> getKind();

    shared_ptr<VCardProperty> getName();
    void setName(string name);

    vector<shared_ptr<VCardProperty>> getMembers();

    vector<shared_ptr<VCardProperty>> getExtendedProperties();
    void addProperty(shared_ptr<VCardProperty> prop);
    void removeProperty(shared_ptr<VCardProperty> prop);

    string serialize();

protected:
    vector<shared_ptr<VCardProperty>> propertiesWithName(string name, bool createIfEmpty = false);
};

#endif
