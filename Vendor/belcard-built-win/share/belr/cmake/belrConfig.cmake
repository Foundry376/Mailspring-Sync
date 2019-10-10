############################################################################
# BelrConfig.cmake
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
#
# Config file for the belr package.
# It defines the following variables:
#
#  BELR_FOUND - system has belr
#  BELR_INCLUDE_DIRS - the belr include directory
#  BELR_LIBRARIES - The libraries needed to use belr
#  BELR_CPPFLAGS - The compilation flags needed to use belr


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was BelrConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

set(BELR_TARGETNAME belr)

include("${CMAKE_CURRENT_LIST_DIR}/${BELR_TARGETNAME}Targets.cmake")

if(NO)
	set(BELR_LIBRARIES ${BELR_TARGETNAME})
else()
	if(TARGET ${BELR_TARGETNAME})
		if(LINPHONE_BUILDER_GROUP_EXTERNAL_SOURCE_PATH_BUILDERS)
			set(BELR_LIBRARIES ${BELR_TARGETNAME})
		else()
			get_target_property(BELR_LIBRARIES ${BELR_TARGETNAME} LOCATION)
		endif()
		get_target_property(BELR_LINK_LIBRARIES ${BELR_TARGETNAME} INTERFACE_LINK_LIBRARIES)
		if(BELR_LINK_LIBRARIES)
			list(APPEND BELR_LIBRARIES ${BELR_LINK_LIBRARIES})
		endif()
	endif()
	get_target_property(BELR_INCLUDE_DIRS ${BELR_TARGETNAME} INTERFACE_INCLUDE_DIRECTORIES)
endif()

set(BELR_CPPFLAGS -DBCTBX_STATIC;-DBELR_STATIC)

set(BELR_FOUND 1)
