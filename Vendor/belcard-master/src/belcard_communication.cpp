/*
	belcard_communication.cpp
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

#include <bctoolbox/parser.h>
#include "belcard/belcard.hpp"


using namespace::std;
using namespace::belr;
using namespace::belcard;

shared_ptr<BelCardPhoneNumber> BelCardPhoneNumber::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardPhoneNumber>("TEL", input);
}

void BelCardPhoneNumber::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("TEL", make_fn(BelCardGeneric::create<BelCardPhoneNumber>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("TEL-value", make_sfn(&BelCardProperty::setValue));
}

BelCardPhoneNumber::BelCardPhoneNumber() : BelCardProperty() {
	setName("TEL");
}

shared_ptr<BelCardEmail> BelCardEmail::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardEmail>("EMAIL", input);
}

void BelCardEmail::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("EMAIL", make_fn(BelCardGeneric::create<BelCardEmail>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("EMAIL-value", make_sfn(&BelCardProperty::setValue));
}

BelCardEmail::BelCardEmail() : BelCardProperty() {
	setName("EMAIL");
}

shared_ptr<BelCardImpp> BelCardImpp::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardImpp>("IMPP", input);
}

void BelCardImpp::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("IMPP", make_fn(BelCardGeneric::create<BelCardImpp>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("MEDIATYPE-param", make_sfn(&BelCardProperty::setMediaTypeParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("IMPP-value", make_sfn(&BelCardProperty::setValue));
}

BelCardImpp::BelCardImpp() : BelCardProperty() {
	setName("IMPP");
}

void BelCardImpp::setValue(const string &value) {
	bctbx_noescape_rules_t uri = {0};
	bctbx_noescape_rules_add_alfanums(uri);
	bctbx_noescape_rules_add_list(uri, ":@.-_~%!$&'()*+,;=");

	// Escape characters if required
	char * escaped_value = bctbx_escape(value.c_str(), uri);
	_escaped_value = string(escaped_value);
	bctbx_free(escaped_value);

	// Unescape previously escaped characters
	char * unescaped_value = bctbx_unescaped_string(value.c_str());
	string new_value = string(unescaped_value);
	bctbx_free(unescaped_value);

	BelCardProperty::setValue(new_value);
}

void BelCardImpp::serialize(ostream& output) const {
	if (getGroup().length() > 0) {
		output << getGroup() << ".";
	}

	output << getName();
	for (auto it = getParams().begin(); it != getParams().end(); ++it) {
		output << ";" << (**it);
	}
	output << ":" << _escaped_value << "\r\n";
}

shared_ptr<BelCardLang> BelCardLang::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardLang>("LANG", input);
}

void BelCardLang::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("LANG", make_fn(BelCardGeneric::create<BelCardLang>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("LANG-value", make_sfn(&BelCardProperty::setValue));
}

BelCardLang::BelCardLang() : BelCardProperty() {
	setName("LANG");
}