/*
	belcard_geographical.hpp
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

#ifndef belcard_geographical_hpp
#define belcard_geographical_hpp

#include "belcard_utils.hpp"
#include "belcard_property.hpp"
#include <belr/grammarbuilder.h>
#include <belr/abnf.h>

#include <string>


namespace belcard {
	class BelCardTimezone : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardTimezone> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardTimezone();
	};
	
	class BelCardGeo : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardGeo> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardGeo();
	};
}

#endif
