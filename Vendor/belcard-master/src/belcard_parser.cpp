/*
	belcard_parser.cpp
	Copyright (C) 2015  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belcard/belcard_parser.hpp"
#include "belcard/belcard.hpp"
#include "belcard/belcard_utils.hpp"
#include "belcard/vcard_grammar.hpp"


#include <iostream>
#include <fstream>
#include <sstream>

using namespace::std;
using namespace::belr;
using namespace::belcard;

shared_ptr<BelCardParser> BelCardParser::getInstance() {
	static shared_ptr<BelCardParser> parser(new BelCardParser);
	return parser;
}

BelCardParser::BelCardParser() {
	shared_ptr<Grammar> grammar = loadVcardGrammar();
	_parser = new Parser<shared_ptr<BelCardGeneric>>(grammar);

	BelCardList::setHandlerAndCollectors(_parser);
	BelCard::setHandlerAndCollectors(_parser);
	BelCardParam::setAllParamsHandlersAndCollectors(_parser);
	BelCardProperty::setHandlerAndCollectors(_parser);

	BelCardSource::setHandlerAndCollectors(_parser);
	BelCardKind::setHandlerAndCollectors(_parser);
	BelCardXML::setHandlerAndCollectors(_parser);

	BelCardFullName::setHandlerAndCollectors(_parser);
	BelCardName::setHandlerAndCollectors(_parser);
	BelCardNickname::setHandlerAndCollectors(_parser);
	BelCardPhoto::setHandlerAndCollectors(_parser);
	BelCardBirthday::setHandlerAndCollectors(_parser);
	BelCardAnniversary::setHandlerAndCollectors(_parser);
	BelCardGender::setHandlerAndCollectors(_parser);

	BelCardAddress::setHandlerAndCollectors(_parser);

	BelCardPhoneNumber::setHandlerAndCollectors(_parser);
	BelCardEmail::setHandlerAndCollectors(_parser);
	BelCardImpp::setHandlerAndCollectors(_parser);
	BelCardLang::setHandlerAndCollectors(_parser);

	BelCardTimezone::setHandlerAndCollectors(_parser);
	BelCardGeo::setHandlerAndCollectors(_parser);

	BelCardTitle::setHandlerAndCollectors(_parser);
	BelCardRole::setHandlerAndCollectors(_parser);
	BelCardLogo::setHandlerAndCollectors(_parser);
	BelCardOrganization::setHandlerAndCollectors(_parser);
	BelCardMember::setHandlerAndCollectors(_parser);
	BelCardRelated::setHandlerAndCollectors(_parser);

	BelCardCategories::setHandlerAndCollectors(_parser);
	BelCardNote::setHandlerAndCollectors(_parser);
	BelCardProductId::setHandlerAndCollectors(_parser);
	BelCardRevision::setHandlerAndCollectors(_parser);
	BelCardSound::setHandlerAndCollectors(_parser);
	BelCardUniqueId::setHandlerAndCollectors(_parser);
	BelCardClientProductIdMap::setHandlerAndCollectors(_parser);
	BelCardURL::setHandlerAndCollectors(_parser);

	BelCardKey::setHandlerAndCollectors(_parser);

	BelCardFBURL::setHandlerAndCollectors(_parser);
	BelCardCALADRURI::setHandlerAndCollectors(_parser);
	BelCardCALURI::setHandlerAndCollectors(_parser);

	BelCardBirthPlace::setHandlerAndCollectors(_parser);
	BelCardDeathPlace::setHandlerAndCollectors(_parser);
	BelCardDeathDate::setHandlerAndCollectors(_parser);
}

BelCardParser::~BelCardParser() {
	delete _parser;
}

shared_ptr<BelCardGeneric> BelCardParser::_parse(const string &input, const string &rule) {
	size_t parsedSize = 0;
	shared_ptr<BelCardGeneric> ret = _parser->parseInput(rule, input, &parsedSize);
	if (parsedSize < input.size()){
		bctbx_error("[belcard] Parsing ended prematuraly at pos %llu", (unsigned long long)parsedSize);
	}
	return ret;
}

shared_ptr<BelCard> BelCardParser::parseOne(const string &input) {
	string vcard = belcard_unfold(input);
	shared_ptr<BelCardGeneric> ret = _parse(vcard, "vcard");
	shared_ptr<BelCard> belCard = dynamic_pointer_cast<BelCard>(ret);
	return belCard;
}

shared_ptr<BelCardList> BelCardParser::parse(const string &input) {
	string vcards = belcard_unfold(input);
	shared_ptr<BelCardGeneric> ret = _parse(vcards, "vcard-list");
	shared_ptr<BelCardList> belCards = dynamic_pointer_cast<BelCardList>(ret);
	return belCards;
}

shared_ptr<BelCardList> BelCardParser::parseFile(const string &filename) {
	string vcard = belcard_read_file(filename);
	return parse(vcard);
}
