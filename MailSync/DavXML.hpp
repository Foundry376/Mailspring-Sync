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

#include "MailStore.hpp"

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

using namespace std;

class DavXML
{
public:
    
    explicit DavXML(string xml, string url);
    
    virtual ~DavXML() noexcept; // nothrow

    void evaluateXPath(string expr, void (*yieldBlock)(xmlNodePtr));

private:
    // Transaction must be non-copyable
    DavXML(const DavXML&);
    DavXML& operator=(const DavXML&);
    
private:
    xmlDocPtr           doc;
    xmlXPathContextPtr  xpathContext;
    
};

#endif
