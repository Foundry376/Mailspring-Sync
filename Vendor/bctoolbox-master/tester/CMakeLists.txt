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

if(ENABLE_SHARED)
	set(PROJECT_LIBS bctoolbox bctoolbox-tester)
else()
	set(PROJECT_LIBS bctoolbox-static bctoolbox-tester-static)
endif()

if(ENABLE_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
	set(TESTER_SOURCES
		bctoolbox_tester.c
		bctoolbox_tester.h
		containers.cc
		port.c
		parser.c
	)
	if(MBEDTLS_FOUND OR POLARSSL_FOUND)
		list(APPEND TESTER_SOURCES crypto.c)
	endif()

	string(REPLACE ";" " " LINK_FLAGS_STR "${LINK_FLAGS}")

	add_executable(bctoolbox_tester_exe ${TESTER_SOURCES})
	if(NOT "${LINK_FLAGS_STR}" STREQUAL "")
		set_target_properties(bctoolbox_tester_exe PROPERTIES LINK_FLAGS "${LINK_FLAGS_STR}")
	endif()
	set_target_properties(bctoolbox_tester_exe PROPERTIES OUTPUT_NAME bctoolbox_tester)
	target_link_libraries(bctoolbox_tester_exe PRIVATE ${PROJECT_LIBS})
	if(MBEDTLS_FOUND)
		target_link_libraries(bctoolbox_tester_exe PRIVATE ${MBEDTLS_LIBRARIES})
		target_include_directories(bctoolbox_tester_exe PRIVATE ${MBEDTLS_INCLUDE_DIRS})
	endif()
	if(POLARSSL_FOUND)
		target_link_libraries(bctoolbox_tester_exe PRIVATE ${POLARSSL_LIBRARIES})
	endif()
	if(DECAF_FOUND)
		target_link_libraries(bctoolbox_tester_exe PRIVATE ${DECAF_LIBRARIES})
	endif()
	set_target_properties(bctoolbox_tester_exe PROPERTIES XCODE_ATTRIBUTE_WARNING_CFLAGS "")
	add_test(NAME bctoolbox_tester COMMAND bctoolbox_tester --verbose)
endif()
