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

static const string CALENDAR_FIELDS = "id,name,hexColor,owner,isDefaultCalendar,canEdit";
static const string EVENT_INDEX_FIELDS = "id,changeKey,type,start,end,recurrence,originalStartTimeZone";
static const string EVENT_FIELDS = "id,iCalUId,type,subject,body,start,end,isAllDay,isCancelled,showAs,location,"
                                   "organizer,attendees,responseStatus,lastModifiedDateTime,recurrence,originalStart";
static const string SERIES_FIELDS = EVENT_FIELDS + ",cancelledOccurrences,exceptionOccurrences";
// A write reads its event as the pass lists it, with what a write must check besides.
static const string EVENT_WRITE_FIELDS = EVENT_INDEX_FIELDS + ",isOnlineMeeting,isOrganizer";

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
    // Read before the listing: a write that stores its rows meanwhile isn't in the listing, and its
    // rows mustn't be removed as if Graph had deleted the event.
    map<string, vector<shared_ptr<Event>>> localByHref {};
    for (auto & row : store->findAll<Event>(Query().equal("calendarId", calendarId))) {
        // A row without a Graph id is a new event, which its create task replaces with Graph's copy.
        if (row->href() != "") {
            localByHref[row->href()].push_back(row);
        }
    }

    map<string, json> remote {}; // Graph id -> listed event
    string url = MicrosoftGraphURL() + "/me/calendars/" + URLEscape(path) + "/events?$select=" + EVENT_INDEX_FIELDS + "&$top=200";
    PaginateGraphCollection(url, tokenProvider(), [&](json & page) {
        for (const auto & entry : page["value"]) {
            if (inSyncWindow(entry, range)) {
                remote[JSONString(entry, "id")] = entry;
            }
        }
    });

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

// An event through its calendar, as Graph reads one shared with the user
// (https://learn.microsoft.com/en-us/graph/outlook-get-shared-events-calendars); relative to
// MicrosoftGraphURL(), as $batch takes it.
static string eventPath(const string & calendarPath, const string & graphId) {
    return "/me/calendars/" + URLEscape(calendarPath) + "/events/" + URLEscape(graphId);
}

// A batched request carries one value per header of its own, so the preferences share one Prefer (RFC 7240 section 2).
static json eventRequest(const string & calendarPath, const json & entry, const string & zone) {
    bool series = JSONString(entry, "type") == "seriesMaster";
    string url = eventPath(calendarPath, JSONString(entry, "id")) +
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
    bool readOnly = !JSONBool(remote, "canEdit");

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
    return MicrosoftGraphURL() + eventPath(calendarPath, graphId);
}

// A guest who deletes a meeting declines it, as Outlook and Google do: a DELETE would tell the
// organizer nothing. A meeting its organizer cancelled has no one to answer, and is deleted as
// Outlook's "Remove from calendar" does.
static bool declinesOnDelete(bool guest, const json & event) {
    return guest && !JSONBool(event, "isCancelled");
}

// A write as $batch takes it, to `path` from eventPath.
static json writeRequest(const string & method, const string & path, const json & body = json()) {
    json request = {{"method", method}, {"url", path},
                    {"headers", {{"Prefer", "IdType=\"ImmutableId\""}, {"Content-Type", "application/json"}}}};
    if (!body.is_null()) {
        request["body"] = body;
    }
    return request;
}

// A guest's answer to the event or occurrence at `path` (accept, tentativelyAccept or decline), which
// Exchange sends the organizer. Answers are retried only when throttled, since a second is a second email.
static json answerRequest(const string & path, const string & action) {
    return writeRequest("POST", path + "/" + action, {{"sendResponse", true}});
}

// Removes the event or occurrence at `path`: a DELETE, or a decline that answers its organizer.
static json removalRequest(const string & path, bool decline) {
    return decline ? answerRequest(path, "decline") : writeRequest("DELETE", path);
}

