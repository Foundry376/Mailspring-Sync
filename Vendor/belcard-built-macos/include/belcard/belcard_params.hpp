/*
	belcard_params.hpp
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

#ifndef belcard_params_hpp
#define belcard_params_hpp

#include <belr/grammarbuilder.h>
#include <belr/abnf.h>
#include "belcard_utils.hpp"
#include "belcard_generic.hpp"

#include <string>
#include <sstream>


namespace belcard {
	class BelCardParam : public BelCardGeneric {
	private:
		std::string _name;
		std::string _value;
	public:
		template <typename T>
		static std::shared_ptr<T> parseParam(const std::string& rule, const std::string& input);
		static std::shared_ptr<BelCardParam> parse(const std::string& input);
		static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		static void setAllParamsHandlersAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardParam();
		
		BELCARD_PUBLIC virtual void setName(const std::string &name);
		BELCARD_PUBLIC virtual const std::string &getName() const;
		
		BELCARD_PUBLIC virtual void setValue(const std::string &value) ;
		BELCARD_PUBLIC virtual const std::string &getValue() const;
		
		BELCARD_PUBLIC void serialize(std::ostream &output) const override;
	};
	
	class BelCardLanguageParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardLanguageParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardLanguageParam();
	};
	
	class BelCardValueParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardValueParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardValueParam();
	};
	
	class BelCardPrefParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardPrefParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardPrefParam();
	};
	
	class BelCardAlternativeIdParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardAlternativeIdParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardAlternativeIdParam();
	};
	
	class BelCardParamIdParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardParamIdParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardParamIdParam();
	};
	
	class BelCardTypeParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardTypeParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardTypeParam();
	};
	
	class BelCardMediaTypeParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardMediaTypeParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardMediaTypeParam();
	};
	
	class BelCardCALSCALEParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardCALSCALEParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardCALSCALEParam();
	};
	
	class BelCardSortAsParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardSortAsParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardSortAsParam();
	};
	
	class BelCardGeoParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardGeoParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardGeoParam();
	};
	
	class BelCardTimezoneParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardTimezoneParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardTimezoneParam();
	};
	
	class BelCardLabelParam : public BelCardParam {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardLabelParam> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardLabelParam();
	};
}

#endif
