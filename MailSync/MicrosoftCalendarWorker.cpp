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
#include <mutex>
#include <set>

static const string CALENDAR_FIELDS = "id,name,hexColor,owner,isDefaultCalendar";
static const string EVENT_INDEX_FIELDS = "id,changeKey,type,start,end,recurrence,originalStartTimeZone";
static const string EVENT_FIELDS = "id,iCalUId,type,subject,body,start,end,isAllDay,isCancelled,showAs,location,"
                                   "organizer,attendees,responseStatus,lastModifiedDateTime,recurrence,originalStart";
static const string SERIES_FIELDS = EVENT_FIELDS + ",cancelledOccurrences,exceptionOccurrences";
// A write reads its event as the pass lists it, with what a write must check besides.
static const string EVENT_WRITE_FIELDS = EVENT_INDEX_FIELDS + ",isOnlineMeeting";

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

// The periodic pass and a manual refresh run on separate threads. Overlapping, both would find a
// new calendar missing and insert it, and the second insert's constraint error stops calendar sync.
static std::mutex passMtx;

MicrosoftCalendarWorker::MicrosoftCalendarWorker(shared_ptr<Account> account) :
    store(make_unique<MailStore>()),
    logger(spdlog::get("logger")),
    account(account)
{
}

// Every calendar /me/calendars lists, once each.
vector<json> MicrosoftCalendarWorker::listCalendars() {
    set<string> listedIds {};
    vector<json> listed {};
    PaginateGraphCollection(MicrosoftGraphURL() + "/me/calendars?$select=" + CALENDAR_FIELDS, tokenProvider(), [&](json & page) {
        for (const auto & remote : page["value"]) {
            // $skip paging repeats a calendar that shifted between page fetches.
            if (listedIds.insert(JSONString(remote, "id")).second) {
                listed.push_back(remote);
            }
        }
    });
    if (listed.empty()) {
        // Graph always lists the default calendar; pruning on an empty listing would delete every one.
        throw SyncException("invalid-graph-resp", "/me/calendars listed no calendars", true);
    }

    // The default calendar's owner is the mailbox itself, at its primary address, which the
    // sign-in address need not be (an alias). The sign-in address stands in if none is marked.
    mailboxAddress = account->emailAddress();
    for (const auto & remote : listed) {
        if (JSONBool(remote, "isDefaultCalendar") && JSONString(JSONObject(remote, "owner"), "address") != "") {
            mailboxAddress = JSONString(JSONObject(remote, "owner"), "address");
        }
    }
    return listed;
}

// responseStatus is the answer of the mailbox the calendar belongs to: in a colleague's, theirs.
vector<string> MicrosoftCalendarWorker::answeringFor(const json & calendar) {
    string owner = JSONString(JSONObject(calendar, "owner"), "address");
    return owner == "" || equalsIgnoreCase(owner, mailboxAddress) ? vector<string>{account->emailAddress(), mailboxAddress}
                                                                   : vector<string>{owner};
}

void MicrosoftCalendarWorker::run() {
    lock_guard<mutex> pass(passMtx);
    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    vector<json> listed = listCalendars();
    set<string> listedIds {};
    for (const auto & remote : listed) {
        string path = JSONString(remote, "id");
        string id = MailUtils::idForCalendar(account->id(), path);
        listedIds.insert(id);
        auto it = local.find(id);
        upsertCalendar(it != local.end() ? it->second : nullptr, id, path, remote);
    }

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
    vector<string> answering = answeringFor(calendar);
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
        vector<pair<json, json>> events = fetchEvents(path, chunk);
        MailStoreTransaction transaction{store.get(), "syncEvents:changes"};
        for (auto & [entry, event] : events) {
            auto it = localByHref.find(JSONString(entry, "id"));
            string startUTC = JSONString(JSONObject(entry, "start"), "dateTime");
            string ics = GraphICS::icsForEvent(event, answering, startUTC);
            ingestEvent(calendarId, entry, ics, it != localByHref.end() ? it->second : vector<shared_ptr<Event>>{});
            fetched++;
        }
        transaction.commit();
    }
    logger->info("- {}: {} events in range, {} of {} changed fetched, {} rows removed", JSONString(calendar, "name"),
                 remote.size(), fetched, changedCount, removedCount);
}

