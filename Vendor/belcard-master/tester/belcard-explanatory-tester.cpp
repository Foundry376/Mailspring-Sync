/*
	belcard-explanatory-tester.cpp
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
#include "belcard-tester.hpp"

using namespace::std;
using namespace::belcard;

static void categories_property(void) {
	test_property<BelCardCategories>("CATEGORIES:INTERNET,IETF,INDUSTRY,INFORMATION TECHNOLOGY\r\n");
}

static void note_property(void) {
	test_property<BelCardNote>("NOTE:This fax number is operational 0800 to 1715\\n EST\\, Mon-Fri.\r\n");
}

static void prodid_property(void) {
	test_property<BelCardProductId>("PRODID:-//ONLINE DIRECTORY//NONSGML Version 1//EN\r\n");
}

static void rev_property(void) {
	test_property<BelCardRevision>("REV:19951031T222710Z\r\n");
	
	shared_ptr<BelCard> card = BelCardGeneric::create<BelCard>();
	shared_ptr<BelCardRevision> rev = BelCardGeneric::create<BelCardRevision>();
	rev->setValue("AZERTY");
	BC_ASSERT_FALSE(BelCardGeneric::isValid(rev));
	card->setRevision(rev);
	BC_ASSERT_TRUE(card->getRevision() == NULL);
	
	rev->setValue("19951031T222710Z");
	BC_ASSERT_TRUE(BelCardGeneric::isValid(rev));
	card->setRevision(rev);
	BC_ASSERT_TRUE(card->getRevision() != NULL);
}

static void sound_property(void) {
	test_property<BelCardSound>("SOUND:data:audio/basic;base64,MIICajCCAdOgAwIBAgICBEUwDQYJKoZIhAQEEBQAwdzELMAkGA1UEBhMCVVMxLDAqBgNVBAoTI05ldHNjYXBlIENvbW11bmljYXRpb25zIENvcnBvcmF0aW9uMRwwGgYDVQQLExNJbmZvcm1hdGlvbiBTeXN0\r\n");
}

static void uid_property(void) {
	test_property<BelCardUniqueId>("UID:urn:uuid:f81d4fae-7dec-11d0-a765-00a0c91e6bf6\r\n");
}

static void clientpidmap_property(void) {
	test_property<BelCardClientProductIdMap>("CLIENTPIDMAP:1;urn:uuid:3df403f4-5924-4bb7-b077-3c711d9eb34b\r\n");
}

static void url_property(void) {
	test_property<BelCardURL>("URL:http://example.org/restaurant.french/~chezchic.html\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("CATEGORIES", categories_property),
	TEST_NO_TAG("NOTE", note_property),
	TEST_NO_TAG("PRODID", prodid_property),
	TEST_NO_TAG("REV", rev_property),
	TEST_NO_TAG("SOUND", sound_property),
	TEST_NO_TAG("UID", uid_property),
	TEST_NO_TAG("CLIENTPIDMAP", clientpidmap_property),
	TEST_NO_TAG("URL", url_property),
};

test_suite_t vcard_explanatory_properties_test_suite = {
	"Explanatory",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
