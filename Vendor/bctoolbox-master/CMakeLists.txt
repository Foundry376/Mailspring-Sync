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

cmake_minimum_required(VERSION 3.2) #since cmake 3.2 is available as a patch for yocto Fido
set(CMAKE_MACOSX_RPATH ON) # Before cmake 3.0.0, MACOSX_RPATH was not set to ON by default - however this is no good reason to not enable it by default

project(bctoolbox)

set(BCTOOLBOX_VERSION_MAJOR 0)
set(BCTOOLBOX_VERSION_MINOR 6)
set(BCTOOLBOX_VERSION_PATCH 0)
set(BCTOOLBOX_SO_VERSION 1)
set(BCTOOLBOXTESTER_SO_VERSION 1)

set(BCTOOLBOX_VERSION "${BCTOOLBOX_VERSION_MAJOR}.${BCTOOLBOX_VERSION_MINOR}.${BCTOOLBOX_VERSION_PATCH}")
set(PROJECT_VERSION_MAJOR ${BCTOOLBOX_VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${BCTOOLBOX_VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${BCTOOLBOX_VERSION_PATCH})
set(PROJECT_VERSION "${BCTOOLBOX_VERSION}")


option(ENABLE_SHARED "Build shared library." ON)
option(ENABLE_STATIC "Build static library." ON)
option(ENABLE_POLARSSL "Enable polarssl support" ON)
option(ENABLE_MBEDTLS "Enable mabedtls support" ON)
option(ENABLE_DECAF "Enable Elliptic Curve Cryptography support" ON)
option(ENABLE_STRICT "Pass strict flags to the compiler" ON)
option(ENABLE_TESTS_COMPONENT "Enable compilation of tests helper library" ON)
option(ENABLE_TESTS "Enable compilation of tests" ON)


include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(CMakePushCheckState)
include(GNUInstallDirs)
include(cmake/BcToolboxCMakeUtils.cmake)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CPACK_GENERATOR AND NOT CMAKE_INSTALL_RPATH AND CMAKE_INSTALL_PREFIX)
	set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR})
	message(STATUS "Setting install rpath to ${CMAKE_INSTALL_RPATH}")
endif()

if(APPLE)
	find_package(Iconv REQUIRED)
endif()

if(ENABLE_MBEDTLS)
	find_package(MbedTLS)
	if(MBEDTLS_FOUND)
		message(STATUS "Using mbedTLS")
		set(HAVE_MBEDTLS 1)
	endif()
endif()

if(ENABLE_DECAF)
	find_package(Decaf CONFIG)
	if(DECAF_FOUND)
		message(STATUS "Using Decaf")
		set(HAVE_DECAF 1)
	endif()
endif()

if(ENABLE_POLARSSL AND NOT MBEDTLS_FOUND)
	find_package(PolarSSL REQUIRED)
	if(POLARSSL_FOUND)
		message(STATUS "Using polarSSL")
		set(HAVE_POLARSSL 1)
		if(CTR_DRBG_FREE)
			set(HAVE_CTR_DRGB_FREE 1)
		endif()
	endif()
endif()

if(HAVE_SSL_GET_DTLS_SRTP_PROTECTION_PROFILE)
	message(STATUS "DTLS SRTP available")
	set(HAVE_DTLS_SRTP 1)
else()
	message(STATUS "DTLS SRTP not available")
endif()

if(MBEDTLS_FOUND)
	get_filename_component(mbedtls_library_path "${MBEDTLS_LIBRARIES}" PATH)
	string(REPLACE ";" " " MBEDTLS_LIBRARIES_STR "${MBEDTLS_LIBRARIES}")
	set(LIBS_PRIVATE "${LIBS_PRIVATE} ${MBEDTLS_LIBRARIES_STR}")
endif()
if(POLARSSL_FOUND)
	get_filename_component(polarssl_library_path "${POLARSSL_LIBRARIES}" PATH)
	set(LIBS_PRIVATE "${LIBS_PRIVATE} -L${polarssl_library_path} -lpolarssl")
