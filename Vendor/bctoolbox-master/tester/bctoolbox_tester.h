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

#ifndef _BCTOOLBOX_TESTER_H
#define _BCTOOLBOX_TESTER_H

#include "bctoolbox/logging.h"
#include "bctoolbox/tester.h"

#ifdef __cplusplus

#define SLOGD BCTBX_SLOGD("bctoolbox-tester")
#define SLOGI BCTBX_SLOGI("bctoolbox-tester")
#define SLOGW BCTBX_SLOGW("bctoolbox-tester")
#define SLOGE BCTBX_SLOGE("bctoolbox-tester")

extern "C" {
#endif

extern test_suite_t containers_test_suite;
extern test_suite_t utils_test_suite;
extern test_suite_t crypto_test_suite;
extern test_suite_t parser_test_suite;

#ifdef __cplusplus
};
#endif


#endif /* _BCTOOLBOX_TESTER_H */
