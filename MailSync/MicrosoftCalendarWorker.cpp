//
//  MicrosoftCalendarWorker.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MicrosoftCalendarWorker.hpp"
#include "MailUtils.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "XOAuth2TokenManager.hpp"
#include "strlib.h"

#include <set>

static const string CALENDAR_FIELDS = "id,name,hexColor,canEdit,owner";

MicrosoftCalendarWorker::MicrosoftCalendarWorker(shared_ptr<Account> account) :
    store(make_unique<MailStore>()),
    logger(spdlog::get("logger")),
    account(account)
{
}

void MicrosoftCalendarWorker::run() {
    string accessToken = SharedXOAuth2TokenManager()->partsForAccount(account, OAuthTokenAudience::MicrosoftGraphCalendars).accessToken;

    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    set<string> listedIds {};

    paginateGraphCollection(MicrosoftGraphURL() + "/me/calendars?$select=" + CALENDAR_FIELDS, accessToken, [&](json & page) {
        for (const auto & remote : page["value"]) {
            string path = JSONString(remote, "id");
            if (path == "") {
                continue;
            }
            string id = MailUtils::idForCalendar(account->id(), path);
            listedIds.insert(id);
            auto it = local.find(id);
            upsertCalendar(it != local.end() ? it->second : nullptr, id, remote);
        }
    });

    for (auto & pair : local) {
        if (!listedIds.count(pair.first)) {
            size_t removed = store->removeCalendarAndEvents(pair.second.get());
            logger->info("Removed calendar '{}' and its {} events; Graph no longer lists it",
                         pair.second->name(), removed);
        }
    }
    logger->info("Microsoft calendar sync complete: {} calendars", listedIds.size());
}

void MicrosoftCalendarWorker::upsertCalendar(shared_ptr<Calendar> local, const string & id, const json & remote) {
    string name = JSONString(remote, "name");
    string color = JSONString(remote, "hexColor");
    string owner = remote.count("owner") && remote["owner"].is_object() ? JSONString(remote["owner"], "address") : "";
    string ownership = owner == "" ? "" : equalsIgnoreCase(owner, account->emailAddress()) ? "mine" : "other";
    // Read-only until event edits go through Graph: TaskProcessor writes events with DAVWorker.
    bool readOnly = true;

    if (!local) {
        Calendar calendar(id, account->id());
        calendar.setPath(JSONString(remote, "id"));
        calendar.setName(name);
        if (color != "") {
            calendar.setColor(color);
        }
        calendar.setReadOnly(readOnly);
        calendar.setOwnership(ownership);
        store->save(&calendar);
        logger->info("Discovered calendar '{}' ({})", name, ownership == "" ? "owner unknown" : ownership);
        return;
    }

    bool changed = false;
    if (local->name() != name) {
        local->setName(name);
        changed = true;
    }
    if (color != "" && local->color() != toLowerCase(color)) {
        local->setColor(color);
        changed = true;
    }
    if (local->readOnly() != readOnly) {
        local->setReadOnly(readOnly);
        changed = true;
    }
    if (ownership != "" && local->ownership() != ownership) {
        local->setOwnership(ownership);
        changed = true;
    }
    if (changed) {
        store->save(local.get());
    }
}

// Keyed like ValidateRequestResp's errors, so runCalContactsSyncWorker stops on a 401 or 403.
json MicrosoftCalendarWorker::graphGET(const string & url, const string & accessToken) {
    GraphResponse resp = PerformGraphRequest("GET", url, accessToken);
    if (!resp.ok()) {
        bool retryable = resp.status != 401 && resp.status != 403;
        throw SyncException("Invalid Response Code: " + to_string(resp.status),
                            url + " RETURNED " + GraphErrorDescription(resp), retryable);
    }
    return resp.body;
}

void MicrosoftCalendarWorker::paginateGraphCollection(const string & url, const string & accessToken, std::function<void(json &)> yieldPage) {
    string next = url;
    while (next != "") {
        json page = graphGET(next, accessToken);
        yieldPage(page);
        next = JSONString(page, "@odata.nextLink");
    }
}
