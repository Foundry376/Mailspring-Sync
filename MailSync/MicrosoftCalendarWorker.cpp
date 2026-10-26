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
#include "Event.hpp"
#include "GraphICS.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "XOAuth2TokenManager.hpp"
#include "strlib.h"

#include <set>

static const string CALENDAR_FIELDS = "id,name,hexColor,canEdit,owner";
static const string EVENT_INDEX_FIELDS = "id,changeKey,type,start,end,recurrence,originalStartTimeZone";
static const string EVENT_FIELDS = "id,iCalUId,type,subject,body,start,end,isAllDay,isCancelled,showAs,location,"
                                   "organizer,attendees,responseStatus,lastModifiedDateTime,recurrence,originalStart";
static const string SERIES_FIELDS = EVENT_FIELDS + ",cancelledOccurrences,exceptionOccurrences";

// "2026-10-22T09:00:00.0000000" as 20261022T090000, "2026-12-31" as 20261231.
static string compact(const string & extended) {
    string out;
    for (char c : extended.substr(0, 19)) {
        if (c != '-' && c != ':') {
            out += c;
        }
    }
    return out;
}

// Whether a listed event (UTC) can have an occurrence in the sync window; a series runs to its
// recurrence range's end.
static bool inSyncWindow(const json & entry, const CalendarSyncRange & range) {
    string windowStart = range.startStr.substr(0, 15);
    string windowEnd = range.endStr.substr(0, 15);
    string start = compact(JSONString(JSONObject(entry, "start"), "dateTime"));
    string end = compact(JSONString(JSONObject(entry, "end"), "dateTime"));
    const json & seriesRange = JSONObject(JSONObject(entry, "recurrence"), "range");
    if (!seriesRange.empty()) {
        end = JSONString(seriesRange, "type") == "endDate" ? compact(JSONString(seriesRange, "endDate")) + "T235959"
                                                           : "99991231T235959";
    }
    return start <= windowEnd && end >= windowStart;
}

// Graph refuses Prefer: outlook.timezone for custom zones ("tzone://Microsoft/Custom") and display
// names ("(UTC-08:00) Pacific Time"), so those events are read in UTC.
static string zoneToReadIn(const json & entry) {
    string zone = JSONString(entry, "originalStartTimeZone");
    return zone == "" || zone.find(':') != string::npos ? "UTC" : zone;
}

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
    vector<pair<string, string>> listed {}; // id, path

    paginateGraphCollection(MicrosoftGraphURL() + "/me/calendars?$select=" + CALENDAR_FIELDS, accessToken, [&](json & page) {
        for (const auto & remote : page["value"]) {
            string path = JSONString(remote, "id");
            string id = MailUtils::idForCalendar(account->id(), path);
            // $skip paging repeats a calendar that shifted between page fetches.
            if (!listedIds.insert(id).second) {
                continue;
            }
            auto it = local.find(id);
            upsertCalendar(it != local.end() ? it->second : nullptr, id, path, remote);
            listed.push_back({id, path});
        }
    });

    for (auto & pair : local) {
        if (!listedIds.count(pair.first)) {
            size_t removed = store->removeCalendarAndEvents(pair.second.get());
            logger->info("Removed calendar '{}' and its {} events; Graph no longer lists it",
                         pair.second->name(), removed);
        }
    }
    for (auto & calendar : listed) {
        syncEvents(calendar.first, calendar.second, accessToken);
    }
    logger->info("Microsoft calendar sync complete: {} calendars", listedIds.size());
}

