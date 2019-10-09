/*
	belcard_property.hpp
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

#ifndef belcard_property_hpp
#define belcard_property_hpp

#include <belr/grammarbuilder.h>
#include <belr/abnf.h>

#include "belcard_utils.hpp"
#include "belcard/belcard_generic.hpp"
#include "belcard_params.hpp"
#include "belcard/belcard_parser.hpp"

#include <string>
#include <list>
#include <sstream>

namespace belcard {
	class BelCardProperty : public BelCardGeneric {
	protected:
		std::string _group;
		std::string _name;
		std::string _value;
		std::shared_ptr<BelCardLanguageParam> _lang_param;
		std::shared_ptr<BelCardValueParam> _value_param;
		std::shared_ptr<BelCardPrefParam> _pref_param;
		std::shared_ptr<BelCardAlternativeIdParam> _altid_param;
		std::shared_ptr<BelCardParamIdParam> _pid_param;
		std::shared_ptr<BelCardTypeParam> _type_param;
		std::shared_ptr<BelCardMediaTypeParam> _mediatype_param;
		std::shared_ptr<BelCardCALSCALEParam> _calscale_param;
		std::shared_ptr<BelCardSortAsParam> _sort_as_param;
		std::shared_ptr<BelCardGeoParam> _geo_param;
		std::shared_ptr<BelCardTimezoneParam> _tz_param;
		std::list<std::shared_ptr<BelCardParam>> _params;

	public:
		template <typename T>
		static std::shared_ptr<T> parseProperty(const std::string& rule, const std::string& input) {
			size_t parseLength;
			std::shared_ptr<BelCardParser> parser = BelCardParser::getInstance();
			std::shared_ptr<BelCardGeneric> ret = parser->_parser->parseInput(rule, input, &parseLength);

			// -2 because the input is terminated by a new line.
			return ret && parseLength == input.length() - 2
				? std::dynamic_pointer_cast<T>(ret)
				: nullptr;
		}

		BELCARD_PUBLIC static std::shared_ptr<BelCardProperty> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardProperty();

		BELCARD_PUBLIC virtual void setGroup(const std::string &group);
		BELCARD_PUBLIC virtual const std::string &getGroup() const;

		BELCARD_PUBLIC virtual void setName(const std::string &name);
		BELCARD_PUBLIC virtual const std::string &getName() const;

		BELCARD_PUBLIC virtual void setValue(const std::string &value);
		BELCARD_PUBLIC virtual const std::string &getValue() const;

		BELCARD_PUBLIC virtual void setLanguageParam(const std::shared_ptr<BelCardLanguageParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardLanguageParam> &getLanguageParam() const;

		BELCARD_PUBLIC virtual void setValueParam(const std::shared_ptr<BelCardValueParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardValueParam> &getValueParam() const;

		BELCARD_PUBLIC virtual void setPrefParam(const std::shared_ptr<BelCardPrefParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardPrefParam> &getPrefParam() const;

		BELCARD_PUBLIC virtual void setAlternativeIdParam(const std::shared_ptr<BelCardAlternativeIdParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardAlternativeIdParam> &getAlternativeIdParam() const;

		BELCARD_PUBLIC virtual void setParamIdParam(const std::shared_ptr<BelCardParamIdParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardParamIdParam> &getParamIdParam() const;

		BELCARD_PUBLIC virtual void setTypeParam(const std::shared_ptr<BelCardTypeParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardTypeParam> &getTypeParam() const;

		BELCARD_PUBLIC virtual void setMediaTypeParam(const std::shared_ptr<BelCardMediaTypeParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardMediaTypeParam> &getMediaTypeParam() const;

		BELCARD_PUBLIC virtual void setCALSCALEParam(const std::shared_ptr<BelCardCALSCALEParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardCALSCALEParam> &getCALSCALEParam() const;

		BELCARD_PUBLIC virtual void setSortAsParam(const std::shared_ptr<BelCardSortAsParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardSortAsParam> &getSortAsParam() const;

		BELCARD_PUBLIC virtual void setGeoParam(const std::shared_ptr<BelCardGeoParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardGeoParam> &getGeoParam() const;

		BELCARD_PUBLIC virtual void setTimezoneParam(const std::shared_ptr<BelCardTimezoneParam> &param);
		BELCARD_PUBLIC virtual const std::shared_ptr<BelCardTimezoneParam> &getTimezoneParam() const;

		BELCARD_PUBLIC virtual void addParam(const std::shared_ptr<BelCardParam> &param);
		BELCARD_PUBLIC virtual const std::list<std::shared_ptr<BelCardParam>> &getParams() const;
		BELCARD_PUBLIC virtual void removeParam(const std::shared_ptr<BelCardParam> &param);

		BELCARD_PUBLIC void serialize(std::ostream &output) const override;
	};
}
#endif
