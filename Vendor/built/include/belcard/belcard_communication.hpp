/*
	belcard_communication.hpp
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

#ifndef belcard_communication_hpp
#define belcard_communication_hpp

#include "belcard_utils.hpp"
#include "belcard_property.hpp"
#include <belr/grammarbuilder.h>
#include <belr/abnf.h>

#include <string>
#include <sstream>


namespace belcard {
	class BelCardPhoneNumber : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardPhoneNumber> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardPhoneNumber();
	};

	class BelCardEmail : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardEmail> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardEmail();
	};

	class BelCardImpp : public BelCardProperty {
	private:
		std::string _escaped_value;
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardImpp> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC void setValue(const std::string &value) override;
		BELCARD_PUBLIC void serialize(std::ostream &output) const override;

		BELCARD_PUBLIC BelCardImpp();
	};

	class BelCardLang : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardLang> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardLang();
	};
}

#endif
