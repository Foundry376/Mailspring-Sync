cmake_minimum_required (VERSION 3.1 FATAL_ERROR)
project (mailsync) 

SET (CMAKE_CXX_FLAGS       "-std=c++1z -Wno-unknown-pragmas")

find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB REQUIRED glib-2.0)

include_directories (${GLIB_INCLUDE_DIRS})
link_directories (${GLIB_LIBRARY_DIRS})

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

  set(additional_includes
    /usr/include/tidy
    /usr/include/libxml2
    /usr/include/curl
  )
  
  set(icu_libraries icudata icui18n icuio icule iculx icutest icutu icuuc)
  set(linux_libraries ${icu_libraries} pthread uuid)

  link_directories(Vendor/mailcore2/build/src)
  include_directories(Vendor/mailcore2/build/src/include)

  include_directories(Vendor)
  include_directories(Vendor/nlohmann)
  include_directories(Vendor/SQLiteCpp/include Vendor/SQLiteCpp/sqlite3)
  include_directories(MailSync MailSync/Models)

  file(GLOB SOURCES
    MailSync/*.h
    MailSync/*.[ch]pp
    MailSync/Models/*.h
    MailSync/Models/*.[ch]pp
    Vendor/SQLiteCpp/sqlite3/*.[ch]
    Vendor/SQLiteCpp/src/*.[ch]pp
  )

  add_executable(mailsync MailSync/main.cpp ${SOURCES})

  target_link_libraries(mailsync libMailCore.a ${GLIB_LIBRARIES} etpan z xml2 sasl2 tidy resolv curl ctemplate icuuc ssl crypto pthread uuid icui18n icuuc icudata dl)

ENDIF()
