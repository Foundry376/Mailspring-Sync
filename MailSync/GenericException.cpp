//
//  SyncException.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "GenericException.hpp"
#include "StanfordCPPLib/exceptions.h"

GenericException::GenericException()
{
    stacktrace::call_stack trace;
    _stackentries = trace.stack;
}

void GenericException::printStackTrace() {
    exceptions::printStackTrace(_stackentries);
}

json GenericException::toJSON() {
    return {
        {"what", what()},
    };
}
