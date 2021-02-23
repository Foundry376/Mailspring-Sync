//#include <iostream>
#include "mailsync/progress_collectors.hpp"


void IMAPProgress::bodyProgress(mailcore::IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress: " << current << "\n";
}

void IMAPProgress::itemsProgress(mailcore::IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress on Item: " << current << "\n";
}

void SMTPProgress::bodyProgress(mailcore::IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress: " << current << "\n";
}