void MicrosoftCalendarWorker::syncEvents(const string & calendarId, const string & path, const string & accessToken) {
    auto range = getCalendarSyncRange();
    map<string, json> remote {}; // Graph id -> listed event
    string url = MicrosoftGraphURL() + "/me/calendars/" + URLEscape(path) + "/events?$select=" + EVENT_INDEX_FIELDS + "&$top=200";
    paginateGraphCollection(url, accessToken, [&](json & page) {
        for (const auto & entry : page["value"]) {
            if (inSyncWindow(entry, range)) {
                remote[JSONString(entry, "id")] = entry;
            }
        }
    });

    map<string, vector<shared_ptr<Event>>> localByHref {};
    for (auto & row : store->findAll<Event>(Query().equal("calendarId", calendarId))) {
        localByHref[row->href()].push_back(row);
    }

    vector<shared_ptr<Event>> removed {};
    for (auto & pair : localByHref) {
        if (!remote.count(pair.first)) {
            removed.insert(removed.end(), pair.second.begin(), pair.second.end());
        }
    }
    size_t removedCount = removed.size(); // chunksOfVector empties `removed`
    for (auto & chunk : MailUtils::chunksOfVector(removed, 100)) {
        MailStoreTransaction transaction{store.get(), "syncEvents:deletions"};
        for (auto & row : chunk) {
            store->remove(row.get());
        }
        transaction.commit();
    }

    size_t fetched = 0;
    for (auto & pair : remote) {
        auto it = localByHref.find(pair.first);
        bool current = it != localByHref.end() && it->second.front()->etag() == JSONString(pair.second, "changeKey");
        if (current) {
            continue;
        }
        try {
            json event = fetchEvent(pair.second, accessToken);
            string startUTC = JSONString(JSONObject(pair.second, "start"), "dateTime");
            string ics = GraphICS::icsForEvent(event, account->emailAddress(), startUTC);
            ingestEvent(calendarId, pair.second, ics, it != localByHref.end() ? it->second : vector<shared_ptr<Event>>{});
            fetched++;
        } catch (SyncException & e) {
            // One unreadable event keeps its rows and is retried next pass; the rest still sync.
            if (!e.isRetryable() || e.isOffline()) {
                throw;
            }
            logger->warn("-X Graph event {} could not be read: {}", pair.first, e.debuginfo);
        }
    }
    logger->info("- {} events in range, {} fetched, {} rows removed", remote.size(), fetched, removedCount);
}

json MicrosoftCalendarWorker::fetchEvent(const json & entry, const string & accessToken) {
    bool series = JSONString(entry, "type") == "seriesMaster";
    string url = MicrosoftGraphURL() + "/me/events/" + URLEscape(JSONString(entry, "id")) + "?$select=" +
                 (series ? SERIES_FIELDS + "&$expand=exceptionOccurrences" : EVENT_FIELDS);
    return graphGET(url, accessToken, {
        "Prefer: outlook.timezone=\"" + zoneToReadIn(entry) + "\"",
        "Prefer: outlook.body-content-type=\"text\"",
    });
}

// One row per VEVENT, keyed by UID and RECURRENCE-ID; rows of the event no longer in its ICS go.
void MicrosoftCalendarWorker::ingestEvent(const string & calendarId, const json & entry, const string & ics,
                                          const vector<shared_ptr<Event>> & previousRows) {
    string graphId = JSONString(entry, "id");
    string changeKey = JSONString(entry, "changeKey");
    ICalendar cal(ics);
    set<string> kept {};
    MailStoreTransaction transaction{store.get(), "ingestGraphEvent"};
    for (auto vevent : cal.Events) {
        if (vevent->DtStart.IsEmpty()) {
            continue;
        }
        auto existing = store->find<Event>(Query().equal("calendarId", calendarId)
                                                  .equal("icsuid", vevent->UID)
                                                  .equal("recurrenceId", vevent->RecurrenceId));
        if (existing) {
            existing->applyICSEventData(changeKey, graphId, ics, vevent);
            store->save(existing.get());
            kept.insert(existing->id());
        } else {
            Event event(changeKey, account->id(), calendarId, ics, vevent);
            event.setHref(graphId);
            store->save(&event);
            kept.insert(event.id());
        }
    }
    for (auto & row : previousRows) {
        if (!kept.count(row->id())) {
            store->remove(row.get());
        }
    }
    transaction.commit();
}

void MicrosoftCalendarWorker::upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote) {
    string name = JSONString(remote, "name");
    string color = JSONString(remote, "hexColor");
    string owner = JSONString(JSONObject(remote, "owner"), "address");
    string ownership = owner == "" ? "" : equalsIgnoreCase(owner, account->emailAddress()) ? "mine" : "other";
    // Read-only until event edits go through Graph: TaskProcessor writes events with DAVWorker.
    bool readOnly = true;

    if (!local) {
        Calendar calendar(id, account->id());
        calendar.setPath(path);
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
    if (color != "") {
        string before = local->color();
        local->setColor(color);
        changed = changed || local->color() != before;
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
json MicrosoftCalendarWorker::graphGET(const string & url, const string & accessToken, const vector<string> & headers) {
    GraphResponse resp = PerformGraphRequest("GET", url, accessToken, "", "application/json", headers);
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
        if (!page.count("value") || !page["value"].is_array()) {
            throw SyncException("invalid-graph-resp", next + " returned no value array", true);
        }
        yieldPage(page);
        next = JSONString(page, "@odata.nextLink");
    }
}
