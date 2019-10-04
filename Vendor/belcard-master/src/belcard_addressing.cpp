/*
	belcard_addressing.cpp
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

shared_ptr<BelCardAddress> BelCardAddress::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardAddress>("ADR", input);
}

void BelCardAddress::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("ADR", make_fn(BelCardGeneric::create<BelCardAddress>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("LABEL-param", make_sfn(&BelCardAddress::setLabelParam))
			->setCollector("LANGUAGE-param", make_sfn(&BelCardProperty::setLanguageParam))
			->setCollector("GEO-PARAM-param", make_sfn(&BelCardProperty::setGeoParam))
			->setCollector("TZ-PARAM-param", make_sfn(&BelCardProperty::setTimezoneParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("TYPE-param", make_sfn(&BelCardProperty::setTypeParam))
			->setCollector("ADR-pobox", make_sfn(&BelCardAddress::setPostOfficeBox))
			->setCollector("ADR-ext", make_sfn(&BelCardAddress::setExtendedAddress))
			->setCollector("ADR-street", make_sfn(&BelCardAddress::setStreet))
			->setCollector("ADR-locality", make_sfn(&BelCardAddress::setLocality))
			->setCollector("ADR-region", make_sfn(&BelCardAddress::setRegion))
			->setCollector("ADR-code", make_sfn(&BelCardAddress::setPostalCode))
			->setCollector("ADR-country", make_sfn(&BelCardAddress::setCountry));
}

BelCardAddress::BelCardAddress() : BelCardProperty() {
	setName("ADR");
}

void BelCardAddress::setPostOfficeBox(const string &value) {
	_po_box = value;
}
const string &BelCardAddress::getPostOfficeBox() const {
	return _po_box;
}

void BelCardAddress::setExtendedAddress(const string &value) {
	_extended_address = value;
}
const string &BelCardAddress::getExtendedAddress() const {
	return _extended_address;
}

void BelCardAddress::setStreet(const string &value) {
	_street = value;
}
const string &BelCardAddress::getStreet() const {
	return _street;
}

void BelCardAddress::setLocality(const string &value) {
	_locality = value;
}
const string &BelCardAddress::getLocality() const {
	return _locality;
}

void BelCardAddress::setRegion(const string &value) {
	_region = value;
}
const string &BelCardAddress::getRegion() const {
	return _region;
}

void BelCardAddress::setPostalCode(const string &value) {
	_postal_code = value;
}
const string &BelCardAddress::getPostalCode() const {
	return _postal_code;
}

void BelCardAddress::setCountry(const string &value) {
	_country = value;
}
const string &BelCardAddress::getCountry() const {
	return _country;
}
		
void BelCardAddress::setLabelParam(const shared_ptr<BelCardLabelParam> &param) {
	_label_param = param;
	_params.push_back(_label_param);
}
const shared_ptr<BelCardLabelParam> &BelCardAddress::getLabelParam() const {
	return _label_param;
}

void BelCardAddress::serialize(ostream& output) const {
	if (getGroup().length() > 0) {
		output << getGroup() << ".";
	}
	
	output << getName();
	for (auto it = getParams().begin(); it != getParams().end(); ++it) {
		output << ";" << (**it); 
	}
	output << ":" << getPostOfficeBox() << ";" << getExtendedAddress()
		<< ";" << getStreet() << ";" << getLocality() << ";" << getRegion() 
		<< ";" << getPostalCode() << ";" << getCountry() << "\r\n";
}