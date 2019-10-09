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

#ifndef _BELR_H_
#define _BELR_H_

#include <list>
#include <map>
#include <memory>
#include <string>


// =============================================================================

#ifdef _MSC_VER
	#ifdef BELR_STATIC
		#define BELR_PUBLIC
	#else
		#ifdef BELR_EXPORTS
			#define BELR_PUBLIC	__declspec(dllexport)
		#else
			#define BELR_PUBLIC	__declspec(dllimport)
		#endif
	#endif
#else
	#define BELR_PUBLIC
#endif

namespace belr{

BELR_PUBLIC std::string tolower(const std::string &str);

class ParserContextBase;
class BinaryOutputStream;
class BinaryGrammarBuilder;

/**
 * The transition map is an internal tool used to optimize recognizers
**/
struct TransitionMap{
	TransitionMap();
	bool intersect(const TransitionMap *other);
	bool intersect(const TransitionMap *other, TransitionMap *result); //performs a AND operation
	void merge(const TransitionMap *other); //Performs an OR operation

	bool mPossibleChars[256];
};

class Recognizer : public std::enable_shared_from_this<Recognizer>{
public:
	virtual ~Recognizer() = default;

	void setName(const std::string &name);
	const std::string &getName()const;
	BELR_PUBLIC size_t feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos);
	unsigned int getId()const{
		return mId;
	}
	bool getTransitionMap(TransitionMap *mask);
	void optimize();
	void optimize(int recursionLevel);
	void serialize(BinaryOutputStream &fstr, bool topLevel=false);
	static std::shared_ptr<Recognizer> build(BinaryGrammarBuilder &ifstr);
protected:
	Recognizer() = default;
	Recognizer(BinaryGrammarBuilder &istr);
	virtual void _serialize(BinaryOutputStream &fstr) = 0;
	/*returns true if the transition map is complete, false otherwise*/
	virtual bool _getTransitionMap(TransitionMap *mask);
	virtual void _optimize(int recursionLevel)=0;
	virtual size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) = 0;

	std::string mName;
	unsigned int mId = 0;
};

enum RecognizerTypeId{
	CharRecognizerId = 1,
	SelectorId,
	ExclusiveSelectorId,
	SequenceId,
	LoopId,
	CharRangeId,
	LiteralId,
	PointerId,
	AliasId,
	RuleRefId
};

class CharRecognizer : public Recognizer{
public:
	CharRecognizer(int to_recognize, bool caseSensitive=false);
	CharRecognizer(BinaryGrammarBuilder &istr);
private:
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;
	void _optimize(int recursionLevel) override;
	virtual void _serialize(BinaryOutputStream &fstr) override;

	int mToRecognize;
	bool mCaseSensitive;
};

class Selector : public Recognizer{
public:
	std::shared_ptr<Selector> addRecognizer(const std::shared_ptr<Recognizer> &element);
	Selector(bool isExclusive = false);
	Selector(BinaryGrammarBuilder &istr);
protected:
	void _optimize(int recursionLevel) override;
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;
	bool _getTransitionMap(TransitionMap *mask) override;
	virtual void _serialize(BinaryOutputStream &fstr) override;

	size_t _feedExclusive(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos);

	std::list<std::shared_ptr<Recognizer>> mElements;
	bool mIsExclusive = false;
};

/**This is an optimization of the first one for the case where there can be only a single match*/
class ExclusiveSelector : public Selector{
public:
	ExclusiveSelector();
	ExclusiveSelector(BinaryGrammarBuilder &istr);
private:
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;
};

class Sequence : public Recognizer{
public:
	Sequence() = default;
	Sequence(BinaryGrammarBuilder &istr);
	bool _getTransitionMap(TransitionMap *mask) override;
	std::shared_ptr<Sequence> addRecognizer(const std::shared_ptr<Recognizer> &element);

protected:
	virtual void _serialize(BinaryOutputStream &fstr) override;
	void _optimize(int recursionLevel) override;

private:
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;

	std::list<std::shared_ptr<Recognizer>> mElements;
};

class Loop : public Recognizer{
public:
	Loop() = default;
	Loop(BinaryGrammarBuilder &istr);
	bool _getTransitionMap(TransitionMap *mask) override;
	std::shared_ptr<Loop> setRecognizer(const std::shared_ptr<Recognizer> &element, int min=0, int max=-1);

protected:
	virtual void _serialize(BinaryOutputStream &fstr) override;
	void _optimize(int recursionLevel) override;

private:
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;

	std::shared_ptr<Recognizer> mRecognizer;
	int mMin = 0;
	int mMax = -1;
};


class Foundation{
public:
	static std::shared_ptr<CharRecognizer> charRecognizer(int character, bool caseSensitive=false);
	static std::shared_ptr<Selector> selector(bool isExclusive=false);
	static std::shared_ptr<Sequence> sequence();
	static std::shared_ptr<Loop> loop();
};

/*this is an optimization of a selector with multiple individual char recognizer*/
class CharRange : public Recognizer{
public:
	CharRange(int begin, int end);
	CharRange(BinaryGrammarBuilder &istr);
private:
	virtual void _serialize(BinaryOutputStream &fstr) override;
	void _optimize(int recursionLevel) override;
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;

	int mBegin;
	int mEnd;
};

