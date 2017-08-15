//
//  SyncException.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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
    void printStackTrace(std::ostream & out = std::cerr);
   
    virtual json toJSON();
};


#endif /* GenericException_hpp */
