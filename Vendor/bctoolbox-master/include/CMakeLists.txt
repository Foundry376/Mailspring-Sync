############################################################################
# CMakeLists.txt
# Copyright (C) 2016  Belledonne Communications, Grenoble France
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

set(HEADER_FILES
	charconv.h
	compiler.h
	defs.h
	exception.hh
	list.h
	logging.h
	map.h
	parser.h
	port.h
	regex.h
	vconnect.h
	vfs.h
)

if(MBEDTLS_FOUND OR POLARSSL_FOUND)
	list(APPEND HEADER_FILES crypto.h)
endif()

if(ENABLE_TESTS_COMPONENT)
	list(APPEND HEADER_FILES tester.h)
endif()

set(BCTOOLBOX_HEADER_FILES )
foreach(HEADER_FILE ${HEADER_FILES})
	list(APPEND BCTOOLBOX_HEADER_FILES "${CMAKE_CURRENT_LIST_DIR}/bctoolbox/${HEADER_FILE}")
endforeach()
set(BCTOOLBOX_HEADER_FILES ${BCTOOLBOX_HEADER_FILES} PARENT_SCOPE)

install(FILES ${BCTOOLBOX_HEADER_FILES}
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bctoolbox
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
