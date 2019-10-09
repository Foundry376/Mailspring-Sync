/*
	bctoolbox
	Copyright (C) 2016  Belledonne Communications SARL.
 
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.
 
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.
 
	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BCTBX_REGEX_H
#define BCTBX_REGEX_H

#ifdef __cplusplus
extern "C" {
#endif

BCTBX_PUBLIC bool_t bctbx_is_matching_regex(const char *entry, const char* regex);

#ifdef __cplusplus
}
#endif

#endif /* BCTBX_REGEX_H */
