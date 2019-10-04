/*
	belcard_general.cpp
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

shared_ptr<BelCardSource> BelCardSource::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardSource>("SOURCE", input);
}

void BelCardSource::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("SOURCE", make_fn(BelCardGeneric::create<BelCardSource>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("PID-param", make_sfn(&BelCardProperty::setParamIdParam))
			->setCollector("PREF-param", make_sfn(&BelCardProperty::setPrefParam))
			->setCollector("MEDIATYPE-param", make_sfn(&BelCardProperty::setMediaTypeParam))
			->setCollector("ALTID-param", make_sfn(&BelCardProperty::setAlternativeIdParam))
			->setCollector("SOURCE-value", make_sfn(&BelCardProperty::setValue));
}

BelCardSource::BelCardSource() : BelCardProperty() {
	setName("SOURCE");
}

shared_ptr<BelCardKind> BelCardKind::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardKind>("KIND", input);
}

void BelCardKind::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("KIND", make_fn(BelCardGeneric::create<BelCardKind>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("KIND-value", make_sfn(&BelCardProperty::setValue));
}

BelCardKind::BelCardKind() : BelCardProperty() {
	setName("KIND");
}

shared_ptr<BelCardXML> BelCardXML::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardXML>("XML", input);
}

void BelCardXML::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("XML", make_fn(BelCardGeneric::create<BelCardXML>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("VALUE-param", make_sfn(&BelCardProperty::setValueParam))
			->setCollector("XML-value", make_sfn(&BelCardProperty::setValue));
}

BelCardXML::BelCardXML() : BelCardProperty() {
	setName("XML");
}