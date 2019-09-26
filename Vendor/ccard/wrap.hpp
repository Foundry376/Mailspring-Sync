#include <nlohmann/json.hpp>


#ifndef Wrap_hpp
#define Wrap_hpp

using nlohmann::json;

// globals
class TUserData
{
public:
    bool inBegin, inEnd, startData;
    string cardType;
    string content;
    vector<shared_ptr<TUserData>> children;
    json attrs;
    string lastAttrKey;
    TUserData() : inBegin(true), inEnd(false), startData(true), attrs({}), content(""), children({}), lastAttrKey("") {}
};


shared_ptr<TUserData> vcsToJSON(string vcs);

#endif
