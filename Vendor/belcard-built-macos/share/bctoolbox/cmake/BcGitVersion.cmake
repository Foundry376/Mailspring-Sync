############################################################################
# BcGitVersion.cmake
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

if(GIT_EXECUTABLE)
	macro(GIT_COMMAND OUTPUT_VAR)
		set(GIT_ARGS ${ARGN})
		execute_process(
			COMMAND ${GIT_EXECUTABLE} ${ARGN}
			WORKING_DIRECTORY ${WORK_DIR}
			OUTPUT_VARIABLE ${OUTPUT_VAR}
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
	endmacro()

	GIT_COMMAND(GIT_DESCRIBE describe --always)
	GIT_COMMAND(GIT_TAG describe --abbrev=0)
	GIT_COMMAND(GIT_REVISION rev-parse HEAD)
endif()

string(TOUPPER "${PROJECT_NAME}" PREFIX_GIT_VERSION)
string(REPLACE "-" "_" PREFIX_GIT_VERSION "${PREFIX_GIT_VERSION}")
if(GIT_DESCRIBE)
	if(NOT GIT_TAG STREQUAL PROJECT_VERSION)
		message(WARNING "Project version (${PROJECT_VERSION}) and git tag (${GIT_TAG}) differ. Please put them identical")
	endif()
	set(PROJECT_GIT_VERSION "${GIT_DESCRIBE}")
	configure_file("${TEMPLATE_DIR}/gitversion.h.in" "${OUTPUT_DIR}/gitversion.h" @ONLY)
elseif(GIT_REVISION)
	set(PROJECT_GIT_VERSION "${LINPHONE_VERSION}_${GIT_REVISION}")
	configure_file("${TEMPLATE_DIR}/gitversion.h.in" "${OUTPUT_DIR}/gitversion.h" @ONLY)
else()
	if(NOT EXISTS "${OUTPUT_DIR}/gitversion.h")
		execute_process(COMMAND ${CMAKE_COMMAND} -E touch "${OUTPUT_DIR}/gitversion.h")
	endif()
endif()
