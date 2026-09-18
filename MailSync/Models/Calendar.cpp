//
//  Calendar.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Calendar.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"

using namespace std;

string Calendar::TABLE_NAME = "Calendar";

Calendar::Calendar(json & json) : MailModel(json) {
    
}

Calendar::Calendar(string id, string accountId) :
    MailModel(id, accountId, 0)
{
}

Calendar::Calendar(SQLite::Statement & query) :
    MailModel(query)
{
}


string Calendar::path() {
    return _data["path"].get<string>();
}

void Calendar::setPath(string path) {
    _data["path"] = path;
}

string Calendar::name() {
    return _data["name"].get<string>();
}

void Calendar::setName(string name) {
    _data["name"] = name;
}

string Calendar::ctag() {
    return _data.count("ctag") ? _data["ctag"].get<string>() : "";
}

void Calendar::setCtag(string ctag) {
    _data["ctag"] = ctag;
}

string Calendar::syncToken() {
    return _data.count("syncToken") ? _data["syncToken"].get<string>() : "";
}

void Calendar::setSyncToken(string token) {
    _data["syncToken"] = token;
}

string Calendar::color() {
    // Default to Google Calendar blue if no color set
    return _data.count("color") ? _data["color"].get<string>() : "#4285f4";
}

void Calendar::setColor(string color) {
    // Normalize color - strip alpha channel if present (Apple sends #RRGGBBAA)
    if (color.length() == 9 && color[0] == '#') {
        color = color.substr(0, 7);  // "#RRGGBBAA" -> "#RRGGBB"
    }
    // Ensure lowercase for consistency
    transform(color.begin(), color.end(), color.begin(), ::tolower);
    _data["color"] = color;
}

string Calendar::description() {
    return _data.count("description") ? _data["description"].get<string>() : "";
}

void Calendar::setDescription(string description) {
    _data["description"] = description;
}

bool Calendar::readOnly() {
    return _data.count("read_only") ? _data["read_only"].get<bool>() : false;
}

void Calendar::setReadOnly(bool readOnly) {
    _data["read_only"] = readOnly;
}

int Calendar::order() {
    return _data.count("order") ? _data["order"].get<int>() : 0;
}

void Calendar::setOrder(int order) {
    _data["order"] = order;
}

string Calendar::tableName() {
    return Calendar::TABLE_NAME;
}

vector<string> Calendar::columnsForQuery() {
    return vector<string>{"id", "data", "accountId"};
}

void Calendar::bindToQuery(SQLite::Statement * query) {
    query->bind(":id", id());
    query->bind(":data", toJSON().dump());
    query->bind(":accountId", accountId());
}
