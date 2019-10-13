//
//  VCard.cpp
//  mailsync
//
//  Created by Ben Gotow on 10/11/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//

#include "VCard.hpp"
#include "strlib.h"

#include <algorithm>
#include <sstream>

/**
 Originally Mailspring mailsync was going to use BelCard and the integration was actually working
 pretty well, but on Linux it caused the app to link against glibc on Ubuntu 14 (trusty) in a way
 that prevented it from being relocated to Ubuntu 18. This was very concerning and I didn't know
 how to fix it, so I re-evaluated what we needed.
 
 This VCard parser / serializer does the bare minimum - it does not understand how to break apart
 VCard properties and does not do anything with the various property value schemas. It just breaks
 the card into rows / properties, allows those to be mutated, and puts it back together. It is not
 lossy.
 */
VCardProperty::VCardProperty(string name, string value, string attrs):
    _name(toUpperCase(name)), _value(value), _attrs(attrs)
{
}

VCardProperty::VCardProperty(string line)
{
    size_t colon = line.find(":");
    size_t semicolon = line.find(";");
    
    if (semicolon != string::npos && colon != string::npos && semicolon < colon) {
        // attributes are present
        _name = toUpperCase(line.substr(0, semicolon));
        _attrs = line.substr(semicolon + 1, colon - semicolon - 1);
        _value = line.substr(colon + 1);
    } else if (colon != string::npos) {
        _name = toUpperCase(line.substr(0, colon));
        _attrs = "";
        _value = line.substr(colon + 1);
    }
}

string VCardProperty::getName() {
    return _name;
}

void VCardProperty::setName(string name) {
    _name = name;
}

string VCardProperty::getValue() {
    return _value;
}

void VCardProperty::setValue(string value) {
    _value = value;
}

string VCardProperty::serialize() {
    if (_attrs != "") {
        return _name + ";" + _attrs + ":" + _value;
    } else {
        return _name + ":" + _value;
    }
}


VCard::VCard(string vcf) {
    vcf = vcf + "\r\n";
    
    auto split = vcf.find("\n");
    string unparsed = "";
    
    // A Vcard is mostly one-property per line but lines can be "run-on", in which case
    // it looks like this. To accomodate these we accumulate the current line in "unparsed"
    // and parse it once we see the next line is not a run-on.
    
    /*
     FN:Katie
     ORG:Running Group;
     item1.TEL;type=pref:+18472749609
     PHOTO;TYPE=PNG;X-ABCROP-RECTANGLE=ABClipRect_1&0&0&440&440&haCIH6MkV8cyYokE
      Bet+qw==;VALUE=uri:https://p52-contacts.icloud.com:443/58330283/wbs/013d0be
      221993f5c4b1b4dd7f8081b8ce3fa776c04
     REV:2019-10-11T20:03:16Z
     */
    
    while (split != string::npos) {
        string line = vcf.substr(0, split);
        if (line.size()) {
            // Note: We split based on "\n" but the official format calls for "\r\n", so we check
            // and optionally remove the \r if it's present on eacch line.
            if (line.substr(split - 1) == "\r") {
                line = line.substr(0, split - 1);
            }
            if (line.size()) {
                if (line.substr(0, 1) == " ") {
                    unparsed += line.substr(1);
                } else {
                    if (unparsed != "") {
                        auto prop = make_shared<VCardProperty>(unparsed);
                        if (prop->getName() != "BEGIN" && prop->getName() != "END") {
                            _properties.push_back(prop);
                        }
                    }
                    unparsed = line;
                }
            }
        }

        vcf = vcf.substr(split + 1);
        split = vcf.find("\n");
    }
    
    if (unparsed != "") {
        auto prop = make_shared<VCardProperty>(unparsed);
        if (prop->getName() != "BEGIN" && prop->getName() != "END") {
            _properties.push_back(prop);
        }
    }

}

bool VCard::incomplete() {
    // future error handling in parsing. We don't pay enough attention
    // to the schema to know / care if a card is incomplete currently.
    return false;
}

// These getters add a property with a blank value if no matching properties
// are found, and the new property can be mutated. This is for consistency with Belcard.

shared_ptr<VCardProperty> VCard::getUniqueId() {
    return propertiesWithName("UID", true).front();
}

shared_ptr<VCardProperty> VCard::getVersion() {
    return propertiesWithName("VERSION", true).front();
}

vector<shared_ptr<VCardProperty>> VCard::getEmails() {
    return propertiesWithName("EMAIL", true);

}
shared_ptr<VCardProperty> VCard::getFormattedName() {
    return propertiesWithName("FN", true).front();

}
shared_ptr<VCardProperty> VCard::getKind() {
    return propertiesWithName("KIND", true).front();
}

shared_ptr<VCardProperty> VCard::getName() {
    return propertiesWithName("N", true).front();
}

void VCard::setName(string name) {
    propertiesWithName("N", true).front()->setValue(name);
}

void VCard::addProperty(shared_ptr<VCardProperty> prop) {
    _properties.push_back(prop);
}

void VCard::removeProperty(shared_ptr<VCardProperty> prop) {
    auto idx = std::find(_properties.begin(), _properties.end(), prop);
    if (idx != _properties.end()) {
        _properties.erase(idx);
    }
}

vector<shared_ptr<VCardProperty>> VCard::getMembers() {
    return propertiesWithName("MEMBER");
}

vector<shared_ptr<VCardProperty>> VCard::getExtendedProperties() {
    vector<shared_ptr<VCardProperty>> results {};
    for (auto prop : _properties) {
        if (prop->getName().substr(0, 2) == "X-") {
            results.push_back(prop);
        }
    }
    return results;
}

vector<shared_ptr<VCardProperty>> VCard::propertiesWithName(string name, bool createIfEmpty) {
    vector<shared_ptr<VCardProperty>> results {};
    for (auto prop : _properties) {
        if (prop->getName() == name) {
            results.push_back(prop);
        }
    }
    if (createIfEmpty && results.size() == 0) {
        shared_ptr<VCardProperty> added = make_shared<VCardProperty>(name, "");
        results.push_back(added);
        _properties.push_back(added);
    }
    
    return results;
}

string VCard::serialize() {
    stringstream str;
    str << "BEGIN:VCARD\r\n";
    for (auto prop : _properties) {
        if (prop->getValue() == "") {
            // ignore properties we createIfEmpty but don't fill
            continue;
        }
        str << prop->serialize();
        str << "\r\n";
    }
    str << "END:VCARD\r\n";
    return str.str();
}
