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

#ifndef DavXML_hpp
#define DavXML_hpp

#include <string>
#include <functional>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

using namespace std;

class DavXML
{
public:
    
    explicit DavXML(string xml, string url);
    
    virtual ~DavXML() noexcept; // nothrow

    void evaluateXPath(string expr, std::function<void(xmlNodePtr)> yieldBlock, xmlNodePtr withinNode = nullptr);
    string nodeContentAtXPath(string expr, xmlNodePtr withinNode = nullptr);
    
private:
    // Transaction must be non-copyable
    DavXML(const DavXML&);
    DavXML& operator=(const DavXML&);
    
private:
    xmlDocPtr           doc;
    xmlXPathContextPtr  xpathContext;
    
};

#endif
