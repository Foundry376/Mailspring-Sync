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

#ifndef _PARSER_H_
#define _PARSER_H_

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>
#include <sstream>

#include "belr.h"

// =============================================================================

namespace belr {


template<typename _parserElementT>
class CollectorBase{
	public:
	virtual ~CollectorBase() = default;
	virtual void invokeWithChild(_parserElementT obj, _parserElementT child)=0;
	virtual void invokeWithValue(_parserElementT obj, const std::string &value) = 0;
};

template <class T, class U>
inline T universal_pointer_cast(const std::shared_ptr<U>& sp){
	return std::static_pointer_cast<typename T::element_type>(sp);
}

template <class T, class U>
inline T universal_pointer_cast(U * p){
	return static_cast<T>(p);
}

template <typename _functorT, typename _parserElementT>
class ParserCollector : public CollectorBase<_parserElementT>{
public:
	ParserCollector(_functorT func) : mFunc(func){
	}
private:
	virtual void invokeWithChild(_parserElementT obj, _parserElementT child) override;
	virtual void invokeWithValue(_parserElementT obj, const std::string &value) override;
	template <typename _valueT>
	inline void _invokeWithValue(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, std::string>::value, const std::string&>::type value){
		mFunc(universal_pointer_cast<typename _functorT::first_argument_type>(obj), value);
	}
	template <typename _valueT>
	inline void _invokeWithValue(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, _parserElementT>::value, const std::string&>::type value){
		// no op.
	}
	template <typename _valueT>
	inline void _invokeWithValue(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, int>::value, const std::string&>::type value){
		mFunc(universal_pointer_cast<typename _functorT::first_argument_type>(obj), std::atoi(value.c_str()));
	}
	template <typename _valueT>
	inline void _invokeWithChild(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, std::string>::value, _parserElementT>::type child){
	}
	template <typename _valueT>
	inline void _invokeWithChild(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, int>::value, _parserElementT>::type child){
	}
	template <typename _valueT>
	inline void _invokeWithChild(_parserElementT obj, typename std::enable_if<std::is_convertible<_valueT, _parserElementT>::value, _parserElementT>::type child){
		mFunc(universal_pointer_cast<typename std::remove_reference<typename _functorT::first_argument_type>::type>(obj), 
			universal_pointer_cast<typename std::remove_reference<typename _functorT::second_argument_type>::type>(child));
	}
	_functorT mFunc;
};

template <typename _parserElementT>
class HandlerContext;

template <typename _parserElementT>
class Parser;

class HandlerContextBase;

template <typename _parserElementT>
class ParserHandlerBase{
	friend class HandlerContext<_parserElementT>;

public:
	virtual ~ParserHandlerBase() = default;
	virtual _parserElementT invoke(const std::string &input, size_t begin, size_t count)=0;

	std::shared_ptr<HandlerContext<_parserElementT>> createContext();
	inline const std::string &getRulename() const {
		return mRulename;
	}

protected:
	void releaseContext(const std::shared_ptr<HandlerContext<_parserElementT>> &ctx);
	ParserHandlerBase(const Parser<_parserElementT> &parser, const std::string &name);
	void installCollector(const std::string &rulename, CollectorBase<_parserElementT>* collector);
	CollectorBase<_parserElementT>* getCollector(unsigned int rule_id)const;

private:
	std::map<unsigned int, std::unique_ptr<CollectorBase<_parserElementT>> > mCollectors;
	const Parser<_parserElementT> &mParser;
	std::string mRulename;
	std::shared_ptr<HandlerContext<_parserElementT>> mCachedContext;
};

template <typename _createElementFn, typename _parserElementT>
class ParserHandler :  public ParserHandlerBase<_parserElementT>{
public:
	typedef typename _createElementFn::result_type _derivedParserElementT;
	ParserHandler(const Parser<_parserElementT> &parser, const std::string &rulename, _createElementFn create)
		: ParserHandlerBase<_parserElementT>(parser, rulename), mHandlerCreateFunc(create){}
	_parserElementT invoke(const std::string &input, size_t begin, size_t count) override;

