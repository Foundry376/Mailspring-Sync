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

if(ENABLE_SHARED)
	set(BELR_LIBRARIES_FOR_TOOLS belr)
else()
	set(BELR_LIBRARIES_FOR_TOOLS belr)
endif()

foreach(EXECUTABLE belr-parse belr-demo belr-compiler)
	set(SOURCE_FILES_CXX ${EXECUTABLE}.cc)
	bc_apply_compile_flags(SOURCE_FILES_CXX STRICT_OPTIONS_CPP STRICT_OPTIONS_CXX)
	add_executable(${EXECUTABLE} ${SOURCE_FILES_CXX})
	set_target_properties(${EXECUTABLE} PROPERTIES LINKER_LANGUAGE CXX)
	target_link_libraries(${EXECUTABLE} ${BELR_LIBRARIES_FOR_TOOLS} bctoolbox)
endforeach()

if(NOT IOS)
	install(TARGETS belr-parse belr-compiler
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
	)
endif()
