cmake_minimum_required (VERSION 3.5 FATAL_ERROR)
project (mailsync)

SET (CMAKE_CXX_FLAGS   "-Wno-unknown-pragmas")

option(MAILSYNC_BUILD_TESTS "Build and run tests" OFF)
option(MAILSYNC_TESTS_ONLY "Only build tests, skip mailsync executable" OFF)

#Set Linker flags
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")


find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB REQUIRED glib-2.0)

include_directories (${GLIB_INCLUDE_DIRS})
link_directories (${GLIB_LIBRARY_DIRS})

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

  # Test-only build configuration
  if(MAILSYNC_BUILD_TESTS)
    enable_testing()

    # Find or fetch Google Test
    find_package(GTest QUIET)
    if(NOT GTEST_FOUND)
      include(FetchContent)
      FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
      )
      set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
      FetchContent_MakeAvailable(googletest)
    endif()

    # SQLiteCpp test sources
    set(SQLITECPP_TESTS
      Vendor/SQLiteCpp/tests/Column_test.cpp
      Vendor/SQLiteCpp/tests/Database_test.cpp
      Vendor/SQLiteCpp/tests/Savepoint_test.cpp
      Vendor/SQLiteCpp/tests/Statement_test.cpp
      Vendor/SQLiteCpp/tests/Backup_test.cpp
      Vendor/SQLiteCpp/tests/Transaction_test.cpp
      Vendor/SQLiteCpp/tests/VariadicBind_test.cpp
      Vendor/SQLiteCpp/tests/Exception_test.cpp
      Vendor/SQLiteCpp/tests/ExecuteMany_test.cpp
    )

    # Build a static library for SQLiteCpp to link tests against
    add_library(SQLiteCpp_lib STATIC
      Vendor/SQLiteCpp/src/Backup.cpp
      Vendor/SQLiteCpp/src/Column.cpp
      Vendor/SQLiteCpp/src/Database.cpp
      Vendor/SQLiteCpp/src/Exception.cpp
      Vendor/SQLiteCpp/src/Savepoint.cpp
      Vendor/SQLiteCpp/src/Statement.cpp
      Vendor/SQLiteCpp/src/Transaction.cpp
      Vendor/SQLiteCpp/sqlite3/sqlite3.c
    )
    target_include_directories(SQLiteCpp_lib PUBLIC
      Vendor/SQLiteCpp/include
      Vendor/SQLiteCpp/sqlite3
    )
    target_compile_definitions(SQLiteCpp_lib PUBLIC
      SQLITE_ENABLE_COLUMN_METADATA
      SQLITE_ENABLE_FTS5=1
    )
    set_target_properties(SQLiteCpp_lib PROPERTIES COMPILE_FLAGS "-fPIC")

    # Build the test executable
    add_executable(mailsync_tests ${SQLITECPP_TESTS})
    target_include_directories(mailsync_tests PRIVATE
      Vendor/SQLiteCpp/include
      Vendor/SQLiteCpp/sqlite3
    )
    target_link_libraries(mailsync_tests SQLiteCpp_lib)
    if(GTEST_FOUND)
      target_link_libraries(mailsync_tests GTest::GTest GTest::Main)
    else()
      target_link_libraries(mailsync_tests gtest_main)
    endif()
    target_link_libraries(mailsync_tests pthread dl)

    # Register tests with CTest
    add_test(NAME SQLiteCppTests COMMAND mailsync_tests)
  endif()

  # Skip mailsync build if only building tests
  if(NOT MAILSYNC_TESTS_ONLY)

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

    find_library(C_ARES_LIB NAMES libcares.a libcares)
    target_link_libraries(mailsync ${C_ARES_LIB})

    #find_library(SASL2_LIB NAMES libsasl2.a libsasl2)
    #target_link_libraries(mailsync ${SASL2_LIB})

    #find_library(SASLEXTRA_LIB NAMES libgssapi.a libgssapi)
    #target_link_libraries(mailsync ${SASLEXTRA_LIB})

    #find_library(DL_LIB NAMES libdl.a libdl)
    #target_link_libraries(mailsync ${DL_LIB})

    #find_library(RT_LIB NAMES librt.a librt)
    #target_link_libraries(mailsync ${RT_LIB})

    # These dynamic links are the packages that must be on the host machine
    target_link_libraries(mailsync pthread sasl2 ssl crypto tidy curl dl)

  endif()

ENDIF()