endif()

if(ENABLE_TESTS_COMPONENT)
  find_package(BcUnit CONFIG REQUIRED)
  cmake_push_check_state(RESET)
	list(APPEND CMAKE_REQUIRED_INCLUDES ${BCUNIT_INCLUDE_DIRS})
	list(APPEND CMAKE_REQUIRED_LIBRARIES ${BCUNIT_LIBRARIES})
	check_symbol_exists("CU_get_suite" "BCUnit/BCUnit.h" HAVE_CU_GET_SUITE)
	check_symbol_exists("CU_curses_run_tests" "BCUnit/BCUnit.h" HAVE_CU_CURSES)
	check_symbol_exists("CU_set_trace_handler" "BCUnit/Util.h" HAVE_CU_SET_TRACE_HANDLER)
	cmake_pop_check_state()
	set(TESTER_REQUIRES_PRIVATE "bcunit")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bctoolbox.pc.in ${CMAKE_CURRENT_BINARY_DIR}/bctoolbox.pc)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bctoolbox.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(ENABLE_TESTS_COMPONENT)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bctoolbox-tester.pc.in ${CMAKE_CURRENT_BINARY_DIR}/bctoolbox-tester.pc)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bctoolbox-tester.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

find_package(Threads REQUIRED)

include_directories(
	include
	src
	${CMAKE_CURRENT_BINARY_DIR}
)

set(BCTOOLBOX_CPPFLAGS )
if(ENABLE_STATIC)
	set(BCTBX_STATIC 1)
	list(APPEND BCTOOLBOX_CPPFLAGS "-DBCTBX_STATIC")
endif()

bc_init_compilation_flags(STRICT_OPTIONS_CPP STRICT_OPTIONS_C STRICT_OPTIONS_CXX ENABLE_STRICT)
if(CMAKE_VERSION VERSION_LESS 3.1 AND NOT APPLE AND NOT MSVC)
	list(APPEND STRICT_OPTIONS_CXX "-std=c++11")
endif()

set(EXPORT_TARGETS_NAME "bctoolbox")

check_library_exists("rt" "clock_gettime" "" HAVE_LIBRT)
check_library_exists("dl" "dladdr" "" HAVE_LIBDL)
check_include_file("execinfo.h" HAVE_EXECINFO)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/config.h PROPERTIES GENERATED ON)
add_definitions("-DHAVE_CONFIG_H")

add_subdirectory(include)
add_subdirectory(src)
if(ENABLE_TESTS AND ENABLE_TESTS_COMPONENT)
	add_subdirectory(tester)
endif()

add_subdirectory(build)

include(CMakePackageConfigHelpers)
set(CONFIG_PACKAGE_LOCATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/cmake")

write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}ConfigVersion.cmake"
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY AnyNewerVersion
)
configure_package_config_file(cmake/BcToolboxConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}Config.cmake"
	INSTALL_DESTINATION ${CONFIG_PACKAGE_LOCATION}
	NO_SET_AND_CHECK_MACRO
)

configure_file(cmake/BcToolboxCMakeUtils.cmake "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}CMakeUtils.cmake" COPYONLY)
configure_file(cmake/BcGitVersion.cmake "${CMAKE_CURRENT_BINARY_DIR}/BcGitVersion.cmake" COPYONLY)
configure_file(cmake/gitversion.h.in "${CMAKE_CURRENT_BINARY_DIR}/gitversion.h.in" COPYONLY)

install(EXPORT ${EXPORT_TARGETS_NAME}Targets
	FILE "${EXPORT_TARGETS_NAME}Targets.cmake"
	DESTINATION ${CONFIG_PACKAGE_LOCATION}
)

install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}Config.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}ConfigVersion.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_TARGETS_NAME}CMakeUtils.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/BcGitVersion.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/gitversion.h.in"
	DESTINATION ${CONFIG_PACKAGE_LOCATION}
)