class Literal : public Recognizer{
public:
	Literal(const std::string &lit);
	Literal(BinaryGrammarBuilder &istr);
	bool _getTransitionMap(TransitionMap *mask) override;

private:
	void _optimize(int recursionLevel) override;
	virtual void _serialize(BinaryOutputStream &fstr) override;
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;

	std::string mLiteral;
	size_t mLiteralSize;
};

class Utils{
public:
	static std::shared_ptr<Recognizer> literal(const std::string & lt);
	static std::shared_ptr<Recognizer> char_range(int begin, int end);
};

/**
 * The RecognizerPointer just points to another recognizer and delegates everything to the pointed recognizer.
 * It is a place holder when a rule not-yet-defined appears when parsing an ABNF grammar.
 * The pointed recognizer is set to the rule when it comes defined.
**/
class RecognizerPointer :  public Recognizer{
public:
	RecognizerPointer() = default;
	//RecognizerPointer(BinaryGrammarBuilder &istr);
	std::shared_ptr<Recognizer> getPointed();
	void setPointed(const std::shared_ptr<Recognizer> &r);

private:
	void _optimize(int recursionLevel) override;
	virtual void _serialize(BinaryOutputStream &fstr) override;
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;

	std::shared_ptr<Recognizer> mRecognizer;
};


/**
 * The RecognizerAlias points to another recognizer and delegates everything. It is necessary to represents ABNF statements like
 * rule2 = rule1
 * It is different from the RecognizerPointer in its function, however it behaves exactly the same way.
 * A different type is necessary to distinguish between the two usage.
**/
class RecognizerAlias :  public Recognizer{
public:
	RecognizerAlias() = default;
	RecognizerAlias(BinaryGrammarBuilder &istr);
	std::shared_ptr<Recognizer> getPointed();
	void setPointed(const std::shared_ptr<Recognizer> &r);

private:
	void _optimize(int recursionLevel) override;
	virtual void _serialize(BinaryOutputStream &fstr) override;
	size_t _feed(const std::shared_ptr<ParserContextBase> &ctx, const std::string &input, size_t pos) override;
	std::shared_ptr<Recognizer> mRecognizer;
};

/**
 * Grammar class represents an ABNF grammar, with all its rules.
**/
class Grammar{
public:
	/**
	 * Initialize an empty grammar, giving a name for debugging.
	**/
	BELR_PUBLIC Grammar(const std::string &name);

	BELR_PUBLIC ~Grammar();

	/**
	 * Include another grammar into this grammar.
	**/
	BELR_PUBLIC void include(const std::shared_ptr<Grammar>& grammar);
	/**
	 * Add a rule to the grammar.
	 * @param name the name of the rule
	 * @param rule the rule recognizer, must be an instance of belr::Recognizer.
	 * @note The grammar takes ownership of the recognizer, which must not be used outside of this grammar.
	 * TODO: use unique_ptr to enforce this, or make a copy ?
	**/
	void addRule(const std::string & name, const std::shared_ptr<Recognizer> &rule);
	/**
	 * Extend a rule from the grammar.
	 * This corresponds to the '/=' operator of ABNF definition.
	 * @param name the name of the rule to extend.
	 * @param rule the recognizer of the extension.
	**/
	void extendRule(const std::string & name, const std::shared_ptr<Recognizer> &rule);
	/**
	 * Find a rule from the grammar, given its name.
	 * @param name the name of the rule
	 * @return the recognizer implementing this rule. Is nullptr if the rule doesn't exist in the grammar.
	**/
	BELR_PUBLIC std::shared_ptr<Recognizer> findRule(const std::string &name);
	/**
	 * Find a rule from the grammar, given its name.
	 * Unlike findRule(), getRule() never returns nullptr.
	 * If the rule is not (yet) defined, it returns an undefined pointer, that will be set later if the rule gets defined.
	 * This mechanism is required to allow defining rules in any order, and defining rules that call themselve recursively.
	 * @param name the name of the rule to get
	 * @return the recognizer implementing the rule, or a RecognizerPointer if the rule isn't yet defined.
	**/
	BELR_PUBLIC std::shared_ptr<Recognizer> getRule(const std::string &name);
	/**
	 * Returns true if the grammar is complete, that is all rules are defined.
	 * In other words, a grammar is complete if no rule depends on another rule which is not defined.
	**/
	BELR_PUBLIC bool isComplete()const;
	/**
	 * Optimize the grammar. This is required to obtain good performance of the recognizers implementing the rule.
	 * The optimization step consists in checking whether belr::Selector objects in the grammar are exclusive or not.
	 * A selector is said exclusive when a single sub-rule can match. Knowing this in advance optimizes the processing because no branch
	 * context is to be created to explore the different choices of the selector recognizer.
	**/
	BELR_PUBLIC void optimize();
	/**
	 * Return the number of rules in this grammar.
	**/
	BELR_PUBLIC int getNumRules()const;
	/**
	 * Save the grammar into a binary file.
	**/
	BELR_PUBLIC int save(const std::string &filename);
	/**
	 * Load the grammar from a binary file
	**/
	BELR_PUBLIC int load(const std::string &filename);
private:
	std::map<std::string,std::shared_ptr<Recognizer>> mRules;
	//The recognizer pointers create loops in the chain of recognizer, preventing shared_ptr<> to be released.
	//We store them in this list so that we can reset them manually to break the loop of reference.
	std::list<std::shared_ptr<RecognizerPointer>> mRecognizerPointers;
	std::string mName;
};



}//end of namespace

#endif
