/*
	belcard_explanatory.hpp
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

#ifndef belcard_explanatory_hpp
#define belcard_explanatory_hpp

#include "belcard_utils.hpp"
#include "belcard_property.hpp"
#include <belr/grammarbuilder.h>
#include <belr/abnf.h>

#include <string>


namespace belcard {
	class BelCardCategories : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardCategories> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardCategories();
	};
	
	class BelCardNote : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardNote> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardNote();
	};
	
	class BelCardProductId : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardProductId> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardProductId();
	};
	
	class BelCardRevision : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardRevision> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardRevision();
	};
	
	class BelCardSound : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardSound> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardSound();
	};
	
	class BelCardUniqueId : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardUniqueId> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardUniqueId();
	};
	
	class BelCardClientProductIdMap : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardClientProductIdMap> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardClientProductIdMap();
	};
	
	class BelCardURL : public BelCardProperty {
	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardURL> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);
		
		BELCARD_PUBLIC BelCardURL();
	};
}

#endif
