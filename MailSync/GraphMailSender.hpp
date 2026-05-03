//
//  GraphMailSender.hpp
//  MailSync
//
//  Sends mail for Office365/Outlook accounts via the Microsoft Graph API
//  (https://graph.microsoft.com/v1.0/me/sendMail and friends), used in place
//  of SMTP because Microsoft now disables SMTP by default on new tenants.
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef GraphMailSender_hpp
#define GraphMailSender_hpp

#include <memory>
#include <string>

class Account;
class Message;

// Sends a draft via Microsoft Graph.
//
// Strategy A (preferred when possible): if the draft was already APPEND'd to
// the IMAP Drafts folder (`draft.remoteUID() != 0`) and `recipientOverride` is
// null, look up the Graph message id by `internetMessageId` and POST
// /me/messages/{id}/send. This avoids re-uploading attachments because
// Exchange already has the message in its store.
//
// Strategy B (fallback + multisend): build a Graph `Message` JSON from the
// draft and either POST /me/sendMail (small) or create-draft + upload chunks
// + send (large). Used when the IMAP draft isn't there, when Strategy A's
// lookup fails after retries, or when `recipientOverride` is set (multisend).
//
// `recipientOverride` (when non-null) replaces the to/cc/bcc with a single
// `to` recipient — used for per-recipient body customization (link/open
// tracking). For multisend callers, also pass `saveToSentItems = false` and
// then APPEND a single canonical copy to Sent via the existing IMAP path.
void sendViaGraph(std::shared_ptr<Account> account,
                  Message & draft,
                  const std::string & body,
                  bool plaintext,
                  bool saveToSentItems,
                  const std::string * recipientOverride);

#endif /* GraphMailSender_hpp */
