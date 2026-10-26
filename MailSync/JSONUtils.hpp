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

#endif /* JSONUtils_hpp */
