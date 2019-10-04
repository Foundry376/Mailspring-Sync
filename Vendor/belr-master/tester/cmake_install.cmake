# Install script for directory: /Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/belr_tester")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/belr_tester" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/belr_tester")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/built/Frameworks"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/belr_tester")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/belr_tester")
    endif()
  endif()
endif()

if("${CMAKE_INSTALL_COMPONENT}" STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/belr-tester/res" TYPE FILE FILES
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/basicgrammar.txt"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/vcardgrammar.txt"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/sipgrammar.txt"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/response.txt"
    "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/register.txt"
    )
endif()

if("${CMAKE_INSTALL_COMPONENT}" STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/belr/grammars" TYPE FILE FILES "/Users/bengotow/Work/F376/Projects/Mailspring/client/mailsync/Vendor/belr-master/tester/res/belr-grammar-example.blr")
endif()

