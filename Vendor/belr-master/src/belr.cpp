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
#include "binarystream.h"
#include "belr/parser.h"
#include "belr/belr.h"

using namespace std;

namespace belr{

void fatal(const char *message){
	bctbx_fatal("%s", message);
}

// =============================================================================

TransitionMap::TransitionMap(){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i)
		mPossibleChars[i]=false;
}

bool TransitionMap::intersect(const TransitionMap* other){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		if (mPossibleChars[i] && other->mPossibleChars[i]) return true;
	}
	return false;
}

bool TransitionMap::intersect(const TransitionMap *other, TransitionMap *result){
	bool ret=false;
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		bool tmp=mPossibleChars[i] && other->mPossibleChars[i];
		result->mPossibleChars[i]=tmp;
		if (tmp) ret=true;
	}
	return ret;
}

void TransitionMap::merge(const TransitionMap* other){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		if (other->mPossibleChars[i]) mPossibleChars[i]=true;
	}
}

void Recognizer::setName(const std::string& name){
	static unsigned int id_base=0;
	mName=name;
	/* every recognizer that is given a name is given also a unique id*/
	mId=++id_base;
}

void Recognizer::serialize(BinaryOutputStream& fstr, bool topLevel){
	RecognizerTypeId tid = CharRecognizerId;
	if (typeid(*this) == typeid(CharRecognizer)) tid = CharRecognizerId;
	else if (typeid(*this) == typeid(Selector)) tid = SelectorId;
	else if (typeid(*this) == typeid(Loop)) tid = LoopId;
	else if (typeid(*this) == typeid(ExclusiveSelector)) tid = ExclusiveSelectorId;
	else if (typeid(*this) == typeid(CharRange)) tid = CharRangeId;
	else if (typeid(*this) == typeid(Literal)) tid = LiteralId;
	else if (typeid(*this) == typeid(Sequence)) tid = SequenceId;
	else if (typeid(*this) == typeid(RecognizerPointer)) tid = PointerId;
	else if (typeid(*this) == typeid(RecognizerAlias)) tid = AliasId;
	else bctbx_fatal("Unsupported Recognizer derived type.");

	unsigned char type_byte = (unsigned char)tid;

	if (tid == PointerId){
		/*
		 * It is useless to serialize a RecognizerPointer, which is only a trick to break loops in recognizer chains.
		 * Instead we follow the pointer and serialize the pointed recognizer.
		**/
		dynamic_cast<RecognizerPointer*>(this)->getPointed()->serialize(fstr, topLevel);
		return;
	}

	if (topLevel || mName.empty()) {
		//write the type
		fstr<<type_byte;
		fstr<<mName;
		//then invoked derived class serialization
		_serialize(fstr);
	}else{
		/* If we are not a top level, and mName is defined, we are referencing another rule of the grammar.
		 * There is then no need to recurse.*/
		type_byte = (unsigned char)RuleRefId;
		fstr<<type_byte;
		fstr<<mName;
	}
}

Recognizer::Recognizer(BinaryGrammarBuilder& istr){
	// read the recognizer name:
	string name;
	istr >> name;
	if (!name.empty()) setName(name);
}


std::shared_ptr<Recognizer> Recognizer::build(BinaryGrammarBuilder &istr){
	shared_ptr<Recognizer> ret;
	unsigned char byte;
	RecognizerTypeId tid;
	//read the type byte
	istr>>byte;
	tid = (RecognizerTypeId) byte;
	switch(tid){
		case CharRecognizerId:
			ret = make_shared<CharRecognizer>(istr);
		break;
		case SelectorId:
			ret = make_shared<Selector>(istr);
		break;
		case ExclusiveSelectorId:
			ret = make_shared<ExclusiveSelector>(istr);
		break;
		case LoopId:
			ret = make_shared<Loop>(istr);
		break;
		case SequenceId:
			ret = make_shared<Sequence>(istr);
		break;
		case CharRangeId:
			ret = make_shared<CharRange>(istr);
		break;
		case LiteralId:
			ret = make_shared<Literal>(istr);
		break;
		case RuleRefId:
		{
			string name;
			istr>>name;
			ret = istr.getRule(name);
		}
		break;
		case PointerId:
		break;
		case AliasId:
			ret = make_shared<RecognizerAlias>(istr);
		break;
	}
	if (!ret){
		BCTBX_SLOGE<<"Unsupported recognizer id "<<(int)byte<<" at pos "<< istr.tellg();
	}else{
		//if (!ret->getName().empty()){
		//	BCTBX_SLOGD<<"Built recognizer "<<ret->getName();
		//}
	}
	return ret;
}

