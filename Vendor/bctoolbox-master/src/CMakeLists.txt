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

set(BCTOOLBOX_C_SOURCE_FILES
	containers/list.c
	logging/logging.c
	parser.c
	utils/port.c
	vconnect.c
	vfs.c
)

set(BCTOOLBOX_CXX_SOURCE_FILES
	containers/map.cc
	conversion/charconv_encoding.cc
	utils/exception.cc
	utils/regex.cc
)

if(ANDROID)
	list(APPEND BCTOOLBOX_CXX_SOURCE_FILES conversion/charconv_android.cc)
elseif(WIN32)
	list(APPEND BCTOOLBOX_CXX_SOURCE_FILES conversion/charconv_windows.cc)
else()
	list(APPEND BCTOOLBOX_CXX_SOURCE_FILES conversion/charconv.cc)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	list(APPEND STRICT_OPTIONS_CXX "-x c++")
endif()
if(MBEDTLS_FOUND OR POLARSSL_FOUND)
	list(APPEND BCTOOLBOX_C_SOURCE_FILES crypto/crypto.c)
	list(APPEND BCTOOLBOX_CXX_SOURCE_FILES crypto/ecc.cc)
endif()
if(MBEDTLS_FOUND)
	list(APPEND BCTOOLBOX_C_SOURCE_FILES crypto/mbedtls.c)
endif()
if(POLARSSL_FOUND)
	if(POLARSSL_VERSION13_OK)
		list(APPEND BCTOOLBOX_C_SOURCE_FILES crypto/polarssl.c)
	else()
		list(APPEND BCTOOLBOX_C_SOURCE_FILES crypto/polarssl1.2.c)
	endif()
endif()
if(ENABLE_TESTS_COMPONENT)
	set(BCTOOLBOX_C_TESTER_SOURCE_FILES tester.c)
endif()

set(BCTOOLBOX_SOURCE_FILES ${BCTOOLBOX_C_SOURCE_FILES} ${BCTOOLBOX_CXX_SOURCE_FILES})
set(BCTOOLBOX_TESTER_SOURCE_FILES ${BCTOOLBOX_C_TESTER_SOURCE_FILES})

bc_apply_compile_flags(BCTOOLBOX_C_SOURCE_FILES STRICT_OPTIONS_CPP STRICT_OPTIONS_C)
bc_apply_compile_flags(BCTOOLBOX_C_TESTER_SOURCE_FILES STRICT_OPTIONS_CPP STRICT_OPTIONS_C)
bc_apply_compile_flags(BCTOOLBOX_CXX_SOURCE_FILES STRICT_OPTIONS_CPP STRICT_OPTIONS_CXX)