	template <typename _functorT>
	ParserHandler<_createElementFn, _parserElementT> *setCollector(const std::string &child_rule_name, _functorT fn){
		this->installCollector(child_rule_name, new ParserCollector<_functorT, _parserElementT>(fn) );
		return this;
	}
private:
	template <typename _funcT>
	typename std::enable_if<std::is_convertible<typename _funcT::first_argument_type,std::string>::value, _derivedParserElementT>::type _invoke(const std::string &value, size_t begin, size_t count){
		// Case where the create func accepts two strings for rulename and matched characters.
		return mHandlerCreateFunc(this->getRulename(), value.substr(begin, count));
	}
	template <typename _funcT>
	typename std::enable_if<std::is_convertible<_funcT, std::function<_derivedParserElementT()>>::value, _derivedParserElementT>::type _invoke(const std::string &value, size_t begin, size_t count){
		// Case where the create func accepts two strings for rulename and matched characters.
		return mHandlerCreateFunc();
	}
	_createElementFn mHandlerCreateFunc;
};

template <typename _parserElementT>
class Assignment{
public:
	Assignment(CollectorBase<_parserElementT> *c, size_t begin, size_t count, const std::shared_ptr<HandlerContext<_parserElementT>> &child)
	: mCollector(c), mBegin(begin), mCount(count), mChild(child) {}

	void invoke(_parserElementT parent, const std::string &input);

private:
	CollectorBase<_parserElementT> * mCollector;//not a shared_ptr for optimization, the collector cannot disapear
	size_t mBegin;
	size_t mCount;
	std::shared_ptr<HandlerContext<_parserElementT>> mChild;
};

class HandlerContextBase : public std::enable_shared_from_this<HandlerContextBase>{
public:
	BELR_PUBLIC virtual ~HandlerContextBase() = default;
};

template <typename _parserElementT>
class HandlerContext : public HandlerContextBase{
public:
	HandlerContext(ParserHandlerBase<_parserElementT> *handler);

	void setChild(unsigned int subrule_id, size_t begin, size_t count, const std::shared_ptr<HandlerContext> &child);
	_parserElementT realize(const std::string &input, size_t begin, size_t count);
	std::shared_ptr<HandlerContext<_parserElementT>> branch();
	void merge(const std::shared_ptr<HandlerContext<_parserElementT>> &other);
	size_t getLastIterator()const;
	void undoAssignments(size_t pos);
	void recycle();

private:
	ParserHandlerBase<_parserElementT> & mHandler;
	std::vector<Assignment<_parserElementT>> mAssignments;
};

struct ParserLocalContext{
	void set(const std::shared_ptr<HandlerContextBase>& hc, const std::shared_ptr<Recognizer>& rec, size_t pos){
		mHandlerContext=hc;
		mRecognizer=rec.get();
		mAssignmentPos=pos;
	}
	std::shared_ptr<HandlerContextBase> mHandlerContext;
	Recognizer * mRecognizer = nullptr; //not a shared ptr to optimize, the object can't disapear in the context of use of ParserLocalContext.
	size_t mAssignmentPos = 0;
};

class ParserContextBase{
public:
	virtual ~ParserContextBase() = default;

	virtual void beginParse(ParserLocalContext &ctx, const std::shared_ptr<Recognizer> &rec)=0;
	virtual void endParse(const ParserLocalContext &ctx, const std::string &input, size_t begin, size_t count)=0;
	virtual std::shared_ptr<HandlerContextBase> branch()=0;
	virtual void merge(const std::shared_ptr<HandlerContextBase> &other)=0;
	virtual void removeBranch(const std::shared_ptr<HandlerContextBase> &other)=0;
};

template <typename _parserElementT>
class ParserContext : public ParserContextBase{
public:
	ParserContext(Parser<_parserElementT> &parser);
	_parserElementT createRootObject(const std::string &input);

protected:
	void beginParse(ParserLocalContext &ctx, const std::shared_ptr<Recognizer> &rec) override;
	void endParse(const ParserLocalContext &ctx, const std::string &input, size_t begin, size_t count) override;
	std::shared_ptr<HandlerContextBase> branch() override;
	void merge(const std::shared_ptr<HandlerContextBase> &other) override;
	void removeBranch(const std::shared_ptr<HandlerContextBase> &other) override;

