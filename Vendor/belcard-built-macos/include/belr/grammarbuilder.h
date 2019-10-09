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

#ifndef _GRAMMARBUILDER_H_
#define _GRAMMARBUILDER_H_

#include "parser.h"

// =============================================================================

namespace belr{
class ABNFAlternation;

class ABNFBuilder{
public:
	virtual ~ABNFBuilder() = default;

	virtual std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar)=0;
};

class ABNFRule : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setName(const std::string &name);
	void setDefinedAs(const std::string &defined_as);
	void setAlternation(const std::shared_ptr<ABNFAlternation> &a);
	bool isExtension()const;
	const std::string &getName()const{
		return mName;
	}

	static std::shared_ptr<ABNFRule> create();

private:
	std::shared_ptr<ABNFAlternation> mAlternation;
	std::string mName;
	std::string mDefinedAs;
};

class ABNFRuleList : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void addRule(const std::shared_ptr<ABNFRule> & rule);

	static std::shared_ptr<ABNFRuleList> create();

private:
	std::list<std::shared_ptr<ABNFRule>> mRules;
};

class ABNFNumval : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setDecVal(const std::string &decval);
	void setHexVal(const std::string &hexval);
	void setBinVal(const std::string &binval);

	static std::shared_ptr<ABNFNumval> create();

private:
	void parseValues(const std::string &val, int base);
	std::vector<int> mValues;
	bool mIsRange = false;
};

class ABNFElement : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setElement(const std::shared_ptr<ABNFBuilder> &e);
	void setRulename(const std::string &rulename);
	void setCharVal(const std::string &charval);
	void setProseVal(const std::string &prose);

	static std::shared_ptr<ABNFElement> create();

private:
	std::shared_ptr<ABNFBuilder> mElement;
	std::string mRulename;
	std::string mCharVal;
};

class ABNFGroup : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setAlternation(const std::shared_ptr<ABNFAlternation> &a);

	static std::shared_ptr<ABNFGroup> create();

private:
	std::shared_ptr<ABNFAlternation> mAlternation;
};

class ABNFRepetition : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setRepeat(const std::string &r);
	void setMin(int min);
	void setMax(int max);
	void setCount(int count);
	void setElement(const std::shared_ptr<ABNFElement> &e);

	static std::shared_ptr<ABNFRepetition> create();

private:
	int mMin = 0;
	int mMax = -1;
	int mCount = -1;
	std::string mRepeat;
	std::shared_ptr<ABNFElement> mElement;
};

class ABNFOption : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void setAlternation(const std::shared_ptr<ABNFAlternation> &a);

	static std::shared_ptr<ABNFOption> create();

private:
	std::shared_ptr<ABNFAlternation> mAlternation;
};

class ABNFConcatenation : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void addRepetition(const std::shared_ptr<ABNFRepetition> &r);

	static std::shared_ptr<ABNFConcatenation> create();

private:
	std::list<std::shared_ptr<ABNFRepetition>> mRepetitions;
};

class ABNFAlternation : public ABNFBuilder{
public:
	std::shared_ptr<Recognizer> buildRecognizer(const std::shared_ptr<Grammar> &grammar) override;

	void addConcatenation(const std::shared_ptr<ABNFConcatenation> &c);
	std::shared_ptr<Recognizer> buildRecognizerNoOptim(const std::shared_ptr<Grammar> &grammar);

	static std::shared_ptr<ABNFAlternation> create();

private:
	std::list<std::shared_ptr<ABNFConcatenation>> mConcatenations;
};

/**
 * The ABNFGrammarBuilder builds a Grammar object from an ABNF grammar defined in a text file.
**/
class ABNFGrammarBuilder{
public:
	/**
	 * Initialize the builder.
	**/
	BELR_PUBLIC ABNFGrammarBuilder();
	/**
	 * Create a grammar from an ABNF grammar defined in the string pointed by abnf.
	 * An optional Grammar argument corresponding to a grammar to include can be passed.
	 * Usually the belr::CoreRules grammar is required for most IETF text protocols.
	 * The returned grammar can be used to instanciate a belr::Parser object capable of parsing
	 * the protocol or language described in the grammar.
	 * @param abnf the string that contains the abnf grammar.
	 * @param grammar an optional grammar to include.
	 * @return the Grammar object corresponding to the text definition loaded, nullptr if an error occured.
	**/
	BELR_PUBLIC std::shared_ptr<Grammar> createFromAbnf(const std::string &abnf, const std::shared_ptr<Grammar> &grammar=nullptr);
	/**
	 * Create a grammar from an ABNF grammar defined in the text file pointed by path.
	 * An optional Grammar argument corresponding to a grammar to include can be passed.
	 * Usually the belr::CoreRules grammar is required for most IETF text protocols.
	 * The returned grammar can be used to instanciate a belr::Parser object capable of parsing
	 * the protocol or language described in the grammar.
	 * @param path the path from where to load the abnf definition.
	 * @param grammar an optional grammar to include.
	 * @return the Grammar object corresponding to the text definition loaded, nullptr if an error occured.
	**/
	BELR_PUBLIC std::shared_ptr<Grammar> createFromAbnfFile(const std::string &path, const std::shared_ptr<Grammar> &grammar=nullptr);

private:
	Parser<std::shared_ptr<ABNFBuilder>> mParser;
};

/**
 * The GrammarLoader creates Grammar objects from binary files lookup and loaded from a pre-defined set of paths.
 * The binary files have to be created from the ABNF grammar files using the belr-compiler tool.
**/
class GrammarLoader{
public:
	/**
	 * Obtain the GrammarLoader singleton.
	**/
	BELR_PUBLIC static GrammarLoader &get();
	/**
	 * Clear all specific lookup paths previously set in the GrammarLoader.
	 * The system paths are not erased.
	**/
	BELR_PUBLIC void clear();
	/**
	 * Add a specific path to lookup for grammar binary files.
	 * Any specific paths added by this method is actually prepended to the list of paths.
	 * @param path a path
	**/
	BELR_PUBLIC void addPath(const std::string &path);
	/**
	 * Build a grammar loaded from a binary file whose name is 'fileName' argument.
	 * The file searched from the list of specific paths eventually added by addPath(), and the system paths.
	 * That the GrammarLoader is initialized with two system paths used for searching:
	 * - ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/belr/grammars
	 * - ${CMAKE_INSTALL_DATADIR}/belr/grammars
	 * @param fileName the file name of grammar
	**/
	BELR_PUBLIC std::shared_ptr<Grammar> load(const std::string &fileName);
private:
	GrammarLoader();
	GrammarLoader(const GrammarLoader & other); //forbids copy constructor
	std::string lookup(const std::string &fileName, const std::list<std::string> &dirs);
	std::list<std::string> mSystemPaths;
	std::list<std::string> mAppPaths;
	bool isAbsolutePath(const std::string &path);
	static GrammarLoader *sInstance;
};

}//end of namespace

#endif
