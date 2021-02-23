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



class VCardProperty
{
    std::string _name;
    std::string _value;
    std::string _attrs;

    public:
    VCardProperty(std::string name, std::string value, std::string attrs = "");
    VCardProperty(std::string line);

    std::string getName();
    void setName(std::string name);
    std::string getValue();
    void setValue(std::string value);
    std::string serialize();
};

class VCard
{
    std::vector<std::shared_ptr<VCardProperty>> _properties;
public:
    explicit VCard(std::string vcf);

    bool incomplete();

    std::shared_ptr<VCardProperty> getUniqueId();
    std::shared_ptr<VCardProperty> getVersion();
    std::vector<std::shared_ptr<VCardProperty>> getEmails();
    std::shared_ptr<VCardProperty> getFormattedName();
    std::shared_ptr<VCardProperty> getKind();

    std::shared_ptr<VCardProperty> getName();
    void setName(std::string name);

    std::vector<std::shared_ptr<VCardProperty>> getMembers();

    std::vector<std::shared_ptr<VCardProperty>> getExtendedProperties();
    void addProperty(std::shared_ptr<VCardProperty> prop);
    void removeProperty(std::shared_ptr<VCardProperty> prop);

    std::string serialize();

protected:
    std::vector<std::shared_ptr<VCardProperty>> propertiesWithName(std::string name, bool createIfEmpty = false);
};

#endif