const string &Recognizer::getName()const{
	return mName;
}

size_t Recognizer::feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t match;

#ifdef BELR_DEBUG
	BCTBX_SLOGD<<<"Trying to match: "<<mName;
#endif

	ParserLocalContext hctx;
	if (ctx) ctx->beginParse(hctx, shared_from_this());
	match=_feed(ctx, input, pos);
	if (match!=string::npos && match>0){
		#ifdef BELR_DEBUG
			if (mName.size()>0){
				string matched=input.substr(pos,match);
				BCTBX_SLOGD<<"Matched recognizer '"<<mName<<"' with sequence '"<<matched<<"'.";
			}
		#endif
	}
	if (ctx) ctx->endParse(hctx, input, pos, match);

	return match;
}

bool Recognizer::getTransitionMap(TransitionMap* mask){
	bool ret=_getTransitionMap(mask);
	if (0 /*!mName.empty()*/){
		BCTBX_SLOGD<<"TransitionMap after "<<mName;
		ostringstream mapss;
		for(int i=0;i<256;++i){
			if (mask->mPossibleChars[i]) mapss<<(char)i;
		}
		BCTBX_SLOGD<<mapss.str();
	}
	return ret;
}


bool Recognizer::_getTransitionMap(TransitionMap* mask){
	string input;
	input.resize(2,'\0');
	for(int i=0;i<256;++i){
		input[0]=i;
		if (feed(nullptr,input,0)==1)
			mask->mPossibleChars[i]=true;
	}
	return true;
}

void Recognizer::optimize(){
	optimize(0);
}

void Recognizer::optimize(int recursionLevel){
	if (recursionLevel!=0 && mId!=0) return; /*stop on rule except at level 0*/
	_optimize(++recursionLevel);
}


CharRecognizer::CharRecognizer(int to_recognize, bool caseSensitive) : mToRecognize(to_recognize), mCaseSensitive(caseSensitive){
	if (::tolower(to_recognize)==::toupper(to_recognize)){
		/*not a case caseSensitive character*/
		mCaseSensitive=true;/*no need to perform a case insensitive match*/
	}else if (!caseSensitive){
		mToRecognize=::tolower(mToRecognize);
	}
}

size_t CharRecognizer::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	int c = (unsigned char)input[pos];
	if (mCaseSensitive){
		return c == mToRecognize ? 1 : string::npos;
	}
	return ::tolower(c) == mToRecognize ? 1 : string::npos;
}

void CharRecognizer::_optimize(int recursionLevel){

}

void CharRecognizer::_serialize(BinaryOutputStream &fstr){
	unsigned char charToRecognize = (unsigned char)mToRecognize;
	fstr<<charToRecognize;
	fstr<<(unsigned char)mCaseSensitive;
}


CharRecognizer::CharRecognizer(BinaryGrammarBuilder &istr) : Recognizer(istr){
	unsigned char toRecognize;
	istr>>toRecognize;
	mToRecognize = toRecognize;

	unsigned char tmp;
	istr>>tmp;
	mCaseSensitive = !!tmp;
}

shared_ptr<Selector> Selector::addRecognizer(const shared_ptr<Recognizer> &r){
	mElements.push_back(r);
	return static_pointer_cast<Selector> (shared_from_this());
}

bool Selector::_getTransitionMap(TransitionMap* mask){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		(*it)->getTransitionMap(mask);
	}
	return true;
}

size_t Selector::_feedExclusive(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		matched=(*it)->feed(ctx, input, pos);
		if (matched!=string::npos && matched>0) {
			return matched;
		}
	}
	return string::npos;
}

size_t Selector::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	if (mIsExclusive) return _feedExclusive(ctx, input, pos);

	size_t matched=0;
	size_t bestmatch=0;
	shared_ptr<HandlerContextBase> bestBranch;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		shared_ptr<HandlerContextBase> br;
		if (ctx) br=ctx->branch();
		matched=(*it)->feed(ctx, input, pos);
		if (matched!=string::npos && matched>bestmatch) {
			bestmatch=matched;
			if (bestBranch) ctx->removeBranch(bestBranch);
			bestBranch=br;
		}else{
			if (ctx)
				ctx->removeBranch(br);
		}
	}
	if (bestmatch==0) return string::npos;
	if (ctx && bestmatch!=string::npos){
		ctx->merge(bestBranch);
	}
	return bestmatch;
}

void Selector::_serialize(BinaryOutputStream &fstr){
	fstr<<(unsigned char) mIsExclusive;
	fstr<<(int)mElements.size();

	for(auto it = mElements.begin(); it != mElements.end(); ++it){
		(*it)->serialize(fstr);
	}
}