if(ENABLE_STATIC)
	add_library(bctoolbox-static STATIC ${BCTOOLBOX_SOURCE_FILES} ${BCTOOLBOX_HEADER_FILES})
	target_link_libraries(bctoolbox-static INTERFACE ${CMAKE_THREAD_LIBS_INIT})
	if(APPLE)
		target_link_libraries(bctoolbox-static INTERFACE "-framework Foundation" "-framework AVFoundation")
		target_include_directories(bctoolbox-static SYSTEM PRIVATE ${ICONV_INCLUDE_DIRS})
		target_link_libraries(bctoolbox-static INTERFACE ${ICONV_LIBRARIES})
	endif()
	if(WIN32)
		target_link_libraries(bctoolbox-static INTERFACE "Winmm" "Ws2_32")
	endif()
	set_target_properties(bctoolbox-static PROPERTIES LINKER_LANGUAGE "CXX")
	if(NOT CMAKE_VERSION VERSION_LESS 3.1)
		set_target_properties(bctoolbox-static PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
	endif()
	set_target_properties(bctoolbox-static PROPERTIES OUTPUT_NAME bctoolbox)
	if(HAVE_LIBRT)
		target_link_libraries(bctoolbox-static INTERFACE rt)
	endif()
	if(HAVE_LIBDL)
		target_link_libraries(bctoolbox-static INTERFACE dl)
	endif()
	if(ANDROID)
		target_link_libraries(bctoolbox-static INTERFACE log)
	endif()
	if(ENABLE_TESTS_COMPONENT)
		add_library(bctoolbox-tester-static STATIC ${BCTOOLBOX_TESTER_SOURCE_FILES})
		set_target_properties(bctoolbox-tester-static PROPERTIES OUTPUT_NAME bctoolbox-tester)
		target_link_libraries(bctoolbox-tester-static INTERFACE bctoolbox-static)
	endif()
endif()
if(ENABLE_SHARED)
	add_library(bctoolbox SHARED ${BCTOOLBOX_SOURCE_FILES} ${BCTOOLBOX_HEADER_FILES})
	target_link_libraries(bctoolbox PRIVATE ${CMAKE_THREAD_LIBS_INIT})
	if(APPLE)
		if(IOS)
			set(MIN_OS ${LINPHONE_IOS_DEPLOYMENT_TARGET})
		else()
			set(MIN_OS ${CMAKE_OSX_DEPLOYMENT_TARGET})
		endif()
		set_target_properties(bctoolbox PROPERTIES
			FRAMEWORK TRUE
			MACOSX_FRAMEWORK_IDENTIFIER org.linphone.bctoolbox
			MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_SOURCE_DIR}/build/osx/Info.plist.in"
			PUBLIC_HEADER "${BCTOOLBOX_HEADER_FILES}"
		)
	endif()
	if(APPLE)
		target_link_libraries(bctoolbox PRIVATE "-framework Foundation" "-framework AVFoundation")
		target_include_directories(bctoolbox SYSTEM PRIVATE ${ICONV_INCLUDE_DIRS})
		target_link_libraries(bctoolbox PRIVATE ${ICONV_LIBRARIES})
	endif()
	if(WIN32)
		if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
			target_link_libraries(bctoolbox PRIVATE "Winmm" "Ws2_32")
		endif()
		target_compile_definitions(bctoolbox PRIVATE "-DBCTBX_EXPORTS")
	endif()
	set_target_properties(bctoolbox PROPERTIES LINKER_LANGUAGE "CXX")
	if(NOT CMAKE_VERSION VERSION_LESS 3.1)
	 	set_target_properties(bctoolbox PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
	endif()
	if(NOT ANDROID)
		# Do not version shared library on Android
		set_target_properties(bctoolbox PROPERTIES SOVERSION ${BCTOOLBOX_SO_VERSION})
	endif()
	if(MSVC)
		if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
			install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bctoolbox.pdb
				DESTINATION ${CMAKE_INSTALL_BINDIR}
				PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
			)
		endif()
	endif()
	if(HAVE_LIBRT)
		target_link_libraries(bctoolbox PRIVATE rt)
	endif()
	if(HAVE_LIBDL)
		target_link_libraries(bctoolbox PRIVATE dl)
	endif()
	if(ANDROID)
		target_link_libraries(bctoolbox PRIVATE log)
	endif()
	if(ENABLE_TESTS_COMPONENT)
		add_library(bctoolbox-tester SHARED ${BCTOOLBOX_TESTER_SOURCE_FILES})
		set_target_properties(bctoolbox-tester PROPERTIES LINKER_LANGUAGE "CXX")
		target_link_libraries(bctoolbox-tester PUBLIC bctoolbox PRIVATE ${BCUNIT_TARGETNAME})
		if(WIN32)
			target_compile_definitions(bctoolbox-tester PRIVATE "-DBCTBX_EXPORTS")
		endif()
		if(NOT ANDROID)
			# Do not version shared library on Android
			set_target_properties(bctoolbox-tester PROPERTIES SOVERSION ${BCTOOLBOXTESTER_SO_VERSION})
		endif()
		if(MSVC)
			if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
				install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bctoolbox-tester.pdb
					DESTINATION ${CMAKE_INSTALL_BINDIR}
					PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
				)
			endif()
		endif()
	endif()
endif()

if(ENABLE_STATIC)
	target_include_directories(bctoolbox-static PUBLIC
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	)
endif()

