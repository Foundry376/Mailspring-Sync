/*
	belcard-communication-tester.cpp
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

static void tel_property(void) {
	test_property<BelCardPhoneNumber>("TEL;VALUE=uri;TYPE=work:tel:+33-9-52-63-65-05\r\n");
	test_property<BelCardPhoneNumber>("TEL;VALUE=uri;TYPE=\"voice,work\":tel:+33952636505\r\n");
	test_property<BelCardPhoneNumber>("TEL;VALUE=text;TYPE=work:+33 9 52 63 65 05\r\n");
}

static void email_property(void) {
	test_property<BelCardEmail>("EMAIL;TYPE=work:sylvain.berfini@belledonne-communications.com\r\n");
}

static void impp_property(void) {
	test_property<BelCardImpp>("IMPP;TYPE=work:sip:sylvain@sip.linphone.org\r\n");
}

static void lang_property(void) {
	test_property<BelCardLang>("LANG;TYPE=home:fr\r\n");
	test_property<BelCardLang>("LANG:fr-FR\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("Tel", tel_property),
	TEST_NO_TAG("Email", email_property),
	TEST_NO_TAG("IMPP", impp_property),
	TEST_NO_TAG("Language", lang_property),
};

test_suite_t vcard_communication_properties_test_suite = {
	"Communication",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
