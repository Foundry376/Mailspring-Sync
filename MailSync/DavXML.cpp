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

void DavXML::evaluateXPath(string expr, void (*yieldBlock)(xmlNodePtr))
{
    if (xpathContext == nullptr) {
        xpathContext = xmlXPathNewContext(doc);
        if (xpathContext == NULL) {
            fprintf(stderr,"Error: unable to create new XPath context\n");
            return;
        }
    
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"d", (const xmlChar *)"DAV:");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"caldav", (const xmlChar *)"urn:ietf:params:xml:ns:caldav");
    }

    string xpathExpr = "//caldav:calendar-home-set/d:href/text()";
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression((const xmlChar *)expr.c_str(), xpathContext);
    if (xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr.c_str());
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