// Keeps the exception `raise` throws, to be thrown once the other writes have been tried.
static void collectFailure(vector<SyncException> & failures, const std::function<void()> & raise) {
    try {
        raise();
    } catch (SyncException & ex) {
        failures.push_back(ex);
    }
}

// A refused write as a task error: a 404 is the event deleted since the client last synced.
[[noreturn]] static void throwForWrite(const string & url, const GraphResponse & resp) {
    if (resp.status == 404) {
        throw SyncException("not-found", url + " RETURNED " + GraphErrorDescription(resp), false);
    }
    ThrowForGraphResponse(url, resp);
}

json MicrosoftCalendarWorker::fetchCalendar(const string & calendarId) {
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

// The client makes a new event from an emailed invitation that isn't on the calendar yet. Created
// through Graph, the user would become its organizer and Exchange would invite every guest. An
// event the user makes in Mailspring names the sign-in address as its organizer, or none.
bool MicrosoftCalendarWorker::organizedElsewhere(const string & ics) {
    for (const auto & vevent : GraphICS::veventsOf(ics)) {
        string organizer = vevent.property("ORGANIZER").calAddress();
        if (organizer != "" && !equalsIgnoreCase(organizer, account->emailAddress())) {
            return true;
        }
    }
    return false;
}

// The event's master and override rows, which share its UID.
vector<shared_ptr<Event>> MicrosoftCalendarWorker::findRows(shared_ptr<Event> event) {
    return store->findAll<Event>(Query().equal("calendarId", event->calendarId()).equal("icsuid", event->icsUID()));
}

// The master row of invitation `uid` that Graph holds, on a calendar that isn't someone else's: in a
// colleague's, answering would speak for them. Null when there is none.
shared_ptr<Event> MicrosoftCalendarWorker::findInvitation(const string & uid) {
    for (auto & row : store->findAll<Event>(Query().equal("accountId", account->id()).equal("icsuid", uid).equal("recurrenceId", ""))) {
        auto calendar = store->find<Calendar>(Query().equal("id", row->calendarId()));
        if (calendar && calendar->ownership() != "other" && row->href() != "") {
            return row;
        }
    }
    return nullptr;
}

json MicrosoftCalendarWorker::fetchEvent(const string & calendarPath, const string & graphId) {
    string url = eventURL(calendarPath, graphId) + "?$select=" + EVENT_WRITE_FIELDS;
    GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()());
    if (!resp.ok()) {
        throwForWrite(url, resp);
    }
    return resp.body;
}

// The series as the pass reads it: its exceptions' originalStart in the series' zone.
json MicrosoftCalendarWorker::fetchSeries(const string & calendarPath, const string & graphId) {
    vector<pair<json, json>> read = fetchEvents(calendarPath, {fetchEvent(calendarPath, graphId)});
    if (read.empty()) {
        throw SyncException("invalid-graph-resp", "Graph event " + graphId + " could not be read", true);
    }
    return read.front().second;
}

