cmake_minimum_required (VERSION 3.1 FATAL_ERROR)
project (mailsync) 

SET (CMAKE_CXX_FLAGS   "-Wno-unknown-pragmas")

#Set Linker flags
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")


find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB REQUIRED glib-2.0)

include_directories (${GLIB_INCLUDE_DIRS})
link_directories (${GLIB_LIBRARY_DIRS})

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

  # Detect Debian/Ubuntu multiarch library path (e.g. /usr/lib/aarch64-linux-gnu)
  # so that find_library() works on ARM64 and other non-x86 architectures
  execute_process(
    COMMAND dpkg-architecture -qDEB_HOST_MULTIARCH
    OUTPUT_VARIABLE DEB_HOST_MULTIARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(DEB_HOST_MULTIARCH)
    list(APPEND CMAKE_LIBRARY_PATH /usr/lib/${DEB_HOST_MULTIARCH})
  endif()

  set(additional_includes
    /usr/include/tidy
    /usr/include/libxml2
    /usr/include/curl
  )
  
  set(icu_libraries icudata icui18n icuio icutest icutu icuuc)
  set(linux_libraries ${icu_libraries} pthread uuid xml2)

  link_directories(/opt/openssl/lib)
  link_directories(Vendor/mailcore2/build/src)
  include_directories(Vendor/mailcore2/build/src/include)

  include_directories(Vendor)
  include_directories(Vendor/nlohmann)
  include_directories(Vendor/icalendarlib)
  include_directories(Vendor/SQLiteCpp/include Vendor/SQLiteCpp/sqlite3)
  include_directories(Vendor/StanfordCPPLib)
  include_directories(MailSync MailSync/Models)

  file(GLOB SOURCES
    MailSync/*.h
    MailSync/*.[ch]pp
    MailSync/Models/*.h
    MailSync/Models/*.[ch]pp
    Vendor/SQLiteCpp/sqlite3/*.[ch]
    Vendor/SQLiteCpp/src/*.[ch]pp
    Vendor/icalendarlib/*.h
    Vendor/icalendarlib/*.cpp
    Vendor/StanfordCPPLib/*.[ch]pp
    Vendor/StanfordCPPLib/stacktrace/*.[ch]pp
  )

  # ... existing mailsync executable ...
  add_executable(mailsync MailSync/main.cpp ${SOURCES})

  add_definitions(-DSQLITE_ENABLE_FTS5=1 -DHAVE_USLEEP=1 -DSQLITE_OMIT_LOAD_EXTENSION=1)

  target_link_libraries(mailsync libMailCore.a)
  target_link_libraries(mailsync ${GLIB_LIBRARIES})
  target_link_libraries(mailsync libetpan.a)

  find_library(XML_LIB NAMES libxml2.a libxml2)
  target_link_libraries(mailsync ${XML_LIB})
  target_include_directories(mailsync PRIVATE /usr/include/libxml2)

  find_library(LZMA_LIB NAMES liblzma.a liblzma)
  target_link_libraries(mailsync ${LZMA_LIB})

  find_library(Z_LIB NAMES libz.a libz)
  target_link_libraries(mailsync ${Z_LIB})

  find_library(RESOLV_LIB NAMES libresolv.a libresolv)
  target_link_libraries(mailsync ${RESOLV_LIB})

  find_library(CTEMPLATE_LIB NAMES libctemplate.a libctemplate)
  target_link_libraries(mailsync ${CTEMPLATE_LIB})

  find_library(UUID_LIB NAMES libuuid.a libuuid)
  target_link_libraries(mailsync ${UUID_LIB})

  find_library(ICUI18N_LIB NAMES libicui18n.a libicui18n)
  target_link_libraries(mailsync ${ICUI18N_LIB})

  find_library(ICUUC_LIB NAMES libicuuc.a libicuuc)
  target_link_libraries(mailsync ${ICUUC_LIB})

  find_library(ICUDATA_LIB NAMES libicudata.a libicudata)
  target_link_libraries(mailsync ${ICUDATA_LIB})

  pkg_check_modules(CARES REQUIRED libcares)
  target_link_libraries(mailsync ${CARES_LINK_LIBRARIES})

  # Batch 1: GTest Setup
  find_library(GTEST_LIB NAMES gtest PATHS /usr/lib/x86_64-linux-gnu)
  find_library(GMOCK_LIB NAMES gmock PATHS /usr/lib/x86_64-linux-gnu)

  if(GTEST_LIB AND GMOCK_LIB)
    message(STATUS "Found GTest: ${GTEST_LIB}")
    message(STATUS "Found GMock: ${GMOCK_LIB}")

    set(TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MailSync/main.cpp")

    add_executable(mailsync_tests tests/Smoke_test.cpp tests/TaskProcessor_test.cpp ${TEST_SOURCES})

    set(MAILCORE_LIB "${CMAKE_SOURCE_DIR}/Vendor/mailcore2/build/src/libMailCore.a")
    set(LIBETPAN_STATIC "${CMAKE_SOURCE_DIR}/Vendor/libetpan/src/.libs/libetpan.a")

    target_link_libraries(mailsync_tests
        -Wl,--start-group
        ${MAILCORE_LIB}
        ${LIBETPAN_STATIC}
        -Wl,--end-group
        ${GLIB_LIBRARIES}
    )
    target_link_libraries(mailsync_tests ${XML_LIB} ${LZMA_LIB} ${Z_LIB} ${RESOLV_LIB} ${UUID_LIB} ${ICUI18N_LIB} ${ICUUC_LIB} ${ICUDATA_LIB} ${CARES_LINK_LIBRARIES})
    target_link_libraries(mailsync_tests pthread sasl2 ssl crypto curl dl lockfile)
    target_link_libraries(mailsync_tests ${GTEST_LIB} ${GMOCK_LIB})

    target_include_directories(mailsync_tests PRIVATE /usr/include/libxml2)
    target_include_directories(mailsync_tests PRIVATE "${CMAKE_SOURCE_DIR}/Vendor/libetpan/include")

    set_target_properties(mailsync_tests PROPERTIES
      BUILD_RPATH "\$ORIGIN;\$ORIGIN/..;${LIBETPAN_VENDOR_LIB_DIR};${CMAKE_SOURCE_DIR}/Vendor/mailcore2/build/src;/opt/openssl/lib"
    )
  endif()

  #find_library(SASL2_LIB NAMES libsasl2.a libsasl2)
  #target_link_libraries(mailsync ${SASL2_LIB})

  #find_library(SASLEXTRA_LIB NAMES libgssapi.a libgssapi)
  #target_link_libraries(mailsync ${SASLEXTRA_LIB})

  #find_library(DL_LIB NAMES libdl.a libdl)
  #target_link_libraries(mailsync ${DL_LIB})

  #find_library(RT_LIB NAMES librt.a librt)
  #target_link_libraries(mailsync ${RT_LIB})

  # These dynamic links are the packages that must be on the host machine
  # Note: libtidy is loaded dynamically at runtime via dlopen() to support
  # different sonames across Linux distributions (Debian/Ubuntu use
  # libtidy.so.5deb1, Fedora uses libtidy.so.5, etc.)
  target_link_libraries(mailsync pthread sasl2 ssl crypto curl dl)

ENDIF()