if(ENABLE_SHARED)
	target_include_directories(bctoolbox PUBLIC
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	)
endif()

if(MBEDTLS_FOUND)
	if(ENABLE_STATIC)
		target_include_directories(bctoolbox-static SYSTEM PRIVATE ${MBEDTLS_INCLUDE_DIRS})
		target_link_libraries(bctoolbox-static INTERFACE ${MBEDTLS_LIBRARIES})
	endif()
	if(ENABLE_SHARED)
		target_include_directories(bctoolbox SYSTEM PRIVATE ${MBEDTLS_INCLUDE_DIRS})
		target_link_libraries(bctoolbox PRIVATE ${MBEDTLS_LIBRARIES})
	endif()
endif()
if(POLARSSL_FOUND)
	if(ENABLE_STATIC)
		target_include_directories(bctoolbox-static SYSTEM PRIVATE ${POLARSSL_INCLUDE_DIRS})
		target_link_libraries(bctoolbox-static INTERFACE ${POLARSSL_LIBRARIES})
	endif()
	if(ENABLE_SHARED)
		target_include_directories(bctoolbox PRIVATE ${POLARSSL_INCLUDE_DIRS})
		target_link_libraries(bctoolbox PRIVATE ${POLARSSL_LIBRARIES})
	endif()
endif()
if(DECAF_FOUND)
	if(ENABLE_STATIC)
		target_include_directories(bctoolbox-static PRIVATE ${DECAF_INCLUDE_DIRS})
		target_link_libraries(bctoolbox-static PUBLIC decaf-static)
	endif()
	if(ENABLE_SHARED)
		target_include_directories(bctoolbox PRIVATE ${DECAF_INCLUDE_DIRS})
		target_link_libraries(bctoolbox PRIVATE ${DECAF_TARGETNAME})
	endif()
endif()

if(ENABLE_TESTS_COMPONENT)
	if(ENABLE_STATIC)
		target_link_libraries(bctoolbox-tester-static PUBLIC bctoolbox-static ${BCUNIT_TARGETNAME})
	endif()
	if(ENABLE_SHARED)
		if(APPLE)
			set_target_properties(bctoolbox-tester PROPERTIES
				FRAMEWORK TRUE
				MACOSX_FRAMEWORK_IDENTIFIER com.belledonne-communications.bctoolbox-tester
				MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_SOURCE_DIR}/build/osx/Info.plist.in"
				PUBLIC_HEADER "${BCTOOLBOX_HEADER_FILES}"
			)
		endif()
	endif()
endif()


if(ENABLE_STATIC)
	install(TARGETS bctoolbox-static EXPORT ${EXPORT_TARGETS_NAME}Targets
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		COMPONENT core
	)
	if(ENABLE_TESTS_COMPONENT)
		install(TARGETS bctoolbox-tester-static EXPORT ${EXPORT_TARGETS_NAME}Targets
			ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
			PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
			COMPONENT tester
		)
	endif()
endif()
if(ENABLE_SHARED)
	install(TARGETS bctoolbox EXPORT ${EXPORT_TARGETS_NAME}Targets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		FRAMEWORK DESTINATION Frameworks
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		COMPONENT core
	)
	if(ENABLE_TESTS_COMPONENT)
		install(TARGETS bctoolbox-tester EXPORT ${EXPORT_TARGETS_NAME}Targets
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
			LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
			ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
			FRAMEWORK DESTINATION Frameworks
			PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
			COMPONENT tester
		)
	endif()
endif()

if(MSVC)
	# Disable "was deprecated" warnings of windows compiler (typically using strcpy_s instead of strcpy and stupid things like this).
	if(ENABLE_SHARED)
		target_compile_options(bctoolbox PRIVATE "/wd4996")
	endif()
	if(ENABLE_STATIC)
		target_compile_options(bctoolbox-static PRIVATE "/wd4996")
	endif()
	if(ENABLE_TESTS_COMPONENT)
		target_compile_options(bctoolbox-tester PRIVATE "/wd4996")
	endif()
endif()