// Through the event's calendar, as Graph reads one shared with the user (https://learn.microsoft.com/en-us/graph/outlook-get-shared-events-calendars).
// A batched request carries one value per header of its own, so the preferences share one Prefer (RFC 7240 section 2).
static json eventRequest(const string & calendarPath, const json & entry, const string & zone) {
    bool series = JSONString(entry, "type") == "seriesMaster";
    string url = "/me/calendars/" + URLEscape(calendarPath) + "/events/" + URLEscape(JSONString(entry, "id")) +
                 "?$select=" + (series ? SERIES_FIELDS + "&$expand=exceptionOccurrences" : EVENT_FIELDS);
    string prefer = "IdType=\"ImmutableId\", outlook.timezone=\"" + zone + "\", outlook.body-content-type=\"text\"";
    return {{"method", "GET"}, {"url", url}, {"headers", {{"Prefer", prefer}}}};
}

// The listed events read in full, each with its listing. An event Graph fails to return is left
// out, so it keeps its rows and is read again next pass.
vector<pair<json, json>> MicrosoftCalendarWorker::fetchEvents(const string & calendarPath, const vector<json> & entries) {
    vector<json> requests {};
    for (const auto & entry : entries) {
        requests.push_back(eventRequest(calendarPath, entry, zoneToReadIn(entry)));
    }
    vector<GraphResponse> responses = PerformGraphBatch(requests, tokenProvider());

    // A zone name Graph refuses in Prefer answers 400; such an event is read again in UTC.
    vector<size_t> refused {};
    vector<json> retries {};
    for (size_t i = 0; i < entries.size(); i++) {
        if (responses[i].status == 400 && zoneToReadIn(entries[i]) != "UTC") {
            refused.push_back(i);
            retries.push_back(eventRequest(calendarPath, entries[i], "UTC"));
        }
    }
    if (!retries.empty()) {
        vector<GraphResponse> inUTC = PerformGraphBatch(retries, tokenProvider());
        for (size_t k = 0; k < refused.size(); k++) {
            responses[refused[k]] = inUTC[k];
        }
    }
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
// Runs inside the caller's transaction. Returns the master row's id.
string MicrosoftCalendarWorker::ingestEvent(const string & calendarId, const json & entry, const string & ics,
                                            const vector<shared_ptr<Event>> & previousRows) {
    string graphId = JSONString(entry, "id");
    string changeKey = JSONString(entry, "changeKey");
    map<string, shared_ptr<Event>> previousById {};
    for (auto & row : previousRows) {
        previousById[row->id()] = row;
    }
    ICalendar cal(ics);
    set<string> kept {};
    string masterId;
    for (auto vevent : cal.Events) {
        if (vevent->DtStart.IsEmpty()) {
            continue;
        }
        string id = MailUtils::idForEvent(account->id(), calendarId, vevent->UID, vevent->RecurrenceId);
        if (vevent->RecurrenceId.empty()) {
            masterId = id;
        }
        auto previous = previousById.find(id);
        shared_ptr<Event> existing = previous != previousById.end() ? previous->second
                                                                     : store->find<Event>(Query().equal("id", id));
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
    return masterId;
}

void MicrosoftCalendarWorker::upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote) {
    string name = JSONString(remote, "name");
    string color = JSONString(remote, "hexColor");
    string owner = JSONString(JSONObject(remote, "owner"), "address");
    string ownership = owner == "" ? "" : equalsIgnoreCase(owner, mailboxAddress) ? "mine" : "other";
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

static string eventsURL(const string & calendarPath) {
    return MicrosoftGraphURL() + "/me/calendars/" + URLEscape(calendarPath) + "/events";
}

static string eventURL(const string & calendarPath, const string & graphId) {
    return eventsURL(calendarPath) + "/" + URLEscape(graphId);
}

// A refused write as a task error: a 404 is the event deleted since the client last synced.
[[noreturn]] static void throwForWrite(const string & url, const GraphResponse & resp) {
    if (resp.status == 404) {
        throw SyncException("not-found", url + " RETURNED " + GraphErrorDescription(resp), false);
    }
    ThrowForGraphResponse(url, resp);
}

json MicrosoftCalendarWorker::listedCalendar(const string & calendarId) {
    auto calendar = store->find<Calendar>(Query().equal("id", calendarId));
    if (calendar) {
        for (const auto & remote : listCalendars()) {
            if (JSONString(remote, "id") == calendar->path()) {
                return remote;
            }
        }
    }
    throw SyncException("no-calendar", "Graph no longer lists the event's calendar", false);
}

vector<shared_ptr<Event>> MicrosoftCalendarWorker::rowsOf(const string & calendarId, const string & graphId) {
    vector<shared_ptr<Event>> rows {};
    for (auto & row : store->findAll<Event>(Query().equal("calendarId", calendarId))) {
        if (row->href() == graphId) {
            rows.push_back(row);
        }
    }
    return rows;
}

json MicrosoftCalendarWorker::listedEvent(const string & calendarPath, const string & graphId) {
    string url = eventURL(calendarPath, graphId) + "?$select=" + EVENT_WRITE_FIELDS;
    GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()());
    if (!resp.ok()) {
        throwForWrite(url, resp);
    }
    return resp.body;
}

// The series as the pass reads it: its exceptions' originalStart in the series' zone.
json MicrosoftCalendarWorker::readSeries(const string & calendarPath, const string & graphId) {
    vector<pair<json, json>> read = fetchEvents(calendarPath, {listedEvent(calendarPath, graphId)});
    if (read.empty()) {
        throw SyncException("invalid-graph-resp", "Graph event " + graphId + " could not be read", true);
    }
    return read.front().second;
}

// Graph's id for the occurrence `anchor` names, or "" when the series has none there. An
// exception is found by its originalStart wherever it moved; any other occurrence is at its slot.
string MicrosoftCalendarWorker::occurrenceId(const string & calendarPath, const json & series, const ICSProperty & anchor) {
    for (const auto & exception : JSONArray(series, "exceptionOccurrences")) {
        if (GraphICS::namesOccurrence(anchor, JSONString(exception, "originalStart"))) {
            return JSONString(exception, "id");
        }
    }
    string zone = anchor.param("TZID") != "" ? anchor.param("TZID") : JSONString(JSONObject(series, "start"), "timeZone");
    auto window = GraphICS::occurrenceWindow(anchor);
    string url = eventURL(calendarPath, JSONString(series, "id")) + "/instances?startDateTime=" + window.first +
                 "&endDateTime=" + window.second + "&$select=id,originalStart";
    GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()(), "", "application/json",
                                             {"Prefer: outlook.timezone=\"" + zone + "\""});
    if (!resp.ok()) {
        ThrowForGraphResponse(url, resp);
    }
    for (const auto & instance : JSONArray(resp.body, "value")) {
        if (GraphICS::namesOccurrence(anchor, JSONString(instance, "originalStart"))) {
            return JSONString(instance, "id");
        }
    }
    return "";
}

