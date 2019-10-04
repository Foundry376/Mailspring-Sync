
############################################################################
# CMakeLists.txt
# Copyright (C) 2015  Belledonne Communications, Grenoble France
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

cmake_minimum_required(VERSION 3.1)
project(BELCARD VERSION 1.0.2 LANGUAGES C CXX)

set(BELCARD_SO_VERSION "1")

option(ENABLE_SHARED "Build shared library." ON)
option(ENABLE_STATIC "Build static library." ON)
option(ENABLE_STRICT "Build with strict compile options." YES)
option(ENABLE_TOOLS "Enable compilation of tools." YES)
option(ENABLE_UNIT_TESTS "Enable compilation of unit tests." YES)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CPACK_GENERATOR AND NOT CMAKE_INSTALL_RPATH AND CMAKE_INSTALL_PREFIX)
	set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR})
	message(STATUS "Setting install rpath to ${CMAKE_INSTALL_RPATH}")
endif()

include(GNUInstallDirs)
include(CheckSymbolExists)
include(CMakePushCheckState)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

find_package(belr REQUIRED CONFIG)

find_package(bctoolbox 0.0.3 REQUIRED OPTIONAL_COMPONENTS tester)

if(UNIX AND NOT APPLE)
	include(CheckIncludeFiles)
	check_include_files(libudev.h HAVE_LIBUDEV_H)
endif()

include_directories(
	include/
	src/
	${CMAKE_CURRENT_BINARY_DIR}
)
if(MSVC)
	include_directories(${MSVC_INCLUDE_DIR})
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/config.h PROPERTIES GENERATED ON)
add_definitions("-DHAVE_CONFIG_H")

set(BELCARD_CPPFLAGS ${BCTOOLBOX_CPPFLAGS} ${BELR_CPPFLAGS})
if(ENABLE_STATIC)
	list(APPEND BELCARD_CPPFLAGS "-DBELCARD_STATIC")
endif()
if(BELCARD_CPPFLAGS)
	list(REMOVE_DUPLICATES BELCARD_CPPFLAGS)
	add_definitions(${BELCARD_CPPFLAGS})
endif()
add_definitions("-DBELCARD_EXPORTS")

set(STRICT_OPTIONS_CPP )
set(STRICT_OPTIONS_C )
set(STRICT_OPTIONS_CXX )
set(STRICT_OPTIONS_OBJC )
if(MSVC)
	if(ENABLE_STRICT)
		list(APPEND STRICT_OPTIONS_CPP "/WX")
	endif()
else()
	list(APPEND STRICT_OPTIONS_CXX "-std=c++11")
	list(APPEND STRICT_OPTIONS_CPP "-Wall" "-Wuninitialized" "-Wno-error=deprecated-declarations")
	list(APPEND STRICT_OPTIONS_C "-Wdeclaration-after-statement" "-Wstrict-prototypes" "-Wno-error=strict-prototypes")
	if(CMAKE_C_COMPILER_ID MATCHES "Clang")
		list(APPEND STRICT_OPTIONS_CPP "-Qunused-arguments" "-Wno-array-bounds")
	endif()
	if(APPLE)
		list(APPEND STRICT_OPTIONS_CPP "-Wno-error=unknown-warning-option" "-Qunused-arguments" "-Wno-tautological-compare" "-Wno-unused-function" "-Wno-array-bounds")
	endif()
	if(ENABLE_STRICT)
		list(APPEND STRICT_OPTIONS_CPP "-Werror" "-Wextra" "-Wno-unused-parameter" "-fno-strict-aliasing")
	endif()
endif()
if(STRICT_OPTIONS_CPP)
	list(REMOVE_DUPLICATES STRICT_OPTIONS_CPP)
endif()
if(STRICT_OPTIONS_C)
	list(REMOVE_DUPLICATES STRICT_OPTIONS_C)
endif()

set(EXPORT_TARGETS_NAME "belcard")

add_subdirectory(include)
add_subdirectory(src)

if(ENABLE_UNIT_TESTS)
	add_subdirectory(tester)
endif()

if(ENABLE_TOOLS)
	add_subdirectory(tools)
endif()

include(CMakePackageConfigHelpers)
set(CONFIG_PACKAGE_LOCATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/cmake")
set(INCLUDE_INSTALL_DIR "include")

export(EXPORT ${EXPORT_TARGETS_NAME}Targets
	FILE "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}Targets.cmake"
)

configure_package_config_file(cmake/BelcardConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}Config.cmake"
  	INSTALL_DESTINATION ${CONFIG_PACKAGE_LOCATION}
	NO_SET_AND_CHECK_MACRO
)
set(ConfigPackageLocation "share/${EXPORT_TARGETS_NAME}/cmake")
install(EXPORT ${EXPORT_TARGETS_NAME}Targets
	FILE ${EXPORT_TARGETS_NAME}Targets.cmake
	DESTINATION ${ConfigPackageLocation}
)
install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}Config.cmake"
	DESTINATION ${ConfigPackageLocation}
)

add_subdirectory(build)