Selector::Selector(BinaryGrammarBuilder& istr) : Recognizer(istr){
	unsigned char tmp;
	istr>>tmp;
	mIsExclusive = !!tmp;

	int count;
	istr>>count;
	for (int i = 0 ; i < count ; ++i){
		auto rec = Recognizer::build(istr);
		if (rec) mElements.push_back(rec);
		else break;
	}
}


void Selector::_optimize(int recursionLevel){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		(*it)->optimize(recursionLevel);
	}
	TransitionMap *all=nullptr;
	bool intersectionFound=false;
	for (auto it=mElements.begin(); it!=mElements.end() && !intersectionFound; ++it){
		TransitionMap *cur=new TransitionMap();
		(*it)->getTransitionMap(cur);
		if (all){
			if (cur->intersect(all)){
				intersectionFound=true;
			}
			all->merge(cur);
			delete cur;
		}else all=cur;
	}
	if (all) delete all;
	if (!intersectionFound){
		mIsExclusive=true;
	}
}

Selector::Selector(bool isExclusive) : mIsExclusive(isExclusive){
}

ExclusiveSelector::ExclusiveSelector() : Selector(true){
}

ExclusiveSelector::ExclusiveSelector(BinaryGrammarBuilder &istr) : Selector(istr){
}

size_t ExclusiveSelector::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	return Selector::_feedExclusive(ctx, input, pos);
}


shared_ptr<Sequence> Sequence::addRecognizer(const shared_ptr<Recognizer> &element){
	mElements.push_back(element);
	return static_pointer_cast<Sequence>( shared_from_this());
}

bool Sequence::_getTransitionMap(TransitionMap* mask){
	bool isComplete=false;
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		if ((*it)->getTransitionMap(mask)) {
			isComplete=true;
			break;
		}
	}
	return isComplete;
}


size_t Sequence::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;
	size_t total=0;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		matched=(*it)->feed(ctx, input, pos);
		if (matched==string::npos){
			return string::npos;
		}
		pos+=matched;
		total+=matched;
	}
	return total;
}

void Sequence::_optimize(int recursionLevel){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it)
		(*it)->optimize(recursionLevel);
}

void Sequence::_serialize(BinaryOutputStream &fstr){
	fstr<<(int) mElements.size();
	for (auto it = mElements.begin(); it != mElements.end(); ++it){
		(*it)->serialize(fstr);
	}
}

Sequence::Sequence(BinaryGrammarBuilder& istr) : Recognizer(istr){
	int count;
	istr>>count;

	for (int i = 0 ; i < count ; ++i){
		shared_ptr<Recognizer> rec;
		rec = Recognizer::build(istr);
		if (rec) mElements.push_back(rec);
		else break;
	}
}


shared_ptr<Loop> Loop::setRecognizer(const shared_ptr<Recognizer> &element, int min, int max){
	mMin=min;
	mMax=max;
	mRecognizer=element;
	return static_pointer_cast<Loop>(shared_from_this());
}

size_t Loop::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;
	size_t total=0;
	int repeat;

	for(repeat=0;(mMax!=-1 ? repeat<mMax : true) && input[pos]!='\0';repeat++){
		matched=mRecognizer->feed(ctx, input, pos);
		if (matched==string::npos) break;
		total+=matched;
		pos+=matched;
	}
	if (repeat<mMin) return string::npos;
	return total;
}

bool Loop::_getTransitionMap(TransitionMap* mask){
	mRecognizer->getTransitionMap(mask);
	return mMin!=0; //we must say to upper layer that this loop recognizer is allowed to be optional by returning FALSE
}

void Loop::_serialize(BinaryOutputStream &fstr){
	fstr<<mMin;
	fstr<<mMax;
	mRecognizer->serialize(fstr);
}

Loop::Loop(BinaryGrammarBuilder & istr) : Recognizer(istr){
	istr>>mMin;
	istr>>mMax;
	mRecognizer = Recognizer::build(istr);
}

void Loop::_optimize(int recursionLevel){
	mRecognizer->optimize(recursionLevel);
}


CharRange::CharRange(int begin, int end) : mBegin(begin), mEnd(end){
}

size_t CharRange::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	int c = (unsigned char)input[pos];
	if (c >= mBegin && c <= mEnd) return 1;
	return string::npos;
}

void CharRange::_optimize(int recursionLevel){

}

void CharRange::_serialize(BinaryOutputStream& fstr){
	unsigned char begin, end;
	begin = (unsigned char)mBegin;
	end = (unsigned char)mEnd;
	fstr<<begin;
	fstr<<end;
}

