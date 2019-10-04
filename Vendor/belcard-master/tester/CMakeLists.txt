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
	set(BELCARD_LIBRARIES_FOR_TESTER belcard)
else()
	set(BELCARD_LIBRARIES_FOR_TESTER belcard)
endif()

set(VCARD_FILES
	vcards/foldtest.vcf
	vcards/unfoldtest.vcf
	vcards/vcard.vcf
	vcards/vcards.vcf
)

set(HEADER_FILES_CXX belcard-tester.hpp)
set(SOURCE_FILES_CXX
	belcard-addressing-tester.cpp
	belcard-calendar-tester.cpp
	belcard-communication-tester.cpp
	belcard-explanatory-tester.cpp
	belcard-general-tester.cpp
	belcard-geographical-tester.cpp
	belcard-identification-tester.cpp
	belcard-organizational-tester.cpp
	belcard-rfc6474-tester.cpp
	belcard-security-tester.cpp
	belcard-tester.cpp
	belcard-vcard-tester.cpp
)

bc_apply_compile_flags(SOURCE_FILES_CXX STRICT_OPTIONS_CPP STRICT_OPTIONS_CXX)

add_executable(belcard_tester ${SOURCE_FILES_CXX} ${HEADER_FILES_CXX})
set_target_properties(belcard_tester PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(belcard_tester PRIVATE ${BCTOOLBOX_INCLUDE_DIRS})
target_link_libraries(belcard_tester belcard bctoolbox bctoolbox-tester)

if(NOT IOS)
	install(TARGETS belcard_tester
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
	)
endif()
install(FILES ${VCARD_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/belcard_tester/vcards")