	void  _beginParse(ParserLocalContext &ctx, const std::shared_ptr<Recognizer> &rec);
	void _endParse(const ParserLocalContext &ctx, const std::string &input, size_t begin, size_t count);
	std::shared_ptr<HandlerContext<_parserElementT>> _branch();
	void _merge(const std::shared_ptr<HandlerContext<_parserElementT>> &other);
	void _removeBranch(const std::shared_ptr<HandlerContext<_parserElementT>> &other);

private:
	Parser<_parserElementT> & mParser;
	std::list<std::shared_ptr<HandlerContext<_parserElementT>>> mHandlerStack;
	std::shared_ptr<HandlerContext<_parserElementT>> mRoot;
};

/**
 * Parser class.
 * This template class allows to parse a text input using a Grammar object describing the language of the input to be parsed.
 * The template argument _parserElementT must be a base class for all elements that will be created to represent the result of the parsing.
 * This can be 'void*' if the parser is implemented in C, but can also be any C++ class provided that each type representing a parsed entity
 * inherits from this class.
**/
template <typename _parserElementT>
class Parser{
friend class ParserContext<_parserElementT>;
friend class ParserHandlerBase<_parserElementT>;

public:
	Parser(const std::shared_ptr<Grammar> &grammar);

	template <typename _createElementFn>
	ParserHandler<_createElementFn,_parserElementT> * setHandler(const std::string &rulename, _createElementFn handler){
		auto ret = new ParserHandler<_createElementFn,_parserElementT>(*this, rulename, handler);
		installHandler(ret);
		return ret;

	}
	_parserElementT parseInput(const std::string &rulename, const std::string &input, size_t *parsed_size);

private:
	ParserHandlerBase<_parserElementT> *getHandler(unsigned int);
	void installHandler(ParserHandlerBase<_parserElementT> * handler);
	std::shared_ptr<Grammar> mGrammar;
	std::map<unsigned int, std::unique_ptr<ParserHandlerBase<_parserElementT>>> mHandlers;
	std::unique_ptr<ParserHandlerBase<_parserElementT>> mNullHandler;
	std::unique_ptr<CollectorBase<_parserElementT>> mNullCollector;
};

class DebugElement{
public:
	DebugElement(const std::string &rulename, const std::string &value);
	static std::shared_ptr<DebugElement> create(const std::string &rulename, const std::string &value);
	void addChild(const std::shared_ptr<DebugElement> &e);
	BELR_PUBLIC void findChildren(const std::string &rulename, std::list<std::shared_ptr<DebugElement>> &retlist)const;
	BELR_PUBLIC std::ostream &tostream(int level, std::ostream &str)const;
	BELR_PUBLIC const std::string &getValue()const;
private:

	std::string mRulename;
	std::string mValue;
	std::list<std::shared_ptr<DebugElement>> mChildren;
};

class DebugParser : protected Parser<std::shared_ptr<DebugElement>>{
public:
	BELR_PUBLIC DebugParser(const std::shared_ptr<Grammar> &grammar);
	BELR_PUBLIC void setObservedRules(const std::list<std::string> &rules);
	BELR_PUBLIC std::shared_ptr<DebugElement> parseInput(const std::string &rulename, const std::string &input, size_t *parsed_size);
};

//Utility functions for handlers/collectors objects instantiation and properties accessors
template <typename _retT>
inline std::function< std::shared_ptr<_retT> ()> make_fn() {
	return std::bind(&std::make_shared<_retT>);
}

template <typename _retT>
inline std::function< _retT ()> make_fn(_retT (*arg)()){
	return std::function<_retT ()>(arg);
}

template <typename _retT, typename _arg1T, typename _arg2T>
inline std::function< _retT (_arg1T,_arg2T)> make_fn(_retT (*arg)(_arg1T,_arg2T)){
	return std::function< _retT (_arg1T,_arg2T)>(arg);
}

template <typename _klassT, typename _argT>
inline std::function< void (_klassT*,_argT)> make_fn(void (_klassT::*arg)(_argT)){
	return std::function< void (_klassT*,_argT)>(std::mem_fn(arg));
}

template <typename _klassT, typename _argT>
inline std::function< void (std::shared_ptr<_klassT>,_argT)> make_sfn(void (_klassT::*arg)(_argT)){
	return std::function< void (std::shared_ptr<_klassT>,_argT)>(std::mem_fn(arg));
}

// =============================================================================
// Parser impl.
// =============================================================================

BELR_PUBLIC void fatal(const char *message);

template <typename _functorT, typename _parserElementT>
void ParserCollector<_functorT, _parserElementT>::invokeWithChild(_parserElementT obj, _parserElementT child){
	_invokeWithChild<typename _functorT::second_argument_type>(obj, child);
}

template <typename _functorT, typename _parserElementT>
void ParserCollector<_functorT, _parserElementT>::invokeWithValue(_parserElementT obj, const std::string &value){
	_invokeWithValue<typename _functorT::second_argument_type>(obj, value);
}


template <typename _parserElementT>
void Assignment<_parserElementT>::invoke(_parserElementT parent, const std::string &input){
	if (mChild){
		mCollector->invokeWithChild(parent, mChild->realize(input,mBegin,mCount));
	}else{
		mCollector->invokeWithValue(parent, input.substr(mBegin, mCount));
	}
}


//
// HandlerContext template class implementation
//

template <typename _parserElementT>
HandlerContext<_parserElementT>::HandlerContext(ParserHandlerBase<_parserElementT> *handler) :
	mHandler(*handler){
}

template <typename _parserElementT>
void HandlerContext<_parserElementT>::setChild(unsigned int subrule_id, size_t begin, size_t count, const std::shared_ptr<HandlerContext<_parserElementT>> &child){
	auto collector=mHandler.getCollector(subrule_id);
	if (collector){
		mAssignments.push_back(Assignment<_parserElementT>(collector, begin, count, child));
	}
}

template <typename _parserElementT>
_parserElementT HandlerContext<_parserElementT>::realize(const std::string &input, size_t begin, size_t count){
	_parserElementT ret=mHandler.invoke(input, begin, count);
	for (auto it=mAssignments.begin(); it!=mAssignments.end(); ++it){
		(*it).invoke(ret,input);
	}
	return ret;
}

template <typename _parserElementT>
std::shared_ptr<HandlerContext<_parserElementT>> HandlerContext<_parserElementT>::branch(){
	return mHandler.createContext();
}

template <typename _parserElementT>
void HandlerContext<_parserElementT>::merge(const std::shared_ptr<HandlerContext<_parserElementT>> &other){
	for (auto it=other->mAssignments.begin();it!=other->mAssignments.end();++it){
		mAssignments.emplace_back(*it);
	}
}

template <typename _parserElementT>
size_t HandlerContext<_parserElementT>::getLastIterator()const{
	return mAssignments.size();
}

template <typename _parserElementT>
void HandlerContext<_parserElementT>::undoAssignments(size_t pos){
	mAssignments.erase(mAssignments.begin()+(long)pos ,mAssignments.end());
}

template <typename _parserElementT>
void HandlerContext< _parserElementT >::recycle(){
	mAssignments.clear();
	mHandler.releaseContext(std::static_pointer_cast<HandlerContext< _parserElementT >>(shared_from_this()));
}

//
// ParserHandlerBase template class implementation
//

template <typename _parserElementT>
ParserHandlerBase<_parserElementT>::ParserHandlerBase(const Parser<_parserElementT> &parser, const std::string &name) : mParser(parser), mRulename(tolower(name)), mCachedContext(nullptr) {
}

template <typename _parserElementT>
void ParserHandlerBase<_parserElementT>::installCollector(const std::string &rulename, CollectorBase<_parserElementT> *collector){
	std::shared_ptr<Recognizer> rec=mParser.mGrammar->findRule(rulename);
	if (!rec){
		std::ostringstream ostr;
		ostr<<"There is no rule '"<<rulename<<"' in the grammar.";
		fatal(ostr.str().c_str());
		return;
	}
	mCollectors[rec->getId()].reset(collector);
}

template <typename _parserElementT>
CollectorBase<_parserElementT> * ParserHandlerBase<_parserElementT>::getCollector(unsigned int rule_id)const{
	auto it=mCollectors.find(rule_id);
	if (it!=mCollectors.end()) return (*it).second.get();
	return mParser.mNullCollector.get();
}

template <typename _parserElementT>
void ParserHandlerBase< _parserElementT >::releaseContext(const std::shared_ptr<HandlerContext<_parserElementT>> &ctx){
	mCachedContext=ctx;
}

template <typename _parserElementT>
std::shared_ptr<HandlerContext<_parserElementT>> ParserHandlerBase<_parserElementT>::createContext(){
	if (mCachedContext) {
		std::shared_ptr<HandlerContext<_parserElementT>> ret=mCachedContext;
		mCachedContext.reset();
		return ret;
	}
	return std::make_shared<HandlerContext<_parserElementT>>(this);
}

//
// ParserHandler template implementation
//

template <typename _createElementFn, typename _parserElementT>
_parserElementT ParserHandler<_createElementFn,_parserElementT>::invoke(const std::string &input, size_t begin, size_t count){
	return universal_pointer_cast<_parserElementT>(_invoke<_createElementFn>(input, begin, count));
}


//
// ParserContext template class implementation
//

template <typename _parserElementT>
ParserContext<_parserElementT>::ParserContext(Parser<_parserElementT> &parser) : mParser(parser) {
}

template <typename _parserElementT>
inline void ParserContext<_parserElementT>::_beginParse(ParserLocalContext & lctx, const std::shared_ptr<Recognizer> &rec){
	std::shared_ptr<HandlerContextBase> ctx;

	auto h=mParser.getHandler(rec->getId());
	if (h){
		ctx=h->createContext();
		mHandlerStack.push_back(std::static_pointer_cast<HandlerContext<_parserElementT>>(ctx));
	}
	if (mHandlerStack.empty()){
		fatal("Cannot parse when mHandlerStack is empty. You must define a top-level rule handler.");
	}
	lctx.set(ctx,rec,mHandlerStack.back()->getLastIterator());
}

template <typename _parserElementT>
inline  void ParserContext<_parserElementT>::_endParse(const ParserLocalContext &localctx, const std::string &input, size_t begin, size_t count){
	if (localctx.mHandlerContext){
		mHandlerStack.pop_back();
		if (count!=std::string::npos && count>0){
			if (!mHandlerStack.empty()){
				/*assign object to parent */
				mHandlerStack.back()->setChild(localctx.mRecognizer->getId(), begin, count,
					std::static_pointer_cast<HandlerContext< _parserElementT >> (localctx.mHandlerContext));

			}else{
				/*no parent, this is our root object*/
				mRoot=std::static_pointer_cast<HandlerContext< _parserElementT >>(localctx.mHandlerContext);
			}
		}else{
			//no match
			std::static_pointer_cast<HandlerContext< _parserElementT >>(localctx.mHandlerContext)->recycle();
		}
	}else{
		if (count!=std::string::npos && count>0){
			/*assign std::string to parent */
			if (localctx.mRecognizer->getId()!=0)
				mHandlerStack.back()->setChild(localctx.mRecognizer->getId(), begin, count, nullptr);
		}else{
			mHandlerStack.back()->undoAssignments(localctx.mAssignmentPos);
		}
	}
}

template <typename _parserElementT>
_parserElementT ParserContext<_parserElementT>::createRootObject(const std::string &input){
	 return mRoot ? mRoot->realize(input,0,input.size()) : nullptr;
}

template <typename _parserElementT>
inline std::shared_ptr<HandlerContext<_parserElementT>> ParserContext<_parserElementT>::_branch(){
	if (mHandlerStack.empty()){
		fatal("Cannot branch while stack is empty");
	}
	std::shared_ptr<HandlerContext<_parserElementT>> ret=mHandlerStack.back()->branch();
	mHandlerStack.push_back(ret);
	return ret;
}

template <typename _parserElementT>
inline void ParserContext<_parserElementT>::_merge(const std::shared_ptr<HandlerContext<_parserElementT>> &other){
	if (mHandlerStack.back()!=other){
		fatal("The branch being merged is not the last one of the stack !");
	}
	mHandlerStack.pop_back();
	mHandlerStack.back()->merge(other);
	other->recycle();
}

template <typename _parserElementT>
inline void ParserContext<_parserElementT>::_removeBranch(const std::shared_ptr<HandlerContext<_parserElementT>> &other){
	auto it=find(mHandlerStack.rbegin(), mHandlerStack.rend(),other);
	if (it==mHandlerStack.rend()){
		fatal("A branch could not be found in the stack while removing it !");
	}else{
		advance(it,1);
		mHandlerStack.erase(it.base());
	}
	other->recycle();
}

template <typename _parserElementT>
void ParserContext<_parserElementT>::beginParse(ParserLocalContext &ctx, const std::shared_ptr<Recognizer> &rec){
	_beginParse(ctx, rec);
}

template <typename _parserElementT>
void ParserContext<_parserElementT>::endParse(const ParserLocalContext &localctx, const std::string &input, size_t begin, size_t count){
	_endParse(localctx, input, begin, count);
}

template <typename _parserElementT>
std::shared_ptr<HandlerContextBase> ParserContext<_parserElementT>::branch(){
	return _branch();
}

template <typename _parserElementT>
void ParserContext<_parserElementT>::merge(const std::shared_ptr<HandlerContextBase> &other){
	_merge(std::static_pointer_cast<HandlerContext<_parserElementT>>(other));
}

template <typename _parserElementT>
void ParserContext<_parserElementT>::removeBranch(const std::shared_ptr<HandlerContextBase> &other){
	_removeBranch(std::static_pointer_cast<HandlerContext<_parserElementT>>(other));
}

//
// Parser template class implementation
//

template <typename _parserElementT>
Parser<_parserElementT>::Parser(const std::shared_ptr<Grammar> &grammar) : mGrammar(grammar) {
	if (!mGrammar->isComplete()){
		fatal("Grammar not complete, aborting.");
		return;
	}
}

template <typename _parserElementT>
ParserHandlerBase<_parserElementT> *Parser<_parserElementT>::getHandler(unsigned int rule_id){
	auto it=mHandlers.find(rule_id);
	if (it==mHandlers.end()) return mNullHandler.get();
	return (*it).second.get();
}

template <typename _parserElementT>
void Parser<_parserElementT>::installHandler(ParserHandlerBase<_parserElementT> *handler){
	std::shared_ptr<Recognizer> rec=mGrammar->findRule(handler->getRulename());
	if (!rec){
		std::ostringstream str;
		str<<"There is no rule '"<<handler->getRulename()<<"' in the grammar.";
		fatal(str.str().c_str());
	}
	mHandlers[rec->getId()].reset(handler);
}

template <typename _parserElementT>
_parserElementT Parser<_parserElementT>::parseInput(const std::string &rulename, const std::string &input, size_t *parsed_size){
	size_t parsed;
	std::shared_ptr<Recognizer> rec=mGrammar->getRule(rulename);
	auto pctx=std::make_shared<ParserContext<_parserElementT>>(*this);

	//auto t_start = std::chrono::high_resolution_clock::now();
	parsed=rec->feed(pctx, input, 0);
	//auto t_end = std::chrono::high_resolution_clock::now();
	//cout<<"Recognition done in "<<std::chrono::duration<double, std::milli>(t_end-t_start).count()<<" milliseconds"<<std::endl;
	if (parsed_size) *parsed_size=parsed;
	auto ret= pctx->createRootObject(input);
	return ret;
}
}

#endif
