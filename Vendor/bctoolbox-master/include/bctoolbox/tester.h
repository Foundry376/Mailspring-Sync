/*
	tester - liblinphone test suite
	Copyright (C) 2013  Belledonne Communications SARL

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

#ifndef BCTOOLBOX_TESTER_H
#define BCTOOLBOX_TESTER_H

#include <bctoolbox/port.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>


typedef void (*test_function_t)(void);
/** Function used in all suites - it is invoked before all and each tests and also after each and all tests
  * @return 0 means success, otherwise it's an error
**/
typedef int (*pre_post_function_t)(void);
// typedef int (*test_suite_function_t)(const char *name);

typedef struct {
	const char *name;
	test_function_t func;
	const char *tags[2];
} test_t;

#define TEST_NO_TAG(name, func) \
	{ name, func, { NULL, NULL } }
#define TEST_ONE_TAG(name, func, tag) \
	{ name, func, { tag, NULL } }
#define TEST_TWO_TAGS(name, func, tag1, tag2) \
	{ name, func, { tag1, tag2 } }

typedef struct {
	const char *name; /*suite name*/
	pre_post_function_t before_all; /*function invoked before running the suite. If not returning 0, suite is not launched. */
	pre_post_function_t after_all; /*function invoked at the end of the suite, even if some tests failed. */
	test_function_t before_each;   /*function invoked before each test within this suite. */
	test_function_t after_each;	/*function invoked after each test within this suite, even if it failed. */
	int nb_tests;				   /* number of tests */
	test_t *tests;				   /* tests within this suite */
} test_suite_t;

