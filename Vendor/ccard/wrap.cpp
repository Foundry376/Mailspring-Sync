#include <iostream>
#include <string>
#include <fstream>
#include <algorithm>
#include <stack>
#include <sstream>
using namespace std;

#include "cardparser.h"
#include "wrap.hpp"
#include <assert.h>

#define TOUPPER(s) transform(s.begin(), s.end(), s.begin(), ::toupper)

using nlohmann::json;

typedef stack<shared_ptr<TUserData>> TUserDataStack;

TUserDataStack dataStack;


extern "C" void PropHandler(void *userData, const CARD_Char *propName, const CARD_Char **params)
{
    if (strcmp(propName, "BEGIN") == 0 || strcmp(propName, "begin") == 0)
    {
        // begin: vcard/vcal/whatever
        auto child = make_shared<TUserData>();
        if (!dataStack.empty()) {
            dataStack.top()->children.push_back(child);
        }
        dataStack.push(child);
        return;
    };

    if (dataStack.empty())
    {
        cout << "ERROR:Property encountered with no BEGIN:" << endl;
        return;
    };

    auto top = dataStack.top();
    
    if (strcmp(propName, "END") == 0 || strcmp(propName, "end") == 0)
    {
        // end: vcard/vcal/whatever
        top->inEnd = true;
    }
    else
    {
        string pname = propName;
        TOUPPER(pname);
        if (top->attrs.count(pname) == 0) {
            top->attrs[pname] = json::array();
        }
        top->lastAttrKey = pname;

        // I think this is for key-value pairs having attributes?
//        const CARD_Char **p = params;
//        if (p[0]) {
//            top->attrs[pname] = {};
//        }
        
//        while (p[0])
//        {
//            top->attrs[pname][p[0]] = "";
//            if (p[1]) {
//                top->attrs[pname][p[0]] = p[1];
//            }
//            p += 2;
//        };
    };
};


extern "C" void DataHandler(void *userData, const CARD_Char *data, int len)
{
    if (dataStack.empty())
    {
        cout << "ERROR:data encountered with no BEGIN:" << endl;
        return;
    };

    auto top = dataStack.top();
    
    if (top->inBegin)
    {
        // accumulate begin data
        ostringstream os;
        if (len > 0)
        {
            os.write(data, len);
            top->cardType += os.str();
        }
        else
        {
            TOUPPER(top->cardType);
            top->inBegin = false;
        };
    }
    else if (top->inEnd)
    {
        if (len == 0)
        {
            dataStack.pop();
        };
    }
    else
    {
        top->startData = false;

        if (len == 0)
        {
            cout <<  endl;
            top->startData = true;
        }
        else
        {
            ostringstream os;
            os.write(data, len);
            
            top->attrs[top->lastAttrKey].push_back(os.str());
        };
    };
};


shared_ptr<TUserData> vcsToJSON(string vcs) {
    CARD_Parser vp = CARD_ParserCreate(NULL);

    dataStack = stack<shared_ptr<TUserData>>{};
    auto root = make_shared<TUserData>();
    dataStack.push(root);
    
    // initialise
    CARD_SetPropHandler(vp, PropHandler);
    CARD_SetDataHandler(vp, DataHandler);

    int rc = CARD_Parse(vp, vcs.c_str(), (int)strlen(vcs.c_str()), false);
    if (rc == 0) {
        return {};
    };
	// finalise parsing
    
    CARD_Parse(vp, NULL, 0, true);

	// free parser
	CARD_ParserFree(vp);
    
    return root->children.at(0);
};
