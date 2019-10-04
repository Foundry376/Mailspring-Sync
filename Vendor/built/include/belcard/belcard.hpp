/*
	belcard.hpp
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

#ifndef belcard_hpp
#define belcard_hpp

#include "belcard_utils.hpp"
#include "belcard_generic.hpp"
#include "belcard_params.hpp"
#include "belcard_property.hpp"
#include "belcard_general.hpp"
#include "belcard_identification.hpp"
#include "belcard_addressing.hpp"
#include "belcard_communication.hpp"
#include "belcard_geographical.hpp"
#include "belcard_organizational.hpp"
#include "belcard_explanatory.hpp"
#include "belcard_security.hpp"
#include "belcard_calendar.hpp"
#include "belcard_rfc6474.hpp"

#include "bctoolbox/logging.h"

#include <string>
#include <list>

namespace belcard {

	class BelCard : public BelCardGeneric {
	private:
		std::string _folded_string;
		bool _skipFieldValidation = false;

		std::shared_ptr<BelCardKind> _kind;
		std::shared_ptr<BelCardFullName> _fn;
		std::shared_ptr<BelCardName> _n;
		std::shared_ptr<BelCardBirthday> _bday;
		std::shared_ptr<BelCardAnniversary> _anniversary;
		std::shared_ptr<BelCardGender> _gender;
		std::shared_ptr<BelCardProductId> _pid;
		std::shared_ptr<BelCardRevision> _rev;
		std::shared_ptr<BelCardUniqueId> _uid;
		std::shared_ptr<BelCardBirthPlace> _bplace;
		std::shared_ptr<BelCardDeathPlace> _dplace;
		std::shared_ptr<BelCardDeathDate> _ddate;
		std::list<std::shared_ptr<BelCardNickname>> _nicknames;
		std::list<std::shared_ptr<BelCardPhoto>> _photos;
		std::list<std::shared_ptr<BelCardAddress>> _addr;
		std::list<std::shared_ptr<BelCardPhoneNumber>> _tel;
		std::list<std::shared_ptr<BelCardEmail>> _emails;
		std::list<std::shared_ptr<BelCardImpp>> _impp;
		std::list<std::shared_ptr<BelCardLang>> _langs;
		std::list<std::shared_ptr<BelCardSource>> _sources;
		std::list<std::shared_ptr<BelCardXML>> _xml;
		std::list<std::shared_ptr<BelCardTimezone>> _timezones;
		std::list<std::shared_ptr<BelCardGeo>> _geos;
		std::list<std::shared_ptr<BelCardTitle>> _titles;
		std::list<std::shared_ptr<BelCardRole>> _roles;
		std::list<std::shared_ptr<BelCardLogo>> _logos;
		std::list<std::shared_ptr<BelCardOrganization>> _organizations;
		std::list<std::shared_ptr<BelCardMember>> _members;
		std::list<std::shared_ptr<BelCardRelated>> _related;
		std::list<std::shared_ptr<BelCardCategories>> _categories;
		std::list<std::shared_ptr<BelCardNote>> _notes;
		std::list<std::shared_ptr<BelCardSound>> _sounds;
		std::list<std::shared_ptr<BelCardClientProductIdMap>> _clientpidmaps;
		std::list<std::shared_ptr<BelCardURL>> _urls;
		std::list<std::shared_ptr<BelCardKey>> _keys;
		std::list<std::shared_ptr<BelCardFBURL>> _fburls;
		std::list<std::shared_ptr<BelCardCALADRURI>> _caladruris;
		std::list<std::shared_ptr<BelCardCALURI>> _caluris;
		std::list<std::shared_ptr<BelCardProperty>> _extended_properties;
		std::list<std::shared_ptr<BelCardProperty>> _properties;

		template<typename T>
		void set(std::shared_ptr<T> &p, const std::shared_ptr<T> &property);

		template<typename T>
		void add(std::list<std::shared_ptr<T>> &property_list, const std::shared_ptr<T> &property);

		template<typename T>
		void remove(std::list<std::shared_ptr<T>> &property_list, std::shared_ptr<T> property);

		// The following are for belcard use only, they don't do any check on the value
		void _setKind(const std::shared_ptr<BelCardKind> &kind);
		void _setFullName(const std::shared_ptr<BelCardFullName> &fn);
		void _setName(const std::shared_ptr<BelCardName> &n);
		void _setBirthday(const std::shared_ptr<BelCardBirthday> &bday);
		void _setAnniversary(const std::shared_ptr<BelCardAnniversary> &anniversary);
		void _setGender(const std::shared_ptr<BelCardGender> &gender);
		void _setProductId(const std::shared_ptr<BelCardProductId> &pid);
		void _setRevision(const std::shared_ptr<BelCardRevision> &rev);
		void _setUniqueId(const std::shared_ptr<BelCardUniqueId> &uid);
		void _setBirthPlace(const std::shared_ptr<BelCardBirthPlace> &place);
		void _setDeathPlace(const std::shared_ptr<BelCardDeathPlace> &place);
		void _setDeathDate(const std::shared_ptr<BelCardDeathDate> &date);
		void _addNickname(const std::shared_ptr<BelCardNickname> &nickname);
		void _addPhoto(const std::shared_ptr<BelCardPhoto> &photo);
		void _addAddress(const std::shared_ptr<BelCardAddress> &addr);
		void _addPhoneNumber(const std::shared_ptr<BelCardPhoneNumber> &tel);
		void _addEmail(const std::shared_ptr<BelCardEmail> &email);
		void _addImpp(const std::shared_ptr<BelCardImpp> &impp);
		void _addLang(const std::shared_ptr<BelCardLang> &lang);
		void _addSource(const std::shared_ptr<BelCardSource> &source);
		void _addXML(const std::shared_ptr<BelCardXML> &xml);
		void _addTimezone(const std::shared_ptr<BelCardTimezone> &tz);
		void _addGeo(const std::shared_ptr<BelCardGeo> &geo);
		void _addTitle(const std::shared_ptr<BelCardTitle> &title);
		void _addRole(const std::shared_ptr<BelCardRole> &role);
		void _addLogo(const std::shared_ptr<BelCardLogo> &logo);
		void _addOrganization(const std::shared_ptr<BelCardOrganization> &org);
		void _addMember(const std::shared_ptr<BelCardMember> &member);
		void _addRelated(const std::shared_ptr<BelCardRelated> &related);
		void _addCategories(const std::shared_ptr<BelCardCategories> &categories);
		void _addNote(const std::shared_ptr<BelCardNote> &note);
		void _addSound(const std::shared_ptr<BelCardSound> &sound);
		void _addClientProductIdMap(const std::shared_ptr<BelCardClientProductIdMap> &clientpidmap);
		void _addURL(const std::shared_ptr<BelCardURL> &url);
		void _addKey(const std::shared_ptr<BelCardKey> &key);
		void _addFBURL(const std::shared_ptr<BelCardFBURL> &fburl);
		void _addCALADRURI(const std::shared_ptr<BelCardCALADRURI> &caladruri);
		void _addCALURI(const std::shared_ptr<BelCardCALURI> &caluri);
		void _addExtendedProperty(const std::shared_ptr<BelCardProperty> &property);

	public:
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCard();
		BELCARD_PUBLIC void setSkipFieldValidation(bool skip);
		BELCARD_PUBLIC bool getSkipFieldValidation();

		BELCARD_PUBLIC bool setKind(const std::shared_ptr<BelCardKind> &kind);
		BELCARD_PUBLIC const std::shared_ptr<BelCardKind> &getKind() const;

		BELCARD_PUBLIC bool setFullName(const std::shared_ptr<BelCardFullName> &fn);
		BELCARD_PUBLIC const std::shared_ptr<BelCardFullName> &getFullName() const;

		BELCARD_PUBLIC bool setName(const std::shared_ptr<BelCardName> &n);
		BELCARD_PUBLIC const std::shared_ptr<BelCardName> &getName() const;

		BELCARD_PUBLIC bool setBirthday(const std::shared_ptr<BelCardBirthday> &bday);
		BELCARD_PUBLIC const std::shared_ptr<BelCardBirthday> &getBirthday() const;

		BELCARD_PUBLIC bool setAnniversary(const std::shared_ptr<BelCardAnniversary> &anniversary);
		BELCARD_PUBLIC const std::shared_ptr<BelCardAnniversary> &getAnniversary() const;

		BELCARD_PUBLIC bool setGender(const std::shared_ptr<BelCardGender> &gender);
		BELCARD_PUBLIC const std::shared_ptr<BelCardGender> &getGender() const;

		BELCARD_PUBLIC bool setProductId(const std::shared_ptr<BelCardProductId> &pid);
		BELCARD_PUBLIC const std::shared_ptr<BelCardProductId> &getProductId() const;

		BELCARD_PUBLIC bool setRevision(const std::shared_ptr<BelCardRevision> &rev);
		BELCARD_PUBLIC const std::shared_ptr<BelCardRevision> &getRevision() const;

		BELCARD_PUBLIC bool setUniqueId(const std::shared_ptr<BelCardUniqueId> &uid);
		BELCARD_PUBLIC const std::shared_ptr<BelCardUniqueId> &getUniqueId() const;

		BELCARD_PUBLIC bool setBirthPlace(const std::shared_ptr<BelCardBirthPlace> &place);
		BELCARD_PUBLIC const std::shared_ptr<BelCardBirthPlace> &getBirthPlace() const;

		BELCARD_PUBLIC bool setDeathPlace(const std::shared_ptr<BelCardDeathPlace> &place);
		BELCARD_PUBLIC const std::shared_ptr<BelCardDeathPlace> &getDeathPlace() const;

		BELCARD_PUBLIC bool setDeathDate(const std::shared_ptr<BelCardDeathDate> &date);
		BELCARD_PUBLIC const std::shared_ptr<BelCardDeathDate> &getDeathDate() const;

		BELCARD_PUBLIC bool addNickname(const std::shared_ptr<BelCardNickname> &nickname);
		BELCARD_PUBLIC void removeNickname(const std::shared_ptr<BelCardNickname> &nickname);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardNickname>> &getNicknames() const;

		BELCARD_PUBLIC bool addPhoto(const std::shared_ptr<BelCardPhoto> &photo);
		BELCARD_PUBLIC void removePhoto(const std::shared_ptr<BelCardPhoto> &photo);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardPhoto>> &getPhotos() const;

		BELCARD_PUBLIC bool addAddress(const std::shared_ptr<BelCardAddress> &addr);
		BELCARD_PUBLIC void removeAddress(const std::shared_ptr<BelCardAddress> &addr);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardAddress>> &getAddresses() const;

		BELCARD_PUBLIC bool addPhoneNumber(const std::shared_ptr<BelCardPhoneNumber> &tel);
		BELCARD_PUBLIC void removePhoneNumber(const std::shared_ptr<BelCardPhoneNumber> &tel);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardPhoneNumber>> &getPhoneNumbers() const;

		BELCARD_PUBLIC bool addEmail(const std::shared_ptr<BelCardEmail> &email);
		BELCARD_PUBLIC void removeEmail(const std::shared_ptr<BelCardEmail> &email);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardEmail>> &getEmails() const;

		BELCARD_PUBLIC bool addImpp(const std::shared_ptr<BelCardImpp> &impp);
		BELCARD_PUBLIC void removeImpp(const std::shared_ptr<BelCardImpp> &impp);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardImpp>> &getImpp() const;

		BELCARD_PUBLIC bool addLang(const std::shared_ptr<BelCardLang> &lang);
		BELCARD_PUBLIC void removeLang(const std::shared_ptr<BelCardLang> &lang);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardLang>> &getLangs() const;

		BELCARD_PUBLIC bool addSource(const std::shared_ptr<BelCardSource> &source);
		BELCARD_PUBLIC void removeSource(const std::shared_ptr<BelCardSource> &source);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardSource>> &getSource() const;

		BELCARD_PUBLIC bool addXML(const std::shared_ptr<BelCardXML> &xml);
		BELCARD_PUBLIC void removeXML(const std::shared_ptr<BelCardXML> &xml);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardXML>> &getXML() const;

		BELCARD_PUBLIC bool addTimezone(const std::shared_ptr<BelCardTimezone> &tz);
		BELCARD_PUBLIC void removeTimezone(const std::shared_ptr<BelCardTimezone> &tz);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardTimezone>> &getTimezones() const;

		BELCARD_PUBLIC bool addGeo(const std::shared_ptr<BelCardGeo> &geo);
		BELCARD_PUBLIC void removeGeo(const std::shared_ptr<BelCardGeo> &geo);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardGeo>> &getGeos() const;

		BELCARD_PUBLIC bool addTitle(const std::shared_ptr<BelCardTitle> &title);
		BELCARD_PUBLIC void removeTitle(const std::shared_ptr<BelCardTitle> &title);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardTitle>> &getTitles() const;

		BELCARD_PUBLIC bool addRole(const std::shared_ptr<BelCardRole> &role);
		BELCARD_PUBLIC void removeRole(const std::shared_ptr<BelCardRole> &role);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardRole>> &getRoles() const;

		BELCARD_PUBLIC bool addLogo(const std::shared_ptr<BelCardLogo> &logo);
		BELCARD_PUBLIC void removeLogo(const std::shared_ptr<BelCardLogo> &logo);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardLogo>> &getLogos() const;

		BELCARD_PUBLIC bool addOrganization(const std::shared_ptr<BelCardOrganization> &org);
		BELCARD_PUBLIC void removeOrganization(const std::shared_ptr<BelCardOrganization> &org);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardOrganization>> &getOrganizations() const;

		BELCARD_PUBLIC bool addMember(const std::shared_ptr<BelCardMember> &member);
		BELCARD_PUBLIC void removeMember(const std::shared_ptr<BelCardMember> &member);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardMember>> &getMembers() const;

		BELCARD_PUBLIC bool addRelated(const std::shared_ptr<BelCardRelated> &related);
		BELCARD_PUBLIC void removeRelated(const std::shared_ptr<BelCardRelated> &related);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardRelated>> &getRelated() const;

		BELCARD_PUBLIC bool addCategories(const std::shared_ptr<BelCardCategories> &categories);
		BELCARD_PUBLIC void removeCategories(const std::shared_ptr<BelCardCategories> &categories);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardCategories>> &getCategories() const;

		BELCARD_PUBLIC bool addNote(const std::shared_ptr<BelCardNote> &note);
		BELCARD_PUBLIC void removeNote(const std::shared_ptr<BelCardNote> &note);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardNote>> &getNotes() const;

		BELCARD_PUBLIC bool addSound(const std::shared_ptr<BelCardSound> &sound);
		BELCARD_PUBLIC void removeSound(const std::shared_ptr<BelCardSound> &sound);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardSound>> &getSounds() const;

		BELCARD_PUBLIC bool addClientProductIdMap(const std::shared_ptr<BelCardClientProductIdMap> &clientpidmap);
		BELCARD_PUBLIC void removeClientProductIdMap(const std::shared_ptr<BelCardClientProductIdMap> &clientpidmap);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardClientProductIdMap>> &getClientProductIdMaps() const;

		BELCARD_PUBLIC bool addURL(const std::shared_ptr<BelCardURL> &url);
		BELCARD_PUBLIC void removeURL(const std::shared_ptr<BelCardURL> &url);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardURL>> &getURLs() const;

		BELCARD_PUBLIC bool addKey(const std::shared_ptr<BelCardKey> &key);
		BELCARD_PUBLIC void removeKey(const std::shared_ptr<BelCardKey> &key);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardKey>> &getKeys() const;

		BELCARD_PUBLIC bool addFBURL(const std::shared_ptr<BelCardFBURL> &fburl);
		BELCARD_PUBLIC void removeFBURL(const std::shared_ptr<BelCardFBURL> &fburl);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardFBURL>> &getFBURLs() const;

		BELCARD_PUBLIC bool addCALADRURI(const std::shared_ptr<BelCardCALADRURI> &caladruri);
		BELCARD_PUBLIC void removeCALADRURI(const std::shared_ptr<BelCardCALADRURI> &caladruri);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardCALADRURI>> &getCALADRURIs() const;

		BELCARD_PUBLIC bool addCALURI(const std::shared_ptr<BelCardCALURI> &caluri);
		BELCARD_PUBLIC void removeCALURI(const std::shared_ptr<BelCardCALURI> &caluri);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardCALURI>> &getCALURIs() const;

		BELCARD_PUBLIC bool addExtendedProperty(const std::shared_ptr<BelCardProperty> &property);
		BELCARD_PUBLIC void removeExtendedProperty(const std::shared_ptr<BelCardProperty> &property);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardProperty>> &getExtendedProperties() const;

		BELCARD_PUBLIC void addProperty(const std::shared_ptr<BelCardProperty> &property);
		BELCARD_PUBLIC void removeProperty(const std::shared_ptr<BelCardProperty> &property);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCardProperty>> &getProperties() const;

		BELCARD_PUBLIC std::string& toFoldedString();
		BELCARD_PUBLIC bool assertRFCCompliance() const;

		BELCARD_PUBLIC void serialize(std::ostream &output) const override;
	};

	class BelCardList : public BelCardGeneric {
	private:
		std::list<std::shared_ptr<BelCard>> _vCards;

	public:
		BELCARD_PUBLIC static void setHandlerAndCollectors(belr::Parser<std::shared_ptr<BelCardGeneric>> *parser);

		BELCARD_PUBLIC BelCardList();

		BELCARD_PUBLIC void addCard(const std::shared_ptr<BelCard> &vcard);
		BELCARD_PUBLIC const std::list<std::shared_ptr<BelCard>> &getCards() const;

		BELCARD_PUBLIC void serialize(std::ostream &output) const override;
	};
}

#endif
