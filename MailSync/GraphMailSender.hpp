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

// Sends a draft via Microsoft Graph. Returns `true` if the message was sent
// via Graph; returns `false` if the account's OAuth refresh token doesn't
// have the `Mail.Send` scope yet (the caller should fall back to SMTP).
// Throws SyncException for any other failure (network, permission, server
// error).
//
// This bool-return-on-missing-scope behavior is intentional: it lets us roll
// out Graph-based sending without breaking accounts that authorized before
// `Mail.Send` was added to the consent flow. Once a user re-auths and gets a
// refresh token with the new scope, future sends transition to Graph
// transparently.
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
bool sendViaGraph(std::shared_ptr<Account> account,
                  Message & draft,
                  const std::string & body,
                  bool plaintext,
                  bool saveToSentItems,
                  const std::string * recipientOverride);

#endif /* GraphMailSender_hpp */
