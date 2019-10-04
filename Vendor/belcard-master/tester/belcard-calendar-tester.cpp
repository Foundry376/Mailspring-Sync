/*
	belcard-calendar-tester.cpp
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

static void fburl_property(void) {
	test_property<BelCardFBURL>("FBURL;MEDIATYPE=text/calendar:ftp://example.com/busy/project-a.ifb\r\n");
}

static void caladruri_property(void) {
	test_property<BelCardCALADRURI>("CALADRURI:http://example.com/calendar/jdoe\r\n");
}

static void caluri_property(void) {
	test_property<BelCardCALURI>("CALURI;MEDIATYPE=text/calendar:ftp://ftp.example.com/calA.ics\r\n");
}

static test_t tests[] = {
	TEST_NO_TAG("FBURL", fburl_property),
	TEST_NO_TAG("CALADRURI", caladruri_property),
	TEST_NO_TAG("CALURI", caluri_property),
};

test_suite_t vcard_calendar_properties_test_suite = {
	"Calendar",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