// The occurrence each anchor (a RECURRENCE-ID, or one EXDATE value) names, with its id and isCancelled,
// or null where the series has none or the lookup failed (added to `failures`). An exception is found
// by its originalStart wherever it moved; any other occurrence through /instances around its slot, the
// lookups in one batch.
vector<json> MicrosoftCalendarWorker::findOccurrences(const string & calendarPath, const json & series,
                                                      const vector<ICSProperty> & anchors, vector<SyncException> & failures) {
    vector<json> found(anchors.size());
    vector<size_t> lookedUp {};
    vector<json> lookups {};
    for (size_t i = 0; i < anchors.size(); i++) {
        const ICSProperty & anchor = anchors[i];
        for (const auto & exception : JSONArray(series, "exceptionOccurrences")) {
            if (GraphICS::namesOccurrence(anchor, JSONString(exception, "originalStart"))) {
                found[i] = exception;
                break;
            }
        }
        if (found[i].is_null()) {
            string zone = anchor.param("TZID") != "" ? anchor.param("TZID") : JSONString(JSONObject(series, "start"), "timeZone");
            auto window = GraphICS::occurrenceWindow(anchor);
            string path = eventPath(calendarPath, JSONString(series, "id")) + "/instances?startDateTime=" + window.first +
                          "&endDateTime=" + window.second + "&$select=id,originalStart,isCancelled";
            lookedUp.push_back(i);
            lookups.push_back({{"method", "GET"}, {"url", path},
                               {"headers", {{"Prefer", "IdType=\"ImmutableId\", outlook.timezone=\"" + zone + "\""}}}});
        }
    }
    vector<GraphResponse> responses = PerformGraphBatch(lookups, tokenProvider());
    for (size_t k = 0; k < lookedUp.size(); k++) {
        const ICSProperty & anchor = anchors[lookedUp[k]];
        if (!responses[k].ok()) {
            string url = MicrosoftGraphURL() + JSONString(lookups[k], "url");
            collectFailure(failures, [&]() { ThrowForGraphResponse(url, responses[k]); });
            continue;
        }
        for (const auto & instance : JSONArray(responses[k].body, "value")) {
            if (GraphICS::namesOccurrence(anchor, JSONString(instance, "originalStart"))) {
                found[lookedUp[k]] = instance;
                break;
            }
        }
        if (found[lookedUp[k]].is_null()) {
            logger->warn("-- No occurrence of {} at {} {}", JSONString(series, "id"), anchor.param("TZID"), anchor.value);
        }
    }
    return found;
}

// Each override and EXDATE as a write of its occurrence, found in the series as it stands after the
// event's own write: a change to the series' times can make Exchange drop its exceptions. The
// lookups go as one batch and the writes as another. A refused write doesn't stop the rest, which
// would leave a series change half applied; the first refusal is thrown once all have been tried.
void MicrosoftCalendarWorker::writeOccurrences(const string & calendarPath, const string & graphId, bool guest,
                                               const vector<GraphEventChange> & changes) {
    if (changes.empty()) {
        return;
    }
    vector<SyncException> failures {};
    vector<ICSProperty> anchors {};
    for (const auto & change : changes) {
        anchors.push_back(change.occurrence);
    }
    vector<json> occurrences = findOccurrences(calendarPath, fetchSeries(calendarPath, graphId), anchors, failures);
    vector<json> writes {};
    for (size_t i = 0; i < changes.size(); i++) {
        if (occurrences[i].is_null()) {
            continue;
        }
        string path = eventPath(calendarPath, JSONString(occurrences[i], "id"));
        writes.push_back(changes[i].kind == GraphEventChange::Kind::UpdateOccurrence
                             ? writeRequest("PATCH", path, changes[i].fields)
                             : removalRequest(path, declinesOnDelete(guest, occurrences[i])));
    }
    // A guest's writes are all removals.
    vector<GraphResponse> responses = PerformGraphBatch(writes, tokenProvider(), guest ? GraphRetry::ThrottledOnly : GraphRetry::ThrottledOrUnavailable);
    for (size_t i = 0; i < writes.size(); i++) {
        if (!responses[i].ok()) {
            string url = MicrosoftGraphURL() + JSONString(writes[i], "url");
            collectFailure(failures, [&]() { throwForWrite(url, responses[i]); });
        }
    }
    if (!failures.empty()) {
        logger->warn("-X {} of {} occurrence writes to {} failed", failures.size(), changes.size(), graphId);
        throw failures.front();
    }
}

