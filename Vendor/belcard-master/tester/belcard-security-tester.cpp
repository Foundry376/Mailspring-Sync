/*
	belcard-security-tester.cpp
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

static void key_property(void) {
	test_property<BelCardKey>("KEY;MEDIATYPE=application/pgp-keys:ftp://example.com/keys/jdoe\r\n");
	test_property<BelCardKey>("KEY:data:application/pgp-keys;base64,MIICajCCAdOgAwIBAgICBEUwDQYJKoZIhvcNAQEEBQAwdzELMAkGA1UEBhMCVVMxLDAqBgNVBAoTI05l\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("Key", key_property),
};

test_suite_t vcard_security_properties_test_suite = {
	"Security",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
