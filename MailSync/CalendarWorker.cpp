//
//  MetadataWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "CalendarWorker.hpp"
#include "MailStore.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Message.hpp"
#include "Thread.hpp"
#include "Contact.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

#include <string>
#include <curl/curl.h>

CalendarWorker::CalendarWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger"))
{
    // For now, we assume Gmail
    server = "https://apidata.googleusercontent.com";
    principalPath = "/caldav/v2/" + account->emailAddress();
    
    // Initialize libxml
    xmlInitParser();
}

void CalendarWorker::run() {
    auto doc = performXMLRequest(principalPath, "PROPFIND", "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:cs=\"http://calendarserver.org/ns/\"><d:prop><d:displayname /><c:calendar-home-set /><cs:getctag /></d:prop></d:propfind>");

    doc->evaluateXPath("//caldav:calendar-home-set/d:href/text()", ([](xmlNodePtr cur){
        fprintf(stdout, "= node \"%s\": type %d, content: \"%s\"\n", cur->name, cur->type, cur->content);
    }));
}

shared_ptr<DavXML> CalendarWorker::performXMLRequest(string path, string method, string payload) {
    string url = server + path;
    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
    headers = curl_slist_append(headers, "Depth: 1");

    if (account->refreshToken() != "") {
        auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        string authorization = "Authorization: Bearer " + parts.accessToken;
        headers = curl_slist_append(headers, authorization.c_str());
    } else {
        // to use basic auth:
        // url.replace(url.find("://"), 3, "://" + username + ":" + password + "@");
    }
    
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    string result = PerformRequest(curl_handle);
    return make_shared<DavXML>(result, url);
}


void CalendarWorker::print_xpath_nodes(xmlNodeSetPtr nodes, FILE* output) {
    xmlNodePtr cur;
    int size;
    int i;
    
    assert(output);
    size = (nodes) ? nodes->nodeNr : 0;
    
    fprintf(output, "Result (%d nodes):\n", size);
    for(i = 0; i < size; ++i) {
        assert(nodes->nodeTab[i]);
        
        if(nodes->nodeTab[i]->type == XML_NAMESPACE_DECL) {
            xmlNsPtr ns;
            
            ns = (xmlNsPtr)nodes->nodeTab[i];
            cur = (xmlNodePtr)ns->next;
            if(cur->ns) {
                fprintf(output, "= namespace \"%s\"=\"%s\" for node %s:%s\n",
                        ns->prefix, ns->href, cur->ns->href, cur->name);
            } else {
                fprintf(output, "= namespace \"%s\"=\"%s\" for node %s\n",
                        ns->prefix, ns->href, cur->name);
            }
        } else if(nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
            cur = nodes->nodeTab[i];
            if(cur->ns) {
                fprintf(output, "= element node \"%s:%s\"\n",
                        cur->ns->href, cur->name);
            } else {
                fprintf(output, "= element node \"%s\"\n",
                        cur->name);
            }
        } else {
            cur = nodes->nodeTab[i];
            fprintf(output, "= node \"%s\": type %d, content: \"%s\"\n", cur->name, cur->type, cur->content);
        }
    }
}