// Graph's copy of the event, read and stored as the pass would, in place of `rows`.
string MicrosoftCalendarWorker::storeEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows) {
    string path = JSONString(calendar, "id");
    json entry = listedEvent(path, graphId);
    vector<pair<json, json>> read = fetchEvents(path, {entry});
    if (read.empty()) {
        throw SyncException("invalid-graph-resp", "Graph event " + graphId + " could not be read back", true);
    }
    string ics = GraphICS::icsForEvent(read.front().second, answeringFor(calendar), JSONString(JSONObject(entry, "start"), "dateTime"));
    MailStoreTransaction transaction{store.get(), "storeEvent"};
    string masterId = ingestEvent(MailUtils::idForCalendar(account->id(), path), entry, ics, rows);
    transaction.commit();
    return masterId;
}

// After a refused or failed write the rows still show the client's edit. They get Graph's copy
// back, or failing that lose their etag so the next pass reads it.
void MicrosoftCalendarWorker::restoreEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows) {
    if (graphId == "") {
        return; // a row Graph never held: the next pass removes it
    }
    try {
        storeEvent(calendar, graphId, rows);
    } catch (std::exception & ex) {
        logger->warn("-X Could not read event {} back: {}", graphId, ex.what());
        for (auto & row : rows) {
            row->setEtag("");
            store->save(row.get());
        }
    }
}

