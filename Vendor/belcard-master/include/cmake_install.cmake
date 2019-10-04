# Install script for directory: /Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/built")
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

if("${CMAKE_INSTALL_COMPONENT}" STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/belcard" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_addressing.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_communication.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_general.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_geographical.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_identification.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_params.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_property.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_security.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_calendar.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_explanatory.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_generic.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_organizational.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_parser.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_rfc6474.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/belcard_utils.hpp"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belcard-master/include/belcard/vcard_grammar.hpp"
    )
endif()

