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
	set(BELCARD_LIBRARIES_FOR_TOOLS belcard)
else()
	set(BELCARD_LIBRARIES_FOR_TOOLS belcard)
endif()

foreach(EXECTUABLE belcard-folder belcard-unfolder belcard-parser)
	set(SOURCE_FILES_CXX ${EXECTUABLE}.cpp)
	bc_apply_compile_flags(SOURCE_FILES_CXX STRICT_OPTIONS_CPP STRICT_OPTIONS_CXX)
	add_executable(${EXECTUABLE} ${SOURCE_FILES_CXX})
	set_target_properties(${EXECTUABLE} PROPERTIES LINKER_LANGUAGE CXX)
	target_link_libraries(${EXECTUABLE} PRIVATE belcard belr)

	if(NOT IOS)
		install(TARGETS ${EXECTUABLE}
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
			LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
			ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
			PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		)
	endif()
endforeach()
