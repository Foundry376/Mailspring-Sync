//
//  MailStoreTransaction.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "DavXML.hpp"
#include "SyncException.hpp"

using namespace std;

DavXML::DavXML(string xml, string url):
    xpathContext(nullptr)
{
    // HTTP Protocol Quirk: Content-Type Header Mismatches
    // Many CalDAV servers (notably iCloud and Open-Xchange) return valid XML but with
    // incorrect Content-Type headers like "text/plain" instead of "text/xml" or
    // "application/xml". The Python caldav library handles this by parsing XML regardless
    // of Content-Type, and we do the same - we simply attempt to parse whatever the server
    // returns without checking Content-Type first. This pragmatic approach ensures
    // compatibility with non-compliant servers.
    doc = xmlReadMemory(xml.c_str(), (int)xml.size(), url.c_str(), "utf-8", 0);
    if (doc == nullptr) {
        throw new SyncException("Unable to parse CalDav XML", xml, false);
    }
}

DavXML::~DavXML() noexcept // nothrow
{
    if (doc != nullptr) {
        xmlFreeDoc(doc);
    }
    if (xpathContext != nullptr) {
        xmlXPathFreeContext(xpathContext);
    }
}

void DavXML::evaluateXPath(string expr, std::function<void(xmlNodePtr)> yieldBlock, xmlNodePtr withinNode)
{
    if (xpathContext == nullptr) {
        xpathContext = xmlXPathNewContext(doc);
        if (xpathContext == NULL) {
            fprintf(stderr,"Error: unable to create new XPath context\n");
            return;
        }
    
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"d", (const xmlChar *)"DAV:");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"D", (const xmlChar *)"DAV:");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"caldav", (const xmlChar *)"urn:ietf:params:xml:ns:caldav");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"carddav", (const xmlChar *)"urn:ietf:params:xml:ns:carddav");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"cs", (const xmlChar *)"http://calendarserver.org/ns/");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"ical", (const xmlChar *)"http://apple.com/ns/ical/");
    }

    xmlXPathObjectPtr xpathObj = nullptr;

    if (withinNode == nullptr) {
        xpathObj = xmlXPathEvalExpression((const xmlChar *)expr.c_str(), xpathContext);
    } else {
        xpathContext->node = withinNode;
        xpathObj = xmlXPathEvalExpression((const xmlChar *)expr.c_str(), xpathContext);
    }
    if (xpathObj == nullptr) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", expr.c_str());
        return;
    }
    
    auto nodes = xpathObj->nodesetval;
    if (nodes == nullptr) {
        return;
    }
    
    for (int i = 0; i < nodes->nodeNr; ++i) {
        xmlNodePtr cur = nodes->nodeTab[i];
        if(cur->type == XML_NAMESPACE_DECL) {
            yieldBlock((xmlNodePtr)cur->next);
        } else {
            yieldBlock(cur);
        }
    }

    xmlXPathFreeObject(xpathObj);
}

string DavXML::nodeContentAtXPath(string expr, xmlNodePtr withinNode) {
    string result = "";
    evaluateXPath(expr, ([&](xmlNodePtr cur) {
        // Handle null content gracefully - can occur when querying for element nodes
        // (which have child nodes, not direct text content) rather than text() nodes
        if (cur->content != nullptr) {
            result = string((char *)cur->content);
        }
        return;
    }), withinNode);
    return result;
}

