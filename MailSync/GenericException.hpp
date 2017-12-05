//
//  SyncException.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef GenericException_hpp
#define GenericException_hpp

#include <stdio.h>
#include "json.hpp"
#include "StanfordCPPLib/stacktrace/call_stack.h"

using namespace nlohmann;
using namespace std;

class GenericException : public std::exception {
    vector<stacktrace::entry> _stackentries;
    
public:
    GenericException();
    void printStackTrace();
   
    virtual json toJSON();
};


#endif /* GenericException_hpp */
