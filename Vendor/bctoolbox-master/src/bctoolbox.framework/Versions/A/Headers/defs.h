/*
defs.h
Copyright (C) 2010-2017 Belledonne Communications SARL

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef BCTBX_DEFS_H_
#define BCTBX_DEFS_H_

/* Macro telling GCC that a 'break' statement has been deliberately omitted
 * in switch block */
#ifndef BCTBX_NO_BREAK
#if defined(__GNUC__) && __GNUC__ >= 7
#define BCTBX_NO_BREAK __attribute__((fallthrough))
#else
#define BCTBX_NO_BREAK
#endif // __GNUC__
#endif // BCTBX_NO_BREAK


#endif /* BCTBX_DEFS_H_ */
