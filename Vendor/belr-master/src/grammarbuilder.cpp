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

#include <fstream>

#include <bctoolbox/logging.h>

#include "belr/abnf.h"
#include "belr/parser.h"
#include "belr/grammarbuilder.h"

#include "config.h"

using namespace std;

// =============================================================================

namespace belr{
shared_ptr< ABNFNumval > ABNFNumval::create(){
	return make_shared<ABNFNumval>();
}

shared_ptr< Recognizer > ABNFNumval::buildRecognizer(const shared_ptr< Grammar >& grammar){
	if (mIsRange){
		return Utils::char_range(mValues[0],mValues[1]);
	}else{
		auto seq=Foundation::sequence();
		for (auto it=mValues.begin();it!=mValues.end();++it){
			seq->addRecognizer(Foundation::charRecognizer(*it,true));
		}
		return seq;
	}
}

void ABNFNumval::parseValues(const string &val, int base){
	size_t dash=val.find('-');
	if (dash!=string::npos){
		mIsRange=true;
		string first=val.substr(1,dash-1);
		string last=val.substr(dash+1,string::npos);
		mValues.push_back(strtol(first.c_str(),nullptr,base));
		mValues.push_back(strtol(last.c_str(),nullptr,base));
	}else{
		mIsRange=false;
		string tmp=val.substr(1,string::npos);
		const char *s=tmp.c_str();
		char *endptr=nullptr;
		do{
			long lv=strtol(s,&endptr,base);
			if (lv == 0 && s == endptr) {
				break;
			}
			if (*endptr=='.') s=endptr+1;
			else s=endptr;
			mValues.push_back(lv);
		}while(*s!='\0');
	}
}

void ABNFNumval::setDecVal(const string& decval){
	parseValues(decval,10);
}

void ABNFNumval::setHexVal(const string& hexval){
	parseValues(hexval,16);
}

void ABNFNumval::setBinVal(const string& binval){
	parseValues(binval,2);
}

shared_ptr< Recognizer > ABNFOption::buildRecognizer(const shared_ptr< Grammar >& grammar){
	return Foundation::loop()->setRecognizer(mAlternation->buildRecognizer(grammar),0,1);
}

shared_ptr< ABNFOption > ABNFOption::create(){
	return make_shared<ABNFOption>();
}

void ABNFOption::setAlternation(const shared_ptr< ABNFAlternation >& a){
	mAlternation=a;
}

shared_ptr< ABNFGroup > ABNFGroup::create(){
	return make_shared<ABNFGroup>();
}

shared_ptr< Recognizer > ABNFGroup::buildRecognizer(const shared_ptr< Grammar >& grammar){
	return mAlternation->buildRecognizer(grammar);
}

void ABNFGroup::setAlternation(const shared_ptr< ABNFAlternation >& a){
	mAlternation=a;
}

shared_ptr< Recognizer > ABNFElement::buildRecognizer(const shared_ptr< Grammar >& grammar){
	if (mElement)
		return mElement->buildRecognizer(grammar);
	if (!mRulename.empty())
		return grammar->getRule(mRulename);
	if (!mCharVal.empty()){
		if (mCharVal.size()==1)
			return Foundation::charRecognizer(mCharVal[0],false);
		else
			return Utils::literal(mCharVal);
	}
	bctbx_fatal("ABNFElement::buildRecognizer is empty, should not happen!");
	return nullptr;
}

shared_ptr< ABNFElement > ABNFElement::create(){
	return make_shared<ABNFElement>();
}

void ABNFElement::setElement(const shared_ptr< ABNFBuilder >& e){
	mElement=e;
}

void ABNFElement::setRulename(const string& rulename){
	mRulename=rulename;
}

void ABNFElement::setCharVal(const string& charval){
	mCharVal=charval.substr(1,charval.size()-2); //in order to remove surrounding quotes
}

void ABNFElement::setProseVal(const string& prose){
	if (!prose.empty()){
		bctbx_fatal("prose-val is not supported.");
	}
}

shared_ptr< ABNFRepetition > ABNFRepetition::create(){
	return make_shared<ABNFRepetition>();
}

void ABNFRepetition::setCount(int count){
	mCount=count;
}

void ABNFRepetition::setMin(int min){
	mMin=min;
}

void ABNFRepetition::setMax(int max){
	mMax=max;
}

void ABNFRepetition::setRepeat(const string& r){
	mRepeat=r;
}

void ABNFRepetition::setElement(const shared_ptr< ABNFElement >& e){
	mElement=e;
}

shared_ptr< Recognizer > ABNFRepetition::buildRecognizer(const shared_ptr< Grammar >& grammar){
	if (mRepeat.empty()) return mElement->buildRecognizer(grammar);
	if (mCount!=-1){
		return Foundation::loop()->setRecognizer(mElement->buildRecognizer(grammar), mCount, mCount);
	}else{
		return Foundation::loop()->setRecognizer(mElement->buildRecognizer(grammar), mMin, mMax);
	}
}

shared_ptr<ABNFConcatenation> ABNFConcatenation::create(){
	return make_shared<ABNFConcatenation>();
}

shared_ptr<Recognizer> ABNFConcatenation::buildRecognizer(const shared_ptr<Grammar> &grammar){
	if (mRepetitions.size()==0){
		bctbx_fatal("No repetitions set !");
	}
	if (mRepetitions.size()==1){
		return mRepetitions.front()->buildRecognizer(grammar);
	}else{
		auto seq=Foundation::sequence();
		for (auto it=mRepetitions.begin(); it!=mRepetitions.end(); ++it){
			seq->addRecognizer((*it)->buildRecognizer(grammar));
		}
		return seq;
	}
	return nullptr;
}

void ABNFConcatenation::addRepetition(const shared_ptr< ABNFRepetition >& r){
	mRepetitions.push_back(r);
}

shared_ptr<ABNFAlternation> ABNFAlternation::create(){
	return make_shared<ABNFAlternation>();
}

void ABNFAlternation::addConcatenation(const shared_ptr<ABNFConcatenation> &c){
	mConcatenations.push_back(c);
}

shared_ptr<Recognizer> ABNFAlternation::buildRecognizer(const shared_ptr<Grammar> &grammar){
	if (mConcatenations.size()==1) return mConcatenations.front()->buildRecognizer(grammar);
	return buildRecognizerNoOptim(grammar);
}

shared_ptr< Recognizer > ABNFAlternation::buildRecognizerNoOptim(const shared_ptr< Grammar >& grammar){
	auto sel=Foundation::selector();
	for (auto it=mConcatenations.begin(); it!=mConcatenations.end(); ++it){
		sel->addRecognizer((*it)->buildRecognizer(grammar));
	}
	return sel;
}

shared_ptr<ABNFRule> ABNFRule::create(){
	return make_shared<ABNFRule>();
}

void ABNFRule::setName(const string& name){
	if (!mName.empty()) bctbx_error("Rule %s is renamed !!!!!", name.c_str());
	mName=name;
}

void ABNFRule::setAlternation(const shared_ptr<ABNFAlternation> &a){
	mAlternation=a;
}

bool ABNFRule::isExtension()const{
	return mDefinedAs.find('/')!=string::npos;
}

shared_ptr<Recognizer> ABNFRule::buildRecognizer(const shared_ptr<Grammar> &grammar){
	return mAlternation->buildRecognizer(grammar);
}

void ABNFRule::setDefinedAs(const string& defined_as){
	mDefinedAs=defined_as;
}


shared_ptr<ABNFRuleList> ABNFRuleList::create(){
	return make_shared<ABNFRuleList>();
}

void ABNFRuleList::addRule(const shared_ptr<ABNFRule>& rule){
	mRules.push_back(rule);
}

shared_ptr<Recognizer> ABNFRuleList::buildRecognizer(const shared_ptr<Grammar> &grammar){
	for (auto it=mRules.begin(); it!=mRules.end(); ++it){
		shared_ptr<ABNFRule> rule=(*it);
		if (rule->isExtension()){
			grammar->extendRule(rule->getName(), rule->buildRecognizer(grammar));
		}else{
			auto rec = rule->buildRecognizer(grammar);
			/* Special case: if the returned recognizer is a rule that was already added to the grammar, 
			 * we should not add it a second time, otherwise the name of the recognizer and the name in the grammar entry
			 * will be different. To solve this problem, we use an intermediary AliasRecognizer*/
			if (!rec->getName().empty()){
				/*only rules (that is recognizers added to the grammar) have a name defined*/
				if (rec->getName() != rule->getName()){
					/* we are facing a statement like rule2 = rule1 */
					auto alias = make_shared<RecognizerAlias>();
					alias->setPointed(rec);
					rec = alias;
					
				}
			}
			grammar->addRule(rule->getName(), rec);
		}
	}
	return nullptr;
}

ABNFGrammarBuilder::ABNFGrammarBuilder()
: mParser(make_shared<ABNFGrammar>()){
	mParser.setHandler("rulelist", make_fn(&ABNFRuleList::create))
		->setCollector("rule", make_sfn(&ABNFRuleList::addRule));
	mParser.setHandler("rule", make_fn(&ABNFRule::create))
		->setCollector("rulename",make_sfn(&ABNFRule::setName))
		->setCollector("defined-as",make_sfn(&ABNFRule::setDefinedAs))
		->setCollector("alternation",make_sfn(&ABNFRule::setAlternation));
	mParser.setHandler("alternation", make_fn(&ABNFAlternation::create))
		->setCollector("concatenation",make_sfn(&ABNFAlternation::addConcatenation));
	mParser.setHandler("concatenation", make_fn(&ABNFConcatenation::create))
		->setCollector("repetition", make_sfn(&ABNFConcatenation::addRepetition));
	mParser.setHandler("repetition", make_fn(&ABNFRepetition::create))
		->setCollector("repeat", make_sfn(&ABNFRepetition::setRepeat))
		->setCollector("repeat-min", make_sfn(&ABNFRepetition::setMin))
		->setCollector("repeat-max", make_sfn(&ABNFRepetition::setMax))
		->setCollector("repeat-count", make_sfn(&ABNFRepetition::setCount))
		->setCollector("element", make_sfn(&ABNFRepetition::setElement));
	mParser.setHandler("element", make_fn(&ABNFElement::create))
		->setCollector("rulename", make_sfn(&ABNFElement::setRulename))
		->setCollector("group", make_sfn(&ABNFElement::setElement))
		->setCollector("option", make_sfn(&ABNFElement::setElement))
		->setCollector("char-val", make_sfn(&ABNFElement::setCharVal))
		->setCollector("num-val", make_sfn(&ABNFElement::setElement))
		->setCollector("prose-val", make_sfn(&ABNFElement::setElement));
	mParser.setHandler("group", make_fn(&ABNFGroup::create))
		->setCollector("alternation", make_sfn(&ABNFGroup::setAlternation));
	mParser.setHandler("option", make_fn(&ABNFOption::create))
		->setCollector("alternation", make_sfn(&ABNFOption::setAlternation));
	mParser.setHandler("num-val", make_fn(&ABNFNumval::create))
		->setCollector("bin-val", make_sfn(&ABNFNumval::setBinVal))
		->setCollector("hex-val", make_sfn(&ABNFNumval::setHexVal))
		->setCollector("dec-val", make_sfn(&ABNFNumval::setDecVal));
}

shared_ptr<Grammar> ABNFGrammarBuilder::createFromAbnf(const string &abnf, const shared_ptr<Grammar> &gram){
	size_t parsed;
	shared_ptr<ABNFBuilder> builder = mParser.parseInput("rulelist",abnf,&parsed);
	if (!builder) {
		bctbx_error("Failed to create builder.");
		return nullptr;
	}

	if (parsed<(size_t)abnf.size()){
		bctbx_error("Only %llu bytes parsed over a total of %llu.", (unsigned long long)parsed, (unsigned long long) abnf.size());
		return nullptr;
	}

	shared_ptr<Grammar> retGram;
	if (gram==nullptr) retGram=make_shared<Grammar>(abnf);
	else retGram=gram;

	builder->buildRecognizer(retGram);
	bctbx_message("Succesfully created grammar with %i rules.", retGram->getNumRules());
	if (retGram->isComplete()){
		bctbx_message("Grammar is complete.");
		retGram->optimize();
		bctbx_message("Grammar has been optimized.");
	}else{
		bctbx_warning("Grammar is not complete.");
	}
	return gram;
}

shared_ptr<Grammar> ABNFGrammarBuilder::createFromAbnfFile(const string &path, const shared_ptr<Grammar> &gram){
	ifstream istr(path);
	if (!istr.is_open()){
		bctbx_error("Could not open %s", path.c_str());
		return nullptr;
	}
	stringstream sstr;
	sstr<<istr.rdbuf();
	return createFromAbnf(sstr.str(), gram);
}

GrammarLoader * GrammarLoader::sInstance = nullptr;

GrammarLoader::GrammarLoader(){
	mSystemPaths.push_back(BELR_GRAMMARS_DIR);
	mSystemPaths.push_back(BELR_GRAMMARS_RELATIVE_DIR);
}

GrammarLoader &GrammarLoader::get(){
	if (sInstance == nullptr){
		sInstance = new GrammarLoader();
	}
	return *sInstance;
}

void GrammarLoader::addPath(const string &path){
	mAppPaths.push_front(path);
}

void GrammarLoader::clear(){
	mAppPaths.clear();
}

string GrammarLoader::lookup(const string &fileName, const list<string> & paths){
	for(auto & it : paths){
		ostringstream absFilename;
		absFilename<<it<<"/"<<fileName;
		if (bctbx_file_exist(absFilename.str().c_str()) == 0){
			return absFilename.str();
		}
	}
	return "";
}

bool GrammarLoader::isAbsolutePath(const string &fileName){
	if (fileName[0] == '/') return TRUE;
#ifdef _WIN32
	/* for windows:*/
	if (fileName.size() > 2 && fileName[1] == ':') return TRUE;
#endif
	return FALSE;
}

shared_ptr<Grammar> GrammarLoader::load(const string &fileName){
	string absFilename;
	
	if (isAbsolutePath(fileName)){
		absFilename = fileName;
	}
	if (absFilename.empty()){
		absFilename = lookup(fileName, mAppPaths);
	}
	if (absFilename.empty()){
		absFilename = lookup(fileName, mSystemPaths);
	}
	if (absFilename.empty()){
		bctbx_error("Could not load grammar %s because the file could not be located.", fileName.c_str());
		return nullptr;
	}
	shared_ptr<Grammar> ret = make_shared<Grammar>(fileName);
	if (ret->load(absFilename) == 0) return ret;
	return nullptr;
}

}//end of namespace
