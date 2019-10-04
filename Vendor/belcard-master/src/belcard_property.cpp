/*
	belcard_property.cpp
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

#include "belcard/belcard_property.hpp"


using namespace::std;
using namespace::belr;
using namespace::belcard;

shared_ptr<BelCardProperty> BelCardProperty::parse(const string& input) {
	return BelCardProperty::parseProperty<BelCardProperty>("X-PROPERTY", input);
}

void BelCardProperty::setHandlerAndCollectors(Parser<shared_ptr<BelCardGeneric>> *parser) {
	parser->setHandler("X-PROPERTY", make_fn(BelCardGeneric::create<BelCardProperty>))
			->setCollector("group", make_sfn(&BelCardProperty::setGroup))
			->setCollector("any-param", make_sfn(&BelCardProperty::addParam))
			->setCollector("X-PROPERTY-name", make_sfn(&BelCardProperty::setName))
			->setCollector("X-PROPERTY-value", make_sfn(&BelCardProperty::setValue));
}

BelCardProperty::BelCardProperty() : BelCardGeneric() {

}

void BelCardProperty::setGroup(const string &group) {
	_group = group;
}
const string &BelCardProperty::getGroup() const {
	return _group;
}

void BelCardProperty::setName(const string &name) {
	_name = name;
}
const string &BelCardProperty::getName() const {
	return _name;
}

void BelCardProperty::setValue(const string &value) {
	string s = value;

	// Trim.
	s.erase(s.begin(), find_if(s.begin(), s.end(), not1(ptr_fun<int, int>(isspace))));
	s.erase(find_if(s.rbegin(), s.rend(), not1(ptr_fun<int, int>(isspace))).base(), s.end());

	_value = s;
}
const string &BelCardProperty::getValue() const {
	return _value;
}

void BelCardProperty::setLanguageParam(const shared_ptr<BelCardLanguageParam> &param) {
	if (_lang_param) {
		removeParam(_lang_param);
	}
	_lang_param = param;
	_params.push_back(_lang_param);
}
const shared_ptr<BelCardLanguageParam> &BelCardProperty::getLanguageParam() const {
	return _lang_param;
}

void BelCardProperty::setValueParam(const shared_ptr<BelCardValueParam> &param) {
	if (_value_param) {
		removeParam(_value_param);
	}
	_value_param = param;
	_params.push_back(_value_param);
}
const shared_ptr<BelCardValueParam> &BelCardProperty::getValueParam() const {
	return _value_param;
}

void BelCardProperty::setPrefParam(const shared_ptr<BelCardPrefParam> &param) {
	if (_pref_param) {
		removeParam(_pref_param);
	}
	_pref_param = param;
	_params.push_back(_pref_param);
}
const shared_ptr<BelCardPrefParam> &BelCardProperty::getPrefParam() const {
	return _pref_param;
}

void BelCardProperty::setAlternativeIdParam(const shared_ptr<BelCardAlternativeIdParam> &param) {
	if (_altid_param) {
		removeParam(_altid_param);
	}
	_altid_param = param;
	_params.push_back(_altid_param);
}
const shared_ptr<BelCardAlternativeIdParam> &BelCardProperty::getAlternativeIdParam() const {
	return _altid_param;
}

void BelCardProperty::setParamIdParam(const shared_ptr<BelCardParamIdParam> &param) {
	if (_pid_param) {
		removeParam(_pid_param);
	}
	_pid_param = param;
	_params.push_back(_pid_param);
}
const shared_ptr<BelCardParamIdParam> &BelCardProperty::getParamIdParam() const {
	return _pid_param;
}

void BelCardProperty::setTypeParam(const shared_ptr<BelCardTypeParam> &param) {
	if (_type_param) {
		removeParam(_type_param);
	}
	_type_param = param;
	_params.push_back(_type_param);
}
const shared_ptr<BelCardTypeParam> &BelCardProperty::getTypeParam() const {
	return _type_param;
}

void BelCardProperty::setMediaTypeParam(const shared_ptr<BelCardMediaTypeParam> &param) {
	if (_mediatype_param) {
		removeParam(_mediatype_param);
	}
	_mediatype_param = param;
	_params.push_back(_mediatype_param);
}
const shared_ptr<BelCardMediaTypeParam> &BelCardProperty::getMediaTypeParam() const {
	return _mediatype_param;
}

void BelCardProperty::setCALSCALEParam(const shared_ptr<BelCardCALSCALEParam> &param) {
	if (_calscale_param) {
		removeParam(_calscale_param);
	}
	_calscale_param = param;
	_params.push_back(_calscale_param);
}
const shared_ptr<BelCardCALSCALEParam> &BelCardProperty::getCALSCALEParam() const {
	return _calscale_param;
}

void BelCardProperty::setSortAsParam(const shared_ptr<BelCardSortAsParam> &param) {
	if (_sort_as_param) {
		removeParam(_sort_as_param);
	}
	_sort_as_param = param;
	_params.push_back(_sort_as_param);
}
const shared_ptr<BelCardSortAsParam> &BelCardProperty::getSortAsParam() const {
	return _sort_as_param;
}

void BelCardProperty::setGeoParam(const shared_ptr<BelCardGeoParam> &param) {
	if (_geo_param) {
		removeParam(_geo_param);
	}
	_geo_param = param;
	_params.push_back(_geo_param);
}
const shared_ptr<BelCardGeoParam> &BelCardProperty::getGeoParam() const {
	return _geo_param;
}

void BelCardProperty::setTimezoneParam(const shared_ptr<BelCardTimezoneParam> &param) {
	if (_tz_param) {
		removeParam(_tz_param);
	}
	_tz_param = param;
	_params.push_back(_tz_param);
}
const shared_ptr<BelCardTimezoneParam> &BelCardProperty::getTimezoneParam() const {
	return _tz_param;
}

void BelCardProperty::addParam(const shared_ptr<BelCardParam> &param) {
	_params.push_back(param);
}
const list<shared_ptr<BelCardParam>> &BelCardProperty::getParams() const {
	return _params;
}
void BelCardProperty::removeParam(const shared_ptr<BelCardParam> &param) {
	_params.remove(param);
}

void BelCardProperty::serialize(ostream& output) const {
	if (getGroup().length() > 0) {
		output << getGroup() << ".";
	}

	output << getName();
	for (auto it = getParams().begin(); it != getParams().end(); ++it) {
		output << ";" << (**it);
	}
	output << ":" << getValue() << "\r\n";
}
