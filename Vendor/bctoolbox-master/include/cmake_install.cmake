# Install script for directory: /home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/tmp/mailsync-build-deps/belcard")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

if(NOT CMAKE_INSTALL_COMPONENT OR "${CMAKE_INSTALL_COMPONENT}" STREQUAL "Unspecified")
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/bctoolbox" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/charconv.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/compiler.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/defs.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/exception.hh"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/list.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/logging.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/map.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/parser.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/port.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/regex.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/vconnect.h"
    "/home/bengotow/Mailspring/mailsync/Vendor/bctoolbox-master/include/bctoolbox/vfs.h"
    )
endif()