#ifdef __cplusplus
extern "C" {
#endif


#define CHECK_ARG(argument, index, argc)                      \
if(index >= argc) {                                           \
fprintf(stderr, "Missing argument for \"%s\"\n", argument);   \
return -1;                                                    \
}                                                             \

BCTBX_PUBLIC void bc_tester_init(void (*ftester_printf)(int level, const char *fmt, va_list args)
									, int verbosity_info, int verbosity_error, const char* expected_res);
BCTBX_PUBLIC void bc_tester_helper(const char *name, const char* additionnal_helper);
BCTBX_PUBLIC int bc_tester_parse_args(int argc, char** argv, int argid);
BCTBX_PUBLIC int bc_tester_start(const char* prog_name);
BCTBX_PUBLIC int bc_tester_register_suites(void);

BCTBX_PUBLIC int bc_tester_register_suite_by_name(const char *suite_name);
BCTBX_PUBLIC void bc_tester_add_suite(test_suite_t *suite);
BCTBX_PUBLIC void bc_tester_uninit(void);
BCTBX_PUBLIC void bc_tester_printf(int level, const char *fmt, ...);
BCTBX_PUBLIC const char * bc_tester_get_resource_dir_prefix(void);
BCTBX_PUBLIC void bc_tester_set_resource_dir_prefix(const char *name);
BCTBX_PUBLIC const char * bc_tester_get_writable_dir_prefix(void);
BCTBX_PUBLIC void bc_tester_set_writable_dir_prefix(const char *name);

BCTBX_PUBLIC void bc_tester_set_silent_func(int (*func)(const char*));
BCTBX_PUBLIC void bc_tester_set_verbose_func(int (*func)(const char*));
BCTBX_PUBLIC void bc_tester_set_logfile_func(int (*func)(const char*));

BCTBX_PUBLIC int bc_tester_nb_suites(void);
BCTBX_PUBLIC int bc_tester_nb_tests(const char* name);
BCTBX_PUBLIC void bc_tester_list_suites(void);
BCTBX_PUBLIC void bc_tester_list_tests(const char *suite_name);
BCTBX_PUBLIC const char * bc_tester_suite_name(int suite_index);
BCTBX_PUBLIC const char * bc_tester_test_name(const char *suite_name, int test_index);
BCTBX_PUBLIC int bc_tester_run_suite(test_suite_t *suite, const char *tag_name);
BCTBX_PUBLIC int bc_tester_run_tests(const char *suite_name, const char *test_name, const char *tag_name);
BCTBX_PUBLIC int bc_tester_suite_index(const char *suite_name);
BCTBX_PUBLIC const char * bc_tester_current_suite_name(void);
BCTBX_PUBLIC const char * bc_tester_current_test_name(void);
BCTBX_PUBLIC const char ** bc_tester_current_test_tags(void);


BCTBX_PUBLIC char* bc_sprintfva(const char* format, va_list args);
BCTBX_PUBLIC char* bc_sprintf(const char* format, ...);
BCTBX_PUBLIC void bc_free(void *ptr);

BCTBX_PUBLIC char * bc_tester_get_failed_asserts(void);
BCTBX_PUBLIC unsigned int bc_get_number_of_failures(void);
BCTBX_PUBLIC void bc_set_trace_handler(void(*handler)(int, const char*, va_list));

/**
 * Get full path to the given resource
 *
 * @param name relative resource path
 * @return path to the resource. Must be freed by caller.
*/
BCTBX_PUBLIC char * bc_tester_res(const char *name);

/**
* Get full path to the given writable_file
*
* @param name relative writable file path
* @return path to the writable file. Must be freed by caller.
*/
BCTBX_PUBLIC char * bc_tester_file(const char *name);


/*Redefine the CU_... macros WITHOUT final ';' semicolon, to allow IF conditions and smarter error message */
extern int CU_assertImplementation(int bValue,
                                      unsigned int uiLine,
                                      const char *strCondition,
                                      const char *strFile,
                                      const char *strFunction,
                                      int bFatal);


/**
* Test unit assertion
*
* @return 1 if assert was true, 0 otherwise
*/
BCTBX_PUBLIC int bc_assert(const char* file, int line, int predicate, const char* format);

#define _BC_ASSERT_PRED(name, pred, actual, expected, type, ...) \
	do { \
		char format[4096] = {0}; \
		type cactual = (actual); \
		type cexpected = (expected); \
		snprintf(format, 4096, name "(" #actual ", " #expected ") - " __VA_ARGS__); \
		bc_assert(__FILE__, __LINE__, pred, format); \
	} while (0)

#define BC_PASS(msg) bc_assert(__FILE__, __LINE__, TRUE, "BC_PASS(" #msg ").")
#define BC_FAIL(msg) bc_assert(__FILE__, __LINE__, FALSE, "BC_FAIL(" #msg ").")
#define BC_ASSERT(value) bc_assert(__FILE__, __LINE__, (value), #value)
#define BC_TEST(value) bc_assert(__FILE__, __LINE__, (value), #value)
#define BC_ASSERT_TRUE(value) bc_assert(__FILE__, __LINE__, (value), "BC_ASSERT_TRUE(" #value ")")
#define BC_ASSERT_FALSE(value) bc_assert(__FILE__, __LINE__, !(value), "BC_ASSERT_FALSE(" #value ")")
#define BC_ASSERT_PTR_EQUAL(actual, expected) bc_assert(__FILE__, __LINE__, ((actual) == (expected)), "BC_ASSERT_PTR_EQUAL(" #actual "!=" #expected ")")
#define BC_ASSERT_PTR_NOT_EQUAL(actual, expected) bc_assert(__FILE__, __LINE__, ((actual) != (expected)), "BC_ASSERT_PTR_NOT_EQUAL(" #actual "==" #expected ")")
#define BC_ASSERT_PTR_NULL(value) bc_assert(__FILE__, __LINE__, ((value) == NULL), "BC_ASSERT_PTR_NULL(" #value ")")
#define BC_ASSERT_PTR_NOT_NULL(value) bc_assert(__FILE__, __LINE__, ((value) != NULL), "BC_ASSERT_PTR_NOT_NULL(" #value ")")

#define BC_ASSERT_STRING_NOT_EQUAL(actual, expected) _BC_ASSERT_PRED("BC_ASSERT_STRING_NOT_EQUAL", (strcmp((const char*)(cactual), (const char*)(cexpected))), actual, expected, const char*, "Expected NOT %s but it was.", cexpected)
#define BC_ASSERT_NSTRING_EQUAL(actual, expected, count) _BC_ASSERT_PRED("BC_ASSERT_NSTRING_EQUAL", !(strncmp((const char*)(cactual), (const char*)(cexpected), (size_t)(count))), actual, expected, const char*, "Expected %*s but was %*s.", (int)(count), cexpected, (int)(count), cactual)
#define BC_ASSERT_NSTRING_NOT_EQUAL(actual, expected, count) _BC_ASSERT_PRED("BC_ASSERT_NSTRING_NOT_EQUAL", (strncmp((const char*)(cactual), (const char*)(cexpected), (size_t)(count))), actual, expected, const char*, "Expected %*s but it was.", (int)count, cexpected)
#define BC_ASSERT_DOUBLE_EQUAL(actual, expected, granularity) _BC_ASSERT_PRED("BC_ASSERT_DOUBLE_EQUAL", ((fabs((double)(cactual) - (cexpected)) <= fabs((double)(granularity)))), actual, expected, double, "Expected %f but was %f.", cexpected, cactual)
#define BC_ASSERT_DOUBLE_NOT_EQUAL(actual, expected, granularity) _BC_ASSERT_PRED("BC_ASSERT_DOUBLE_NOT_EQUAL", ((fabs((double)(cactual) - (cexpected)) > fabs((double)(granularity)))), actual, expected, double, "Expected %f but was %f.", cexpected, cactual)

#define BC_ASSERT_EQUAL(actual, expected, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_EQUAL", ((cactual) == (cexpected)), actual, expected, type, "Expected " type_format " but was " type_format ".", cexpected, cactual)
#define BC_ASSERT_NOT_EQUAL(actual, expected, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_NOT_EQUAL", ((cactual) != (cexpected)), actual, expected, type, "Expected NOT " type_format " but it was.", cexpected)
#define BC_ASSERT_STRING_EQUAL(actual, expected) _BC_ASSERT_PRED("BC_ASSERT_STRING_EQUAL", cactual && cexpected && !(strcmp((const char*)(cactual), (const char*)(cexpected))), actual, expected, const char*, "Expected %s but was %s.", cexpected, cactual)
#define BC_ASSERT_GREATER(actual, min, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_GREATER", ((cactual) >= (cexpected)), actual, min, type, "Expected at least " type_format " but was " type_format ".", cexpected, cactual)
#define BC_ASSERT_LOWER(actual, max, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_LOWER", ((cactual) <= (cexpected)), actual, max, type, "Expected at most " type_format " but was " type_format ".", cexpected, cactual)
#define BC_ASSERT_GREATER_STRICT(actual, min, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_GREATER", ((cactual) > (cexpected)), actual, min, type, "Expected more than " type_format " but was " type_format ".", cexpected, cactual)
#define BC_ASSERT_LOWER_STRICT(actual, max, type, type_format) _BC_ASSERT_PRED("BC_ASSERT_LOWER", ((cactual) < (cexpected)), actual, max, type, "Expected less than " type_format " but was " type_format ".", cexpected, cactual)

#ifdef __cplusplus
}
#endif
#endif
