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

#include "common.h"
#include "belr/parser.h"

using namespace std;

// =============================================================================

namespace belr{




DebugElement::DebugElement(const string& rulename, const string& value)
	:mRulename(rulename), mValue(value){

}

shared_ptr< DebugElement > DebugElement::create(const string& rulename, const string& value){
	return make_shared<DebugElement>(rulename,value);
}

void DebugElement::addChild(const shared_ptr< DebugElement >& e){
	//cout<<mRulename<<"["<<this<<"] is given child "<<e->mRulename<<"["<<e<<"]"<<endl;
	mChildren.push_back(e);
}

const string &DebugElement::getValue()const{
	return mValue;
}

void DebugElement::findChildren(const std::string &rulename, std::list<std::shared_ptr<DebugElement>> &retlist)const{
	for (std::list<std::shared_ptr<DebugElement>>::const_iterator it = mChildren.begin(); it != mChildren.end() ; ++it){
		if ((*it)->mRulename == rulename){
			retlist.push_back(*it);
		}else{
			findChildren(rulename, retlist);
		}
	}
}

ostream& DebugElement::tostream(int level, ostream& str) const{
	int i;
	for(i=0;i<level;i++) str<<'\t';
	if (!mChildren.empty()){
		str<<mRulename<<endl;
		for (auto it=mChildren.begin(); it!=mChildren.end(); ++it){
			(*it)->tostream(level+1,str);
		}
	}else{
		size_t pos;
		string value(mValue);
		while((pos=value.find("\r"))!=string::npos){
			value.replace(pos,1,"\\r");
		};
		while((pos=value.find("\n"))!=string::npos){
			value.replace(pos,1,"\\n");
		};
		str<<mRulename<<" : "<<"'"<<value<<"'"<<endl;
	}
	return str;
}

DebugParser::DebugParser(const shared_ptr< Grammar >& grammar)
	: Parser<shared_ptr<DebugElement>>(grammar){

}

shared_ptr<DebugElement> DebugParser::parseInput(const string& rulename, const string& input, size_t* parsed_size){
	return Parser<shared_ptr<DebugElement>>::parseInput(rulename, input, parsed_size);
}

void DebugParser::setObservedRules(const list< string >& rules){
	for (auto it=rules.begin(); it!=rules.end(); ++it){
		auto h=setHandler(*it, make_fn(&DebugElement::create));
		for (auto it2=rules.begin(); it2!=rules.end(); ++it2){
			h->setCollector(*it2, make_sfn(&DebugElement::addChild));
		}
	}
}



}//end of namespace
