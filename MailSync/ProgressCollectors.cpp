//
//  ProgressCollectors.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "ProgressCollectors.hpp"


void IMAPProgress::bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        cout << "Progress: " << current << "\n";
}

void IMAPProgress::itemsProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        cout << "Progress on Item: " << current << "\n";
}

void SMTPProgress::bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum) {
    //        cout << "Progress: " << current << "\n";
}

