/*
    bctoolbox
    Copyright (C) 2017  Belledonne Communications SARL

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

#ifndef BCTBX_PARSER_H_
#define BCTBX_PARSER_H_

#include "bctoolbox/port.h"

#ifdef __cplusplus
extern "C"
{
#endif
/**
 * A 256 entries table where each entries defines if character corresponding to its index is allowed  or not (value = 0)
 * for instance noescape_rules[':'] = 1 means that  ':' should not be escaped
 */
typedef  unsigned char bctbx_noescape_rules_t[256+1]; /*last entry (BCTBX_NOESCAPE_RULES_USER_INDEX) is reserved for user purpose. Might be usefull to set if array was initialed of not */
	
#define BCTBX_NOESCAPE_RULES_USER_INDEX (sizeof(bctbx_noescape_rules_t) -1)
/**
 * Allocate a new string with unauthorized characters escaped (I.E replaced by % HEX HEX) if any.
 * sample:
 * bctbx_noescape_rules_t my_rules = {0}; nothing allowed
 * bctbx_noescape_rules_add_alfanums(my_rules);
 * char * my_escaped_string = bctbx_escape("François",my_rules);
 * expeted result my_escaped_string == Fran%c3%a7ois
 * @param  buff  NULL terminated input buffer.
 * @param  noescape_rules bctbx_noescape_rules_t to apply for this input buff

 * @return a newly allocated null terminated string
 */
BCTBX_PUBLIC char* bctbx_escape(const char* buff, const bctbx_noescape_rules_t noescape_rules);

/**
 * Add a list of allowed charaters to a noescape rule.
 * @param  noescape_rules rule to be modified.
 * @param  allowed string representing allowed char. Sample:  ";-/" 
 */
BCTBX_PUBLIC void bctbx_noescape_rules_add_list(bctbx_noescape_rules_t noescape_rules, const char *allowed);

/**
 * Add a range of allowed charaters to noescape rule. bctbx_noescape_rules_add_range(noescape_rules, '0','9') is the same as bctbx_noescape_rules_add_list(noescape_rules,"0123456789")
 * @param noescape_rules rule to be modified.
 * @param first allowed char.
 * @param last allowed char.
 */
BCTBX_PUBLIC void bctbx_noescape_rules_add_range(bctbx_noescape_rules_t noescape_rules, char first, char last);
/**
 * Add ['0'..'9'], ['a'..'z'] ['A'..'z'] to no escape rule. 
 */
BCTBX_PUBLIC void bctbx_noescape_rules_add_alfanums(bctbx_noescape_rules_t noescape_rules);

/**
 * Allocate a new string with escaped character (I.E % HEX HEX) replaced by unicode char.
 * @param  buff  NULL terminated input buffer.
 * @return a newly allocated null terminated string with unescated values.
 */
BCTBX_PUBLIC char* bctbx_unescaped_string(const char* buff);

	
/**
 *Convert a single input "a" into unscaped output if need.
 * a = "%c3%a7" into "ç" with return value = 3
 * a = "A" into "A" with return value = 1
 * @param a input value
 * @param out outbut buffer for conversion
 * @return number of byte wrote into out
 */
BCTBX_PUBLIC size_t bctbx_get_char (const char *a, char *out);
	
#ifdef __cplusplus
}
#endif

#endif /*BCTBX_PARSER_H_*/




