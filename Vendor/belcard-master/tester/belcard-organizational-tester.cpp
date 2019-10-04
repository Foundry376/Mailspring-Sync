/*
	belcard-organizational-tester.cpp
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

static void title_property(void) {
	test_property<BelCardTitle>("TITLE:Research Scientist\r\n");
}

static void role_property(void) {
	test_property<BelCardRole>("ROLE:Project Leader\r\n");
}

static void logo_property(void) {
	test_property<BelCardLogo>("LOGO:http://www.example.com/pub/logos/abccorp.jpg\r\n");
	test_property<BelCardLogo>("LOGO:data:image/jpeg;base64,MIICajCCAdOgAwIBAgICBEUwDQYJKoZIhvcAQEEBQAwdzELMAkGA1UEBhMCVVMxLDAqBgNVBAoTI05ldHNjYXBlIENvbW11bmljYXRpb25zIENvcnBvcmF0aW9uMRwwGgYDVQQLExNJbmZvcm1hdGlvbiBTeXN0\r\n");
}

static void org_property(void) {
	test_property<BelCardOrganization>("ORG:ABC\\, Inc.;North American Division;Marketing\r\n");
}

static void member_property(void) {
	test_property<BelCardMember>("MEMBER:mailto:subscriber1@example.com\r\n");
	test_property<BelCardMember>("MEMBER:xmpp:subscriber2@example.com\r\n");
	test_property<BelCardMember>("MEMBER:sip:subscriber3@example.com\r\n");
	test_property<BelCardMember>("MEMBER:tel:+1-418-555-5555\r\n");
	test_property<BelCardMember>("MEMBER:urn:uuid:b8767877-b4a1-4c70-9acc-505d3819e519\r\n");
}

static void related_property(void) {
	test_property<BelCardRelated>("RELATED;TYPE=friend:urn:uuid:f81d4fae-7dec-11d0-a765-00a0c91e6bf6\r\n");
	test_property<BelCardRelated>("RELATED;TYPE=contact:http://example.com/directory/jdoe.vcf\r\n");
	test_property<BelCardRelated>("RELATED;TYPE=co-worker;VALUE=text:Please contact my assistant Jane Doe for any inquiries.\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("TITLE", title_property),
	TEST_NO_TAG("ROLE", role_property),
	TEST_NO_TAG("LOGO", logo_property),
	TEST_NO_TAG("ORG", org_property),
	TEST_NO_TAG("MEMBER", member_property),
	TEST_NO_TAG("RELATED", related_property),
};

test_suite_t vcard_organizational_properties_test_suite = {
	"Organizational",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
