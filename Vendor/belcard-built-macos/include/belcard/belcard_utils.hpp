/*
	belcard_utils.hpp
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

#ifndef belcard_utils_hpp
#define belcard_utils_hpp

#include <string>

#ifdef _MSC_VER
	#ifdef BELCARD_STATIC
		#define BELCARD_PUBLIC
	#else
		#ifdef BELCARD_EXPORTS
			#define BELCARD_PUBLIC	__declspec(dllexport)
		#else
			#define BELCARD_PUBLIC	__declspec(dllimport)
		#endif
	#endif
#else
	#define BELCARD_PUBLIC
#endif


BELCARD_PUBLIC std::string belcard_fold(const std::string &input);
BELCARD_PUBLIC std::string belcard_unfold(const std::string &input);
BELCARD_PUBLIC std::string belcard_read_file(const std::string &filename);

#endif
