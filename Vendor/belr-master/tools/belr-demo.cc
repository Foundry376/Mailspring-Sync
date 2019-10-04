/*
 * Copyright (c) 2016-2019 Belledonne Communications SARL.
 *
 * This file is part of belr - a language recognition library for ABNF-defined grammars.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstring>

#include "belr/grammarbuilder.h"
#include "belr/abnf.h"

using namespace::belr;
using namespace::std;

/*
 * This demo program instanciates a SIP parser from the SIP grammar in a text file.
 * Then it attempts to parse a SIP URI and fill URI components in our custom C++ object.
**/

/*This is our base class for all our parsed elements. It does nothing but is required for run time type identification*/
class SipElement{
public:
	virtual ~SipElement () = default;
};

/*this the class representing uri "other" params, per the grammar. They will be added to the SIP-URI when found.*/
class OtherParam : public SipElement{
private:
	string mName;
	string mValue;
public:
	OtherParam(){
	}
	static shared_ptr<OtherParam> create(){
		return make_shared<OtherParam>();
	}
	void setName(const string & name){
		mName = name;
	}
	const string & getName()const{
		return mName;
	}
	void setValue(const string &value){
		mValue = value;
	}
	const string &getValue()const{
		return mValue;
	}
};

/*This is our SipUri class, representing sip uris. It has accessors for most common field, including accessing parameters*/
class SipUri : public SipElement{
private:
	string mUsername;
	string mHost;
	string mPasswd;
	int mPort;
	list<shared_ptr<OtherParam>> mOtherParams;
public:
	static shared_ptr<SipUri> create(){
		return make_shared<SipUri>();
	}
	SipUri(){
		mPort = 0;
	}
	/*follows all the setter/getters of SIP-URI fields*/
	void setUsername(const string &username){
		mUsername = username;
	}
	const string & getUsername()const{
		return mUsername;
	}
	void setHost(const string &host){
		mHost = host;
	}
	const string & getHost()const{
		return mHost;
	}
	void setPasswd(const string & passwd){
		mPasswd = passwd;
	}
	const string &getPasswd()const{
		return mPasswd;
	}
	void setPort(int port){
		mPort = port;
	}
	int getPort()const{
		return mPort;
	}
	void addOtherParam(const shared_ptr<OtherParam> &params){
		mOtherParams.push_back(params);
	}
	const list<shared_ptr<OtherParam>> & getOtherParams()const{
		return mOtherParams;
	}
};

int main(int argc, char *argv[]){
	string uriToParse;

	if (argc<2){
		cerr<<argv[0]<<" <uri to parse>"<<endl;
		return -1;
	}
	uriToParse = argv[1];
	//Create a GrammarBuilder:
	ABNFGrammarBuilder builder;
	//construct the grammar from the grammar file, the core rules are included since required by most RFCs.
	shared_ptr<Grammar> grammar=builder.createFromAbnfFile("sipgrammar.txt", make_shared<CoreRules>());

	if (!grammar){
		cerr<<"Could not build grammar from sipgrammar.txt"<<endl;
		return -1;
	}

	//now instanciate a parser and assign it collectors and handlers
	//This parser expects to build objects which are all inherited from SipElement, and that are stored as shared_ptr.
	Parser<shared_ptr<SipElement>> parser(grammar);

	//Now, tell our parser where to assign elements when they are found during parsing.
	parser.setHandler("SIP-URI", make_fn(&SipUri::create)) //tells that whenever a SIP-URI is found, a SipUri object must be created.
		->setCollector("user", make_sfn(&SipUri::setUsername)) //tells that when a "user" field is found, SipUri::setUsername() is to be called for assigning the "user"
		->setCollector("host", make_sfn(&SipUri::setHost)) //tells that when host is encountered, use SipUri::setHost() to assign it to our SipUri object.
		->setCollector("port", make_sfn(&SipUri::setPort)) // understood ?
		->setCollector("password", make_sfn(&SipUri::setPasswd))
		->setCollector("other-param", make_sfn(&SipUri::addOtherParam));
	parser.setHandler("other-param", make_fn(&OtherParam::create)) //when other-param is matched, construct an OtherParam object to hold the name and value of the other-params.
		->setCollector("pname", make_sfn(&OtherParam::setName))
		->setCollector("pvalue", make_sfn(&OtherParam::setValue));

	//now, parse the input. We have to tell the parser the root object, which is the SIP-URI in this example.
	size_t parsedSize = 0;
	shared_ptr<SipElement> ret = parser.parseInput("SIP-URI", uriToParse, &parsedSize);
	//if a sip uri is found, the return value is non null and should cast into a SipUri object.
	if (ret){
		shared_ptr<SipUri> sipUri = dynamic_pointer_cast<SipUri>(ret);
		//now let's print the content of uri object to make sure that its content is consistent with the input passed.
		cout<<"user: "<<sipUri->getUsername()<<endl;
		cout<<"passord: "<<sipUri->getPasswd()<<endl;
		cout<<"host: "<<sipUri->getHost()<<endl;
		cout<<"port: "<<sipUri->getPort()<<endl;
		cout<<"Listing of other-params:"<<endl;
		for(auto it = sipUri->getOtherParams().begin(); it != sipUri->getOtherParams().end(); ++it){
			cout<<"name: "<<(*it)->getName()<<" value: "<<(*it)->getValue()<<endl;
		}
	}else{
		cerr<<"Could not parse ["<<uriToParse<<"]"<<endl;
	}
	//so nice, so easy to use belr, isn't it ?
	return 0;
};
