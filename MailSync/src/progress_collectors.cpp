//#include <iostream>
#include "mailsync/progress_collectors.hpp"


void IMAPProgress::bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress: " << current << "\n";
}

void IMAPProgress::itemsProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress on Item: " << current << "\n";
}

void SMTPProgress::bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        std::cout << "Progress: " << current << "\n";
}
