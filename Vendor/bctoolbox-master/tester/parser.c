/*
	bctoolbox
    Copyright (C) 2016  Belledonne Communications SARL


    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bctoolbox_tester.h"
#include "bctoolbox/parser.h"


static void simple_escaping(void) {
 char * my_escaped_string;
 bctbx_noescape_rules_t my_rules = {0};
 bctbx_noescape_rules_add_alfanums(my_rules);
 my_escaped_string = bctbx_escape("François",my_rules);
 BC_ASSERT_TRUE(strcmp("Fran%c3%a7ois",my_escaped_string)==0);
 bctbx_free(my_escaped_string);
}

static void simple_unescaping(void) {
 char * my_unescaped_string;
 my_unescaped_string = bctbx_unescaped_string("Fran%c3%a7ois");
 BC_ASSERT_TRUE(strcmp("François",my_unescaped_string)==0);
 bctbx_free(my_unescaped_string);
}

static test_t container_tests[] = {
	TEST_NO_TAG("simple escaping", simple_escaping),
	TEST_NO_TAG("simple unescaping", simple_unescaping),
};

test_suite_t parser_test_suite = {"Parsing", NULL, NULL, NULL, NULL,
							   sizeof(container_tests) / sizeof(container_tests[0]), container_tests};