CharRange::CharRange(BinaryGrammarBuilder& istr) : Recognizer(istr){
	unsigned char begin, end;
	istr>>begin;
	istr>>end;
	mBegin = begin;
	mEnd = end;
}



shared_ptr<CharRecognizer> Foundation::charRecognizer(int character, bool caseSensitive){
	return make_shared<CharRecognizer>(character, caseSensitive);
}

shared_ptr<Selector> Foundation::selector(bool isExclusive){
	return isExclusive ? make_shared<ExclusiveSelector>() : make_shared<Selector>();
}

shared_ptr<Sequence> Foundation::sequence(){
	return make_shared<Sequence>();
}

shared_ptr<Loop> Foundation::loop(){
	return make_shared<Loop>();
}

Literal::Literal(const string& lit) : mLiteral(tolower(lit)), mLiteralSize(mLiteral.size()) {

}

size_t Literal::_feed(const shared_ptr< ParserContextBase >& ctx, const string& input, size_t pos){
	size_t i;
	for(i=0;i<mLiteralSize;++i){
		if (::tolower(input[pos+i])!=mLiteral[i]) return string::npos;
	}
	return mLiteralSize;
}

void Literal::_serialize(BinaryOutputStream &fstr){
	fstr<<mLiteral;
}

Literal::Literal(BinaryGrammarBuilder &istr) : Recognizer(istr){
	istr >> mLiteral;
	mLiteralSize = mLiteral.size();
}

void Literal::_optimize(int recursionLevel){

}

bool Literal::_getTransitionMap(TransitionMap* mask){
	mask->mPossibleChars[::tolower(mLiteral[0])]=true;
	mask->mPossibleChars[::toupper(mLiteral[0])]=true;
	return true;
}

shared_ptr<Recognizer> Utils::literal(const string & lt){
	return make_shared<Literal>(lt);
}

shared_ptr<Recognizer> Utils::char_range(int begin, int end){
	return make_shared<CharRange>(begin, end);
}

shared_ptr<Recognizer> RecognizerPointer::getPointed(){
	return mRecognizer;
}

size_t RecognizerPointer::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	if (mRecognizer){
		return mRecognizer->feed(ctx, input, pos);
	}else{
		bctbx_fatal("RecognizerPointer with name '%s' is undefined", mName.c_str());
	}
	return string::npos;
}

void RecognizerPointer::_serialize(BinaryOutputStream &fstr){
	bctbx_fatal("The RecognizerPointer is not supposed to be serialized.");
}

//RecognizerPointer::RecognizerPointer(BinaryGrammarBuilder& istr) : Recognizer(istr){
//	//nothing to do
//}


void RecognizerPointer::setPointed(const shared_ptr<Recognizer> &r){
	mRecognizer=r;
}

void RecognizerPointer::_optimize(int recursionLevel){
	/*do not call optimize() on the pointed value to avoid a loop.
	 * The grammar will do it for all rules anyway*/
}


shared_ptr<Recognizer> RecognizerAlias::getPointed(){
	return mRecognizer;
}

size_t RecognizerAlias::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	if (mRecognizer){
		return mRecognizer->feed(ctx, input, pos);
	}else{
		bctbx_fatal("RecognizerAlias with name '%s' is undefined", mName.c_str());
	}
	return string::npos;
}

void RecognizerAlias::_serialize(BinaryOutputStream &fstr){
	mRecognizer->serialize(fstr);
}

RecognizerAlias::RecognizerAlias(BinaryGrammarBuilder& istr) : Recognizer(istr){
	mRecognizer = Recognizer::build(istr);
}


void RecognizerAlias::setPointed(const shared_ptr<Recognizer> &r){
	mRecognizer=r;
}

void RecognizerAlias::_optimize(int recursionLevel){
	/*do not call optimize() on the pointed value to avoid a loop.
	 * The grammar will do it for all rules anyway*/
}


Grammar::Grammar(const string& name) : mName(name){

}

Grammar::~Grammar() {
	for(auto it = mRecognizerPointers.begin(); it != mRecognizerPointers.end(); ++it) {
		shared_ptr<RecognizerPointer> pointer = dynamic_pointer_cast<RecognizerPointer>(*it);
		pointer->setPointed(nullptr);
	}
}

void Grammar::addRule(const string &argname, const shared_ptr<Recognizer> &rule){
	string name=tolower(argname);
	rule->setName(name);
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>((*it).second);
		if (pointer){
			pointer->setPointed(rule);
		}else{
			bctbx_fatal("Rule '%s' is being redefined !", name.c_str());
		}
	}
	/*in any case the map should contain real recognizers (not just pointers) */
	mRules[name]=rule;
}

