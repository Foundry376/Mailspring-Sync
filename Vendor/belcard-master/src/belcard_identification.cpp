/*
	belcard_identification.cpp
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

#include "belcard/belcard.hpp"


using namespace::std;
using namespace::belr;
using namespace::belcard;

shared_ptr<BelCardFullName> BelCardFullName::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardFullName>("FN", input);
}

void BelCardFullName::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("FN", make_fn(BelCardGeneric::create<BelCardFullName>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("LANGUAGE-param", make_sfn(&BelCardProperty::setLanguageParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("FN-value", make_sfn(&BelCardProperty::setValue));
}

BelCardFullName::BelCardFullName() : BelCardProperty() {
	setName("FN");
}

shared_ptr<BelCardName> BelCardName::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardName>("N", input);
}

void BelCardName::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("N", make_fn(BelCardGeneric::create<BelCardName>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("SORT-AS-param", make_sfn(&BelCardProperty::setSortAsParam))
			->setCollector("LANGUAGE-param", make_sfn(&BelCardProperty::setLanguageParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("N-fn", make_sfn(&BelCardName::setFamilyName))
			->setCollector("N-gn", make_sfn(&BelCardName::setGivenName))
			->setCollector("N-an", make_sfn(&BelCardName::setAdditionalName))
			->setCollector("N-text", make_sfn(&BelCardName::setFreeformTextName))
			->setCollector("N-prefixes", make_sfn(&BelCardName::setPrefixes))
			->setCollector("N-suffixes", make_sfn(&BelCardName::setSuffixes));
}

BelCardName::BelCardName() : BelCardProperty() {
	setName("N");
}

void BelCardName::setFamilyName(const string &value) {
	_family_name = value;
}
const string &BelCardName::getFamilyName() const {
	return _family_name;
}

void BelCardName::setGivenName(const string &value) {
	_given_name = value;
}
const string &BelCardName::getGivenName() const {
	return _given_name;
}

void BelCardName::setAdditionalName(const string &value) {
	_additional_name = value;
}
const string &BelCardName::getAdditionalName() const {
	return _additional_name;
}

void BelCardName::setFreeformTextName(const string &value) {
	_freeform_text_name = value;
}
const string &BelCardName::getFreeformTextName() const {
	return _freeform_text_name;
}

void BelCardName::setPrefixes(const string &value) {
	_prefixes = value;
}
const string &BelCardName::getPrefixes() const {
	return _prefixes;
}

void BelCardName::setSuffixes(const string &value) {
	_suffixes = value;
}
const string &BelCardName::getSuffixes() const {
	return _suffixes;
}

void BelCardName::serialize(ostream& output) const {
	if (getGroup().length() > 0) {
		output << getGroup() << ".";
	}
	
	output << getName();
	for (auto it = getParams().begin(); it != getParams().end(); ++it) {
		output << ";" << (**it); 
	}
	if (getFreeformTextName() != "") {
		output << ":" << getFreeformTextName() + ";\r\n";
	} else {
		output << ":" << getFamilyName() + ";" + getGivenName() + ";" + getAdditionalName() + ";" + getPrefixes() + ";" + getSuffixes() << "\r\n";
	}
}

shared_ptr<BelCardNickname> BelCardNickname::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardNickname>("NICKNAME", input);
}

void BelCardNickname::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("NICKNAME", make_fn(BelCardGeneric::create<BelCardNickname>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("LANGUAGE-param", make_sfn(&BelCardProperty::setLanguageParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("NICKNAME-value", make_sfn(&BelCardProperty::setValue));
}

BelCardNickname::BelCardNickname() : BelCardProperty() {
	setName("NICKNAME");
}

shared_ptr<BelCardBirthday> BelCardBirthday::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardBirthday>("BDAY", input);
}

void BelCardBirthday::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("BDAY", make_fn(BelCardGeneric::create<BelCardBirthday>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("LANGUAGE-param", make_sfn(&BelCardProperty::setLanguageParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("CALSCALE-param", make_sfn(&BelCardProperty::setCALSCALEParam))
			->setCollector("BDAY-value", make_sfn(&BelCardProperty::setValue));
}

BelCardBirthday::BelCardBirthday() : BelCardProperty() {
	setName("BDAY");
}

shared_ptr<BelCardAnniversary> BelCardAnniversary::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardAnniversary>("ANNIVERSARY", input);
}

void BelCardAnniversary::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("ANNIVERSARY", make_fn(BelCardGeneric::create<BelCardAnniversary>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("CALSCALE-param", make_sfn(&BelCardProperty::setCALSCALEParam))
			->setCollector("ANNIVERSARY-value", make_sfn(&BelCardProperty::setValue));
}

BelCardAnniversary::BelCardAnniversary() : BelCardProperty() {
	setName("ANNIVERSARY");
}

shared_ptr<BelCardGender> BelCardGender::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardGender>("GENDER", input);
}

void BelCardGender::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("GENDER", make_fn(BelCardGeneric::create<BelCardGender>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("GENDER-value", make_sfn(&BelCardProperty::setValue));
}

BelCardGender::BelCardGender() : BelCardProperty() {
	setName("GENDER");
}

shared_ptr<BelCardPhoto> BelCardPhoto::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardPhoto>("PHOTO", input);
}

void BelCardPhoto::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("PHOTO", make_fn(BelCardGeneric::create<BelCardPhoto>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("MEDIATYPE-param", make_sfn(&BelCardProperty::setMediaTypeParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PHOTO-value", make_sfn(&BelCardProperty::setValue));
}

BelCardPhoto::BelCardPhoto() : BelCardProperty() {
	setName("PHOTO");
}