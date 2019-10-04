/*
	belcard-rfc6474-tester.cpp
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

static void birth_place_property(void) {
	test_property<BelCardBirthPlace>("BIRTHPLACE:Babies'R'Us Hospital\r\n");
	test_property<BelCardBirthPlace>("BIRTHPLACE;VALUE=uri:http://example.com/hospitals/babiesrus.vcf\r\n");
	test_property<BelCardBirthPlace>("BIRTHPLACE;VALUE=uri:geo:46.769307\\,-71.283079\r\n");
}

static void death_place_property(void) {
	test_property<BelCardDeathPlace>("DEATHPLACE:Aboard the Titanic\\, near Newfoundland\r\n");
	test_property<BelCardDeathPlace>("DEATHPLACE;VALUE=uri:http://example.com/ships/titanic.vcf\r\n");
	test_property<BelCardDeathPlace>("DEATHPLACE;VALUE=uri:geo:41.731944\\,-49.945833\r\n");
}

static void death_date_property(void) {
	test_property<BelCardDeathDate>("DEATHDATE:19960415\r\n");
	test_property<BelCardDeathDate>("DEATHDATE:--0415\r\n");
	test_property<BelCardDeathDate>("DEATHDATE:19531015T231000Z\r\n");
	test_property<BelCardDeathDate>("DEATHDATE;VALUE=text:circa 1800\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("Birth place", birth_place_property),
	TEST_NO_TAG("Death place", death_place_property),
	TEST_NO_TAG("Death date", death_date_property),
};

test_suite_t vcard_rfc6474_properties_test_suite = {
	"RFC 6474",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