void Grammar::extendRule(const string &argname, const shared_ptr<Recognizer> &rule){
	string name=tolower(argname);
	rule->setName("");
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		shared_ptr<Selector> sel=dynamic_pointer_cast<Selector>((*it).second);
		if (sel){
			sel->addRecognizer(rule);
		}else{
			bctbx_fatal("rule '%s' cannot be extended because it was not defined with a Selector.", name.c_str());
		}
	}else{
		bctbx_fatal("rule '%s' cannot be extended because it is not defined.", name.c_str());
	}
}

shared_ptr<Recognizer> Grammar::findRule(const string &argname){
	string name=tolower(argname);
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		return (*it).second;
	}
	return nullptr;
}

shared_ptr<Recognizer> Grammar::getRule(const string &argname){
	shared_ptr<Recognizer> ret=findRule(argname);

	if (ret){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>(ret);
		if (pointer){
			if (pointer->getPointed()){/*if pointer is defined return the pointed value directly*/
				return pointer->getPointed();
			}else{
				return pointer;
			}
		}
		return ret;
	}else{/*the rule doesn't exist yet: return a pointer*/
		shared_ptr<RecognizerPointer> recognizerPointer = make_shared<RecognizerPointer>();
		ret=recognizerPointer;
		string name=tolower(argname);
		ret->setName(string("@")+name);
		mRules[name]=ret;
		mRecognizerPointers.push_back(recognizerPointer);
	}
	return ret;
}

void Grammar::include(const shared_ptr<Grammar>& grammar){
	for(auto it=grammar->mRules.begin();it!=grammar->mRules.end();++it){
		if (mRules.find((*it).first)!=mRules.end()){
			BCTBX_SLOGE<<"Rule '"<<(*it).first<<"' is being redefined while including grammar '"<<grammar->mName<<"' into '"<<mName<<"'";
		}
		mRules[(*it).first]=(*it).second;
	}
}

bool Grammar::isComplete()const{
	bool ret=true;
	for(auto it=mRules.begin(); it!=mRules.end(); ++it){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>((*it).second);
		if (pointer && !pointer->getPointed()){
			BCTBX_SLOGE<<"Rule '"<<(*it).first<<"' is not defined.";
			ret=false;
		}
	}
	return ret;
}

void Grammar::optimize(){
	for(auto it=mRules.begin(); it!=mRules.end(); ++it){
		(*it).second->optimize();
	}
}


int Grammar::getNumRules() const{
	return (int)mRules.size();
}

int Grammar::save(const std::string &filename){
	BinaryOutputStream of;
	of.open(filename,ofstream::out|ofstream::trunc|ofstream::binary);
	if (of.fail()){
		BCTBX_SLOGE<<"Could not open "<<filename;
		return -1;
	}
	//magic string
	of<<"#!belr";
	//grammar name:
	of<<mName;
	//iterate over rules
	for (auto it = mRules.begin(); it != mRules.end(); ++it){
		(*it).second->serialize(of, true);
	}

	of.close();
	return 0;
}

int Grammar::load(const std::string &filename){
	BinaryGrammarBuilder ifs(*this);
	int err = 0;

	ifs.open(filename, ifstream::in|ifstream::binary);
	if (ifs.fail()){
		BCTBX_SLOGE<<"Could not open "<<filename;
		return -1;
	}
	/*extract the magic string*/
	string magic;
	ifs>>magic;
	if (magic != "#!belr"){
		ifs.close();
		BCTBX_SLOGE<<filename<< " is not a belr grammar binary file.";
		return -1;
	}

	/*extract the name of the grammar*/
	ifs>>mName;
	//load rules
	while(true){
		/*check if we are going to reach end of file at next read operation*/
		ifs.get();
		if (ifs.eof()) break;
		ifs.unget();

		shared_ptr<Recognizer> rule = Recognizer::build(ifs);
		if (!rule){
			bctbx_error("Fail to parse recognizer.");
			err = -1;
			break;
		}
		if (rule->getName().empty()){
			bctbx_error("Top level rule has no name");
			err = -1;
			break;
		}
		BCTBX_SLOGD<<"Added rule "<< rule->getName();
		addRule(rule->getName(), rule);
	}
	ifs.close();
	if (!isComplete()){
		bctbx_error("Grammar is not complete");
		err = -1;
	}
	return err;
}


string tolower(const string &str){
	string ret(str);
	transform(ret.begin(),ret.end(), ret.begin(), ::tolower);
	return ret;
}

}//end of namespace
