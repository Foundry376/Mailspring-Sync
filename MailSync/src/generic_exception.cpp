#include "mailsync/generic_exception.hpp"
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
