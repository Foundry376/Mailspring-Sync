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

#include <limits>
#include <set>

static const string CALENDAR_FIELDS = "id,name,hexColor,canEdit,owner";
static const string EVENT_INDEX_FIELDS = "id,changeKey,type,start,end,recurrence,originalStartTimeZone";
static const string EVENT_FIELDS = "id,iCalUId,type,subject,body,start,end,isAllDay,isCancelled,showAs,location,"
                                   "organizer,attendees,responseStatus,lastModifiedDateTime,recurrence,originalStart";
static const string SERIES_FIELDS = EVENT_FIELDS + ",cancelledOccurrences,exceptionOccurrences";

// Changed events read and stored together; PerformGraphBatch sends them 20 to a request.
static const size_t EVENTS_PER_ROUND = 100;

// Whether a listed event (UTC) can have an occurrence in the sync window; a series runs to its
// recurrence range's end.
static bool inSyncWindow(const json & entry, const CalendarSyncRange & range) {
    time_t start = GraphICS::secondsAsUTC(JSONString(JSONObject(entry, "start"), "dateTime"));
    time_t end = GraphICS::secondsAsUTC(JSONString(JSONObject(entry, "end"), "dateTime"));
    const json & seriesRange = JSONObject(JSONObject(entry, "recurrence"), "range");
    if (!seriesRange.empty()) {
        end = JSONString(seriesRange, "type") == "endDate" ? GraphICS::secondsAsUTC(JSONString(seriesRange, "endDate")) + 86399
                                                           : numeric_limits<time_t>::max();
    }
    return eventOverlapsRange(start, end, range);
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
    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    set<string> listedIds {};
    vector<json> listed {};

    PaginateGraphCollection(MicrosoftGraphURL() + "/me/calendars?$select=" + CALENDAR_FIELDS, tokenProvider(), [&](json & page) {
        for (const auto & remote : page["value"]) {
            string path = JSONString(remote, "id");
            string id = MailUtils::idForCalendar(account->id(), path);
            // $skip paging repeats a calendar that shifted between page fetches.
            if (!listedIds.insert(id).second) {
                continue;
            }
            auto it = local.find(id);
            upsertCalendar(it != local.end() ? it->second : nullptr, id, path, remote);
            listed.push_back(remote);
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
        try {
            syncEvents(calendar);
        } catch (SyncException & ex) {
            // One calendar Graph refuses (a colleague stopped sharing it) must not stop the others.
            if (ex.isOffline()) {
                throw;
            }
            logger->warn("-X Calendar '{}' could not be synced: {} {}", JSONString(calendar, "name"), ex.key, ex.debuginfo);
        }
    }
    logger->info("Microsoft calendar sync complete: {} calendars", listedIds.size());
}

void MicrosoftCalendarWorker::syncEvents(const json & calendar) {
    string path = JSONString(calendar, "id");
    string calendarId = MailUtils::idForCalendar(account->id(), path);
    auto range = getCalendarSyncRange();
    map<string, json> remote {}; // Graph id -> listed event
    string url = MicrosoftGraphURL() + "/me/calendars/" + URLEscape(path) + "/events?$select=" + EVENT_INDEX_FIELDS + "&$top=200";
    PaginateGraphCollection(url, tokenProvider(), [&](json & page) {
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

    vector<json> changed {};
    for (auto & pair : remote) {
        auto it = localByHref.find(pair.first);
        if (it == localByHref.end() || it->second.front()->etag() != JSONString(pair.second, "changeKey")) {
            changed.push_back(pair.second);
        }
    }
    size_t changedCount = changed.size(); // chunksOfVector empties `changed`
    size_t fetched = 0;
    for (auto & chunk : MailUtils::chunksOfVector(changed, EVENTS_PER_ROUND)) {
        for (auto & [entry, event] : fetchEvents(chunk)) {
            auto it = localByHref.find(JSONString(entry, "id"));
            string startUTC = JSONString(JSONObject(entry, "start"), "dateTime");
            string ics = GraphICS::icsForEvent(event, account->emailAddress(), startUTC);
            ingestEvent(calendarId, entry, ics, it != localByHref.end() ? it->second : vector<shared_ptr<Event>>{});
            fetched++;
        }
    }
    logger->info("- {}: {} events in range, {} of {} changed fetched, {} rows removed", JSONString(calendar, "name"),
                 remote.size(), fetched, changedCount, removedCount);
}

// A $batch request reading one listed event. A batched request carries only its own headers, and
// one value per header, so the preferences share one Prefer (RFC 7240 section 2).
static json eventRequest(const json & entry) {
    bool series = JSONString(entry, "type") == "seriesMaster";
    string url = "/me/events/" + URLEscape(JSONString(entry, "id")) + "?$select=" +
                 (series ? SERIES_FIELDS + "&$expand=exceptionOccurrences" : EVENT_FIELDS);
    string prefer = "IdType=\"ImmutableId\", outlook.timezone=\"" + zoneToReadIn(entry) +
                    "\", outlook.body-content-type=\"text\"";
    return {{"method", "GET"}, {"url", url}, {"headers", {{"Prefer", prefer}}}};
}

// The listed events read in full, each with its listing. An event Graph fails to return is left
// out, so it keeps its rows and is read again next pass.
vector<pair<json, json>> MicrosoftCalendarWorker::fetchEvents(const vector<json> & entries) {
    vector<json> requests {};
    for (const auto & entry : entries) {
        requests.push_back(eventRequest(entry));
    }
    vector<GraphResponse> responses = PerformGraphBatch(requests, tokenProvider());
    vector<pair<json, json>> events {};
    for (size_t i = 0; i < entries.size(); i++) {
        if (responses[i].ok()) {
            events.push_back({entries[i], responses[i].body});
        } else {
            logger->warn("-X Graph event {} could not be read: {}", JSONString(entries[i], "id"), GraphErrorDescription(responses[i]));
        }
    }
    return events;
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

// Asked for per request: a pass can outlast the token it began with, which is renewed only near expiry.
std::function<string()> MicrosoftCalendarWorker::tokenProvider() {
    return [this]() {
        return SharedXOAuth2TokenManager()->partsForAccount(account, OAuthTokenAudience::MicrosoftGraphCalendars).accessToken;
    };
}
