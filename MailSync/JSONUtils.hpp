//
//  JSONUtils.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef JSONUtils_hpp
#define JSONUtils_hpp

#include <string>
#include "json.hpp"

using namespace nlohmann;
using namespace std;

// object[key] when it is a string, otherwise "".
inline string JSONString(const json & object, const string & key) {
    if (!object.is_object() || !object.count(key) || !object[key].is_string()) {
        return "";
    }
    return object[key].get<string>();
}

// object[key] when it is an object, otherwise an empty one.
inline const json & JSONObject(const json & object, const string & key) {
    static const json none = json::object();
    return object.is_object() && object.count(key) && object[key].is_object() ? object[key] : none;
}

// object[key] when it is an array, otherwise an empty one.
inline const json & JSONArray(const json & object, const string & key) {
    static const json none = json::array();
    return object.is_object() && object.count(key) && object[key].is_array() ? object[key] : none;
}

// object[key] when it is a boolean, otherwise false.
inline bool JSONBool(const json & object, const string & key) {
    return object.is_object() && object.count(key) && object[key].is_boolean() && object[key].get<bool>();
}

// object[key] when it is an integer, otherwise 0.
inline int JSONInt(const json & object, const string & key) {
    return object.is_object() && object.count(key) && object[key].is_number_integer() ? object[key].get<int>() : 0;
}

#endif /* JSONUtils_hpp */