// Graph's copy of the event, read and stored as the pass would, in place of `rows`; returns the
// master row's id. Failing that, or with no `calendar`, returns "" and leaves the rows to the next
// pass: they lose their etag, and a new event's row takes Graph's id, so the pass reads it over them.
string MicrosoftCalendarWorker::storeEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows) {
    if (!calendar.is_null()) {
        try {
            string path = JSONString(calendar, "id");
            json entry = fetchEvent(path, graphId);
            vector<pair<json, json>> read = fetchEvents(path, {entry});
            if (read.empty()) {
                throw SyncException("invalid-graph-resp", "Graph event " + graphId + " could not be read back", true);
            }
            string ics = GraphICS::icsForEvent(read.front().second, answeringFor(calendar), JSONString(JSONObject(entry, "start"), "dateTime"));
            MailStoreTransaction transaction{store.get(), "storeEvent"};
            string masterId = ingestEvent(MailUtils::idForCalendar(account->id(), path), entry, ics, rows);
            transaction.commit();
            return masterId;
        } catch (std::exception & ex) {
            logger->warn("-X Could not read event {} back: {}", graphId, ex.what());
        }
    }
    for (auto & row : rows) {
        row->setHref(graphId);
        row->setEtag("");
        store->save(row.get());
    }
    return "";
}

// After a refused or failed write the rows still show the client's edit: they get Graph's copy
// back. A new event's row goes, since no pass removes a row Graph never held.
void MicrosoftCalendarWorker::restoreEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows) {
    if (graphId == "") {
        for (auto & row : rows) {
            store->remove(row.get());
        }
        return;
    }
    storeEvent(calendar, graphId, rows);
}

string MicrosoftCalendarWorker::writeEvent(shared_ptr<Event> event, const string & previousIcs, const string & ics, bool answer) {
    if (answer) {
        // The EventRSVPTask queued beside it answers through Graph and reads Graph's copy back. Read back
        // here, possibly before that, Graph's copy would show the meeting unanswered.
        logger->info("- Answer to {} is left to its EventRSVPTask", event->href());
        return event->id();
    }
    string graphId = event->href();
    if (graphId == "" && organizedElsewhere(ics)) {
        // Exchange put the invitation on the calendar when it arrived; the next pass brings it in.
        logger->info("- Not creating event {}: someone else organizes it", event->icsUID());
        store->remove(event.get());
        return event->id();
    }
    vector<shared_ptr<Event>> rows = findRows(event);

    json calendar; // null while Graph hasn't listed it
    try {
        calendar = fetchCalendar(event->calendarId());
        string path = JSONString(calendar, "id");
        if (graphId != "" && previousIcs == "") {
            // Read as a new event, the edit would create a second one and invite its guests again.
            throw SyncException("invalid-ics", "No copy of the event from before the edit", false);
        }
        string startUTC;
        bool onlineMeeting = false;
        bool guest = false;
        if (graphId != "") {
            json listed = fetchEvent(path, graphId);
            startUTC = JSONString(JSONObject(listed, "start"), "dateTime");
            onlineMeeting = JSONBool(listed, "isOnlineMeeting");
            guest = !JSONBool(listed, "isOrganizer");
        }
        vector<GraphEventChange> changes = GraphICS::changesBetween(graphId == "" ? "" : previousIcs, ics, startUTC);
        for (auto & change : changes) {
            // A guest's only edit is removing an occurrence, which declines it.
            if (guest && change.kind != GraphEventChange::Kind::CancelOccurrence) {
                throw GraphEditRefused("not-organizer", "Only the organizer can change this meeting");
            }
            if (onlineMeeting && change.fields.count("body")) {
                // Text notes would replace the HTML body, and the join details in it
                // (https://learn.microsoft.com/en-us/graph/api/event-update, "body property and online meetings").
                if (!change.reapplied) {
                    throw GraphEditRefused("online-meeting-notes", "Notes of an online meeting can't be changed here");
                }
                change.fields.erase("body"); // the occurrence's own notes; it keeps the series'
            }
        }

        // The event's own write first (changesBetween puts it there): until it succeeds nothing has
        // changed, and its refusal fails the task as it stands.
        vector<GraphEventChange> occurrenceChanges {};
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
                occurrenceChanges.push_back(change);
                continue;
            }
            if (!resp.ok()) {
                throwForWrite(url, resp);
            }
        }
        if (graphId == "") {
            throw SyncException("invalid-graph-resp", "Graph created the event without an id", false);
        }
        writeOccurrences(path, graphId, guest, occurrenceChanges);
        logger->info("- Wrote {} changes to Graph event {}", changes.size(), graphId);

    } catch (GraphEditRefused & ex) {
        restoreEvent(calendar, graphId, rows);
        throw SyncException(ex.key, ex.what(), false);
    } catch (SyncException & ex) {
        restoreEvent(calendar, graphId, rows);
        throw;
    }
    // Graph holds the edit now: a failed read-back leaves the rows to the next pass rather than
    // failing the task, which the user would then save again.
    string masterId = storeEvent(calendar, graphId, rows);
    return masterId != "" ? masterId : event->id();
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
    GraphResponse listed = PerformGraphRequest("GET", url + "?$select=isOrganizer,isCancelled", tokenProvider()());
    if (!listed.ok()) {
        ThrowForGraphResponse(url, listed);
    }
    bool decline = declinesOnDelete(!JSONBool(listed.body, "isOrganizer"), listed.body);
    GraphResponse resp = PerformGraphBatch({removalRequest(eventPath(calendar->path(), event->href()), decline)}, tokenProvider(),
                                           decline ? GraphRetry::ThrottledOnly : GraphRetry::ThrottledOrUnavailable).front();
    if (!resp.ok()) {
        ThrowForGraphResponse(url, resp);
    }
    MailStoreTransaction transaction{store.get(), "deleteEvent"};
    for (auto & row : findRows(event)) {
        store->remove(row.get());
    }
    transaction.commit();
    logger->info("- {} event {} on Graph", decline ? "Declined" : "Deleted", event->href());
}