string MicrosoftCalendarWorker::writeEvent(shared_ptr<Event> event, const string & previousIcs) {
    json calendar = listedCalendar(event->calendarId());
    string path = JSONString(calendar, "id");
    string graphId = event->href();
    if (graphId != "" && previousIcs == "") {
        // Read as a new event, the edit would create a second one and invite its guests again.
        throw SyncException("invalid-ics", "No copy of the event from before the edit", false);
    }
    vector<shared_ptr<Event>> rows = graphId == "" ? vector<shared_ptr<Event>>{event} : rowsOf(event->calendarId(), graphId);

    try {
        string startUTC;
        bool onlineMeeting = false;
        if (graphId != "") {
            json listed = listedEvent(path, graphId);
            startUTC = JSONString(JSONObject(listed, "start"), "dateTime");
            onlineMeeting = JSONBool(listed, "isOnlineMeeting");
        }
        vector<GraphEventChange> changes = GraphICS::changesBetween(graphId == "" ? "" : previousIcs, event->icsData(), startUTC);
        for (const auto & change : changes) {
            if (onlineMeeting && change.fields.is_object() && change.fields.count("body")) {
                // Text notes would replace the HTML body, and the join details in it
                // (https://learn.microsoft.com/en-us/graph/api/event-update, "body property and online meetings").
                throw GraphEditRefused("online-meeting-notes", "Notes of an online meeting can't be changed here");
            }
        }

        json series; // read at the first occurrence write, after any write to the series (changesBetween puts those first)
        for (const auto & change : changes) {
            string url;
            GraphResponse resp;
            if (change.kind == GraphEventChange::Kind::Create) {
                json fields = change.fields;
                fields["transactionId"] = event->icsUID(); // a retried POST then creates nothing new
                url = eventsURL(path);
                resp = PerformGraphRequest("POST", url, tokenProvider()(), fields.dump());
                graphId = resp.ok() ? JSONString(resp.body, "id") : "";
            } else if (change.kind == GraphEventChange::Kind::Update) {
                url = eventURL(path, graphId);
                resp = PerformGraphRequest("PATCH", url, tokenProvider()(), change.fields.dump());
            } else {
                if (series.is_null()) {
                    series = readSeries(path, graphId);
                }
                string occurrence = occurrenceId(path, series, change.occurrence);
                if (occurrence == "") {
                    logger->warn("-- No occurrence of {} at {} {}", graphId, change.occurrence.param("TZID"), change.occurrence.value);
                    continue;
                }
                url = eventURL(path, occurrence);
                resp = change.kind == GraphEventChange::Kind::UpdateOccurrence
                     ? PerformGraphRequest("PATCH", url, tokenProvider()(), change.fields.dump())
                     : PerformGraphRequest("DELETE", url, tokenProvider()());
            }
            if (!resp.ok()) {
                throwForWrite(url, resp);
            }
        }
        if (graphId == "") {
            throw SyncException("invalid-graph-resp", "Graph created the event without an id", false);
        }
        logger->info("- Wrote {} changes to Graph event {}", changes.size(), graphId);
        return storeEvent(calendar, graphId, rows);

    } catch (GraphEditRefused & ex) {
        restoreEvent(calendar, graphId, rows);
        throw SyncException(ex.key, ex.what(), false);
    } catch (SyncException & ex) {
        restoreEvent(calendar, graphId, rows);
        throw;
    }
}

void MicrosoftCalendarWorker::deleteEvent(shared_ptr<Event> event) {
    if (event->href() == "") {
        store->remove(event.get()); // Graph never held it
        return;
    }
    if (!event->recurrenceId().empty()) {
        // An override row carries its series' id: deleting by it would take the whole series.
        throw SyncException("shared-resource", "Cannot delete a single occurrence by deleting its series", false);
    }
    auto calendar = store->find<Calendar>(Query().equal("id", event->calendarId()));
    if (!calendar) {
        throw SyncException("no-calendar", "Calendar not found for event deletion", false);
    }
    string url = eventURL(calendar->path(), event->href());
    GraphResponse resp = PerformGraphRequest("DELETE", url, tokenProvider()());
    if (!resp.ok()) {
        ThrowForGraphResponse(url, resp);
    }
    MailStoreTransaction transaction{store.get(), "deleteEvent"};
    for (auto & row : rowsOf(event->calendarId(), event->href())) {
        store->remove(row.get());
    }
    transaction.commit();
    logger->info("- Deleted event {} from Graph", event->href());
}
