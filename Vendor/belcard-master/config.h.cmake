/***************************************************************************
* config.h.cmake
* Copyright (C) 2015  Belledonne Communications, Grenoble France
*
****************************************************************************
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*
****************************************************************************/

#define BELCARD_MAJOR_VERSION ${BELCARD_MAJOR_VERSION}
#define BELCARD_MINOR_VERSION ${BELCARD_MINOR_VERSION}
#define BELCARD_MICRO_VERSION ${BELCARD_MICRO_VERSION}
#define BELCARD_VERSION "${BELCARD_VERSION}"

#cmakedefine HAVE_BCUNIT_BCUNIT_H 1
#cmakedefine HAVE_CU_GET_SUITE 1
