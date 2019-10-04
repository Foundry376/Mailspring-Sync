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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <regex>

#include "bctoolbox/logging.h"
#include "bctoolbox/regex.h"

/*
	This part is needed since CentOS7 have an old gcc compiler.
	TODO: Remove this code when all supported platorms have gcc 4.9.0 or more 
 */
#if __cplusplus >= 201103L                             && \
    (!defined(__GLIBCXX__) || (__cplusplus >= 201402L) || \
        (defined(_GLIBCXX_REGEX_DFS_QUANTIFIERS_LIMIT) || \
         defined(_GLIBCXX_REGEX_STATE_LIMIT)           || \
             (defined(_GLIBCXX_RELEASE)                && \
             _GLIBCXX_RELEASE > 4)))                   && \
	!defined(__ANDROID__) || defined(_WIN32)
#define HAVE_WORKING_REGEX 1
#else
#define HAVE_WORKING_REGEX 0
#include <regex.h>
#endif

extern "C" bool_t bctbx_is_matching_regex(const char *entry, const char* regex) {
#if HAVE_WORKING_REGEX
	try {
		std::regex entry_regex(regex, std::regex_constants::extended | std::regex_constants::nosubs);
		std::cmatch m;
		return std::regex_match(entry, m, entry_regex);
	}
	catch (const std::regex_error& e) {
		bctbx_error("Could not compile regex '%s': %s", regex, e.what());
		return FALSE;
	}
#else
	regex_t regex_pattern;
	char err_msg[256];
	int res;
	res = regcomp(&regex_pattern, regex, REG_EXTENDED | REG_NOSUB);
	if(res != 0) {
		regerror(res, &regex_pattern, err_msg, sizeof(err_msg));
		bctbx_error("Could not compile regex '%s': %s", regex, err_msg);
		return FALSE;
	}
	res = regexec(&regex_pattern, entry, 0, NULL, 0);
	regfree(&regex_pattern);
	return (res != REG_NOMATCH);
#endif
}
