/*
 * Copyright (c) 2016-2019 Belledonne Communications SARL.
 *
 * This file is part of belr - a language recognition library for ABNF-defined grammars.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef belr_tester_hpp
#define belr_tester_hpp

#include <bctoolbox/tester.h>
#include "belr/grammarbuilder.h"
#include "belr/abnf.h"
#include "belr/belr.h"


#include <fstream>
#include <string>
#include <memory>
#include <sstream>
#include <iostream>
#include <vector>
#include <chrono>


std::string bcTesterFile(const std::string &name);
std::string bcTesterRes(const std::string &name);

#ifdef __cplusplus
extern "C" {
#endif

extern test_suite_t grammar_suite;


void belr_tester_init(void(*ftester_printf)(int level, const char *fmt, va_list args));
void belr_tester_uninit(void);

#ifdef __cplusplus
};
#endif



#endif
