/*
	belcard-identification-tester.cpp
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

static void fn_property(void) {
	test_property<BelCardFullName>("FN:Sylvain Berfini\r\n");
	test_property<BelCardFullName>("FN:François Grisez\r\n");
}

static void n_property(void) {
	test_property<BelCardName>("N:Berfini;Sylvain;Pascal;;\r\n");
}

static void nickname_property(void) {
	test_property<BelCardNickname>("NICKNAME;TYPE=home:Viish\r\n");
}

static void bday_property(void) {
	test_property<BelCardBirthday>("BDAY:19891001\r\n");
	test_property<BelCardBirthday>("BDAY;VALUE=text:1er Octobre 1989\r\n");
}

static void anniversary_property(void) {
	test_property<BelCardAnniversary>("ANNIVERSARY;VALUE=date-and-or-time:20140621\r\n");
}

static void gender_property(void) {
	test_property<BelCardGender>("GENDER:M\r\n");
	test_property<BelCardGender>("GENDER:F;just kidding\r\n");
}

static void photo_property(void) {
	test_property<BelCardPhoto>("PHOTO;TYPE=work;VALUE=URL;MEDIATYPE=image/png:http://www.belledonne-communications.com/uploads/images/belledonne-communications.png\r\n");
	test_property<BelCardPhoto>("PHOTO:data:image/png;base64,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\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("Full name", fn_property),
	TEST_NO_TAG("Name", n_property),
	TEST_NO_TAG("Nickname", nickname_property),
	TEST_NO_TAG("Birthday", bday_property),
	TEST_NO_TAG("Anniversary", anniversary_property),
	TEST_NO_TAG("Gender", gender_property),
	TEST_NO_TAG("Photo", photo_property),
};

test_suite_t vcard_identification_properties_test_suite = {
	"Identification",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
