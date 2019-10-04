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

set(LIBS bctoolbox)

set(BELR_HEADER_FILES )
set(BELR_SOURCE_FILES_C )
set(BELR_SOURCE_FILES_CXX
	abnf.cpp
	belr.cpp
	grammarbuilder.cpp
	parser.cpp
	binarystream.cpp
)

bc_apply_compile_flags(BELR_SOURCE_FILES_C STRICT_OPTIONS_CPP STRICT_OPTIONS_C)
bc_apply_compile_flags(BELR_SOURCE_FILES_CXX STRICT_OPTIONS_CPP STRICT_OPTIONS_CXX)

add_definitions(
	-DBCTBX_LOG_DOMAIN="belr"
)

if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
	list(APPEND LIBS ws2_32)
endif()

if(ENABLE_STATIC)
	add_library(belr STATIC ${BELR_HEADER_FILES} ${BELR_SOURCE_FILES_C} ${BELR_SOURCE_FILES_CXX})
	set_target_properties(belr PROPERTIES OUTPUT_NAME belr)
	target_link_libraries(belr PUBLIC ${LIBS})
	target_include_directories(belr INTERFACE
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	)
endif()
if(ENABLE_SHARED)
	add_library(belr SHARED ${BELR_HEADER_FILES} ${BELR_SOURCE_FILES_C} ${BELR_SOURCE_FILES_CXX})
	if(APPLE)
		if(IOS)
			set(MIN_OS ${LINPHONE_IOS_DEPLOYMENT_TARGET})
		else()
			set(MIN_OS ${CMAKE_OSX_DEPLOYMENT_TARGET})
		endif()
		set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/build/osx/")
		set_target_properties(belr PROPERTIES
			FRAMEWORK TRUE
			MACOSX_FRAMEWORK_IDENTIFIER org.linphone.belr
			MACOSX_FRAMEWORK_INFO_PLIST Info.plist.in
			PUBLIC_HEADER "${BELR_HEADER_FILES}"
		)
	endif()
	set_target_properties(belr PROPERTIES VERSION ${BELR_SO_VERSION})
	target_include_directories(belr INTERFACE
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	)
	target_link_libraries(belr PUBLIC ${LIBS})
	if(MSVC)
		if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
			install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/belr.pdb
				DESTINATION ${CMAKE_INSTALL_BINDIR}
				PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
			)
		endif()
	endif()
endif()

install(TARGETS belr EXPORT ${EXPORT_TARGET_NAME}Targets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	FRAMEWORK DESTINATION Frameworks
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(FILES ${BELR_HEADER_FILES}
	DESTINATION include/belr
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
