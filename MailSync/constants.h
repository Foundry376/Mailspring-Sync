//
//  constants.h
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
#include <map>

#ifndef constants_h
#define constants_h

static std::map<std::string, std::string> COMMON_FOLDER_NAMES = {
    {"gel\xc3\xb6scht", "trash"},
    {"papierkorb", "trash"},
    {"\xd0\x9a\xd0\xbe\xd1\x80\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0", "trash"},
    {"[imap]/trash", "trash"},
    {"papelera", "trash"},
    {"borradores", "trash"},
    {"[imap]/\xd0\x9a\xd0\xbe\xd1\x80", "trash"},
    {"\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0", "trash"},
    {"deleted items", "trash"},
    {"\xd0\xa1\xd0\xbc\xd1\x96\xd1\x82\xd1\x82\xd1\x8f", "trash"},
    {"papierkorb/trash", "trash"},
    {"gel\xc3\xb6schte elemente", "trash"},
    {"deleted messages", "trash"},
    {"[gmail]/trash", "trash"},
    {"inbox/trash", "trash"},
    {"trash", "trash"},
    {"mail/trash", "trash"},
    {"inbox.trash", "trash"},
    
    {"roskaposti", "spam"},
    {"inbox.spam", "spam"},
    {"inbox.spam", "spam"},
    {"skr\xc3\xa4ppost", "spam"},
    {"spamverdacht", "spam"},
    {"spam", "spam"},
    {"spam", "spam"},
    {"[gmail]/spam", "spam"},
    {"[imap]/spam", "spam"},
    {"\xe5\x9e\x83\xe5\x9c\xbe\xe9\x82\xae\xe4\xbb\xb6", "spam"},
    {"junk", "spam"},
    {"junk mail", "spam"},
    {"junk e-mail", "spam"},
    
    {"inbox", "inbox"},
    
    {"archive", "archive"},
    
    {"postausgang", "sent"},
    {"inbox.gesendet", "sent"},
    {"[gmail]/sent mail", "sent"},
    {"\xeb\xb3\xb4\xeb\x82\xbc\xed\x8e\xb8\xec\xa7\x80\xed\x95\xa8", "sent"},
    {"elementos enviados", "sent"},
    {"sent", "sent"},
    {"sent items", "sent"},
    {"sent messages", "sent"},
    {"inbox.papierkorb", "sent"},
    {"odeslan\xc3\xa9", "sent"},
    {"mail/sent-mail", "sent"},
    {"ko\xc5\xa1", "sent"},
    {"inbox.sentmail", "sent"},
    {"gesendet", "sent"},
    {"ko\xc5\xa1/sent items", "sent"},
    {"gesendete elemente", "sent"},

    {"drafts", "drafts"},
    {"draft", "drafts"},
    {"brouillons", "drafts"},

};

#endif /* constants_h */
