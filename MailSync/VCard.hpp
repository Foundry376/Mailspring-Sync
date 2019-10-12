//
//  VCard.hpp
//  mailsync
//
//  Created by Ben Gotow on 10/11/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//


#ifndef VCard_hpp
#define VCard_hpp

#include <vector>
#include <memory>
#include <string>

using namespace std;

class VCardProperty
{
    string _name;
    string _value;
    string _attrs;
    
    public:
    VCardProperty(string name, string value, string attrs = "");
    VCardProperty(string line);

    string getName();
    void setName(string name);
    string getValue();
    void setValue(string value);
    string serialize();
};

class VCard
{
    vector<shared_ptr<VCardProperty>> _properties;
public:
    explicit VCard(string vcf);
    
    bool incomplete();

    shared_ptr<VCardProperty> getUniqueId();
    shared_ptr<VCardProperty> getVersion();
    vector<shared_ptr<VCardProperty>> getEmails();
    shared_ptr<VCardProperty> getFormattedName();
    shared_ptr<VCardProperty> getKind();

    shared_ptr<VCardProperty> getName();
    void setName(string name);
    
    vector<shared_ptr<VCardProperty>> getMembers();

    vector<shared_ptr<VCardProperty>> getExtendedProperties();
    void addProperty(shared_ptr<VCardProperty> prop);
    void removeProperty(shared_ptr<VCardProperty> prop);
    
    string serialize();

protected:
    vector<shared_ptr<VCardProperty>> propertiesWithName(string name, bool createIfEmpty = false);
};

#endif
