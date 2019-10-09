/*
	belcard_addressing.hpp
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

#ifndef belcard_addressing_hpp
#define belcard_addressing_hpp

#include "belcard_utils.hpp"
#include "belcard_property.hpp"
#include <belr/grammarbuilder.h>
#include <belr/abnf.h>

#include <string>
#include <sstream>


namespace belcard {
	class BelCardAddress : public BelCardProperty {
	private:
		std::string _po_box;
		std::string _extended_address;
		std::string _street;
		std::string _locality;
		std::string _region;
		std::string _postal_code;
		std::string _country;
		std::shared_ptr<BelCardLabelParam> _label_param;

	public:
		BELCARD_PUBLIC static std::shared_ptr<BelCardAddress> parse(const std::string& input);
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardAddress();

		BELCARD_PUBLIC void setPostOfficeBox(const std::string &value);
		BELCARD_PUBLIC const std::string &getPostOfficeBox() const;

		BELCARD_PUBLIC void setExtendedAddress(const std::string &value);
		BELCARD_PUBLIC const std::string &getExtendedAddress() const;

		BELCARD_PUBLIC void setStreet(const std::string &value);
		BELCARD_PUBLIC const std::string &getStreet() const;

		BELCARD_PUBLIC void setLocality(const std::string &value);
		BELCARD_PUBLIC const std::string &getLocality() const;

		BELCARD_PUBLIC void setRegion(const std::string &value);
		BELCARD_PUBLIC const std::string &getRegion() const;

		BELCARD_PUBLIC void setPostalCode(const std::string &value);
		BELCARD_PUBLIC const std::string &getPostalCode() const;

		BELCARD_PUBLIC void setCountry(const std::string &value);
		BELCARD_PUBLIC const std::string &getCountry() const;

		BELCARD_PUBLIC void setLabelParam(const std::shared_ptr<BelCardLabelParam> &param);
		BELCARD_PUBLIC const std::shared_ptr<BelCardLabelParam> &getLabelParam() const;

		BELCARD_PUBLIC void serialize(std::ostream &output) const override;
	};
}

#endif
