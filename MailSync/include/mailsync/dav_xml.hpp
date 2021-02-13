/** DAVXML [MailSync]
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

#ifndef DavXML_hpp
#define DavXML_hpp

#include <functional>
#include <string>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

class DavXML
{
public:

    explicit DavXML(std::string xml, std::string url);

    virtual ~DavXML() noexcept; // nothrow

    void evaluateXPath(std::string expr, std::function<void(xmlNodePtr)> yieldBlock, xmlNodePtr withinNode = nullptr);
    std::string nodeContentAtXPath(std::string expr, xmlNodePtr withinNode = nullptr);

private:
    // Transaction must be non-copyable
    DavXML(const DavXML&);
    DavXML& operator=(const DavXML&);

private:
    xmlDocPtr           doc;
    xmlXPathContextPtr  xpathContext;

};

#endif