bool MicrosoftCalendarWorker::answerEvent(const string & reply, const string & partstat) {
    static const map<string, string> ACTIONS {{"ACCEPTED", "accept"}, {"TENTATIVE", "tentativelyAccept"}, {"DECLINED", "decline"}};
    auto action = ACTIONS.find(partstat);
    vector<ICSVEvent> vevents = GraphICS::veventsOf(reply);
    if (action == ACTIONS.end() || vevents.empty()) {
        throw SyncException("invalid-ics", "Graph has no answer for a REPLY of " + partstat, false);
    }
    shared_ptr<Event> event = findInvitation(vevents.front().property("UID").value);
    if (!event) {
        return false;
    }
    ICSProperty occurrence = GraphICS::occurrenceInUTC(vevents.front().property("RECURRENCE-ID"), reply);
    string graphId = event->href();
    vector<shared_ptr<Event>> rows = findRows(event);

    json calendar; // null while Graph hasn't listed it
    try {
        calendar = fetchCalendar(event->calendarId());
        string path = JSONString(calendar, "id");
        string answered = graphId;
        if (occurrence.value != "") {
            // An invitation to one occurrence alone is an event of its own on Graph, answered as a whole.
            json series = fetchSeries(path, graphId);
            if (JSONString(series, "type") == "seriesMaster") {
                vector<SyncException> failures {};
                json found = findOccurrences(path, series, {occurrence}, failures).front();
                if (!failures.empty()) {
                    throw failures.front();
                }
                if (found.is_null()) {
                    throw SyncException("not-found", "Graph event " + graphId + " has no occurrence at " + occurrence.value, false);
                }
                answered = JSONString(found, "id");
            }
        }
        GraphResponse resp = PerformGraphBatch({answerRequest(eventPath(path, answered), action->second)}, tokenProvider(),
                                               GraphRetry::ThrottledOnly).front();
        if (!resp.ok()) {
            throwForWrite(eventURL(path, answered), resp);
        }
    } catch (SyncException & ex) {
        // The client's SyncbackEventTask has already written the answer into the rows.
        restoreEvent(calendar, graphId, rows);
        throw;
    }
    logger->info("- Answered {} on Graph event {}", action->second, graphId);
    storeEvent(calendar, graphId, rows);
    return true;
}
