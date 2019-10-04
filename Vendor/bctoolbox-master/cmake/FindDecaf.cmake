############################################################################
# FindDecaf.txt
# Copyright (C) 2017  Belledonne Communications, Grenoble France
#
############################################################################
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
############################################################################
#
# - Find the decaf include files and library
#
#  DECAF_FOUND - system has lib decaf
#  DECAF_INCLUDE_DIRS - the decaf include directory
#  DECAF_LIBRARY - The library needed to use decaf

include(CMakePushCheckState)
include(CheckIncludeFile)


find_path(DECAF_INCLUDE_DIRS
	NAMES decaf.h
	PATH_SUFFIXES include/decaf
)

# find the three mbedtls library
find_library(DECAF_LIBRARY
	NAMES decaf
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Decaf
	DEFAULT_MSG
	DECAF_INCLUDE_DIRS DECAF_LIBRARY
)

mark_as_advanced(DECAF_INCLUDE_DIRS DECAF_LIBRARY)
