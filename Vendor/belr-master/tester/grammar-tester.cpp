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

#include "belr-tester.h"
#include <cstdio>

using namespace::std;
using namespace::belr;

static string openFile(const string &name) {
	ifstream istr(name, std::ios::binary);
	if (!istr.is_open()) {
		BC_FAIL(name);
	}

	stringstream tmpStream;
	tmpStream << istr.rdbuf();
	string tmp = tmpStream.str();
	return tmp;
}

static bool parseMessage(shared_ptr<Grammar> grammar,  const string &message) {
	shared_ptr<DebugParser> parser = make_shared<DebugParser>(grammar);
	parser->setObservedRules({"sip-message"});
	size_t pos = 0;
	shared_ptr<DebugElement> elem = parser->parseInput("sip-message", message, &pos);
	BC_ASSERT_TRUE(elem != nullptr);
	if (!elem) return FALSE;
	BC_ASSERT_EQUAL((int)pos, (int)message.size(), int, "%i");
	BC_ASSERT_TRUE(message == elem->getValue());
	
	return message.size() == pos && message == elem->getValue();
}


static void sipgrammar_save_and_load(void) {

	string grammarToParse = bcTesterRes("res/sipgrammar.txt");
	string grammarDump = bcTesterFile("grammarDump.bin");
	string sipmessage = openFile(bcTesterRes("res/register.txt"));

	remove(grammarDump.c_str());
	
	BC_ASSERT_TRUE(sipmessage.size() > 0);

	ABNFGrammarBuilder builder;

	//Read grammar put it in object grammar
	auto start = std::chrono::high_resolution_clock::now();
	shared_ptr<Grammar> grammar=builder.createFromAbnfFile(grammarToParse, make_shared<CoreRules>());
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsedFirst = finish - start;

	BC_ASSERT_FALSE(!grammar);
	
	if (!grammar) return;
	
	// make sure we're able to parse a SIP message with the created grammar
	BC_ASSERT_TRUE(parseMessage(grammar, sipmessage));

	//Save grammar

	grammar->save(grammarDump);

	//Load grammar
	start = std::chrono::high_resolution_clock::now();
	shared_ptr<Grammar> loadedGram = make_shared<Grammar>("loaded");
	BC_ASSERT_TRUE(loadedGram->load(grammarDump) ==  0);
	finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsedSecond = finish - start;

	// Make sure the loaded from binary file grammar is still able to parse the sip message:
	BC_ASSERT_TRUE(parseMessage(loadedGram, sipmessage));

	BC_ASSERT_TRUE(elapsedFirst.count() > 2*elapsedSecond.count());
}

/**
 * In SIP grammar, we have:
 * header-name = token
 * This kind of rules were causing issues in previous version of belr because "header-name" rule could not be attached
 * with a parser handler, as the recognizer behind it was named "token".
 */
static void aliases_rules(void) {
	string grammarToParse = bcTesterRes("res/sipgrammar.txt");
	string sipmessage = openFile(bcTesterRes("res/response.txt"));
	
	BC_ASSERT_TRUE(sipmessage.size() > 0);

	ABNFGrammarBuilder builder;

	//Read grammar put it in object grammar
	shared_ptr<Grammar> grammar=builder.createFromAbnfFile(grammarToParse, make_shared<CoreRules>());

	BC_ASSERT_FALSE(!grammar);
	
	if (!grammar) return;
	
	shared_ptr<DebugParser> parser = make_shared<DebugParser>(grammar);
	parser->setObservedRules({"sip-message", "header-name"});
	size_t pos = 0;
	shared_ptr<DebugElement> elem = parser->parseInput("sip-message", sipmessage, &pos);
	BC_ASSERT_TRUE(elem != nullptr);
	if (!elem) return;
	BC_ASSERT_EQUAL(pos, sipmessage.size(), int, "%i");
	BC_ASSERT_TRUE(sipmessage == elem->getValue());
	list<shared_ptr<DebugElement>> headerNames;
	elem->findChildren("header-name", headerNames);
	BC_ASSERT_TRUE(headerNames.size() == 1);
	if (!headerNames.empty()) {
		BC_ASSERT_TRUE(headerNames.front()->getValue() ==  "Custom-header");
	}
}


static void test_grammar_loader(void) {
	GrammarLoader & loader = GrammarLoader::get();
	loader.addPath("res");
	shared_ptr<Grammar> grammar = loader.load("belr-grammar-example.blr");
	BC_ASSERT_PTR_NOT_NULL(grammar);
}

static test_t tests[] = {
	TEST_NO_TAG("SIP grammar save and load", sipgrammar_save_and_load),
	TEST_NO_TAG("SIP grammar with aliases rules", aliases_rules), 
	TEST_NO_TAG("Grammar loader", test_grammar_loader)
};

test_suite_t grammar_suite = {
	"Grammar",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
