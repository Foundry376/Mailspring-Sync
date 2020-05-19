cmake_minimum_required (VERSION 3.1 FATAL_ERROR)
project (mailsync) 

SET (CMAKE_C_COMPILER "/usr/bin/gcc-5")
SET (CMAKE_C_FLAGS     "-std=c++1z")

SET (CMAKE_CXX_COMPILER "/usr/bin/g++-5")
SET (CMAKE_CXX_FLAGS   "-std=c++1z -Wno-unknown-pragmas")

#Set Linker flags
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")


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

  find_library(TIDY_LIB NAMES libtidy.a libtidy)
  target_link_libraries(mailsync ${TIDY_LIB})

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

  target_link_libraries(mailsync /tmp/mailsync-build-deps-v2/curl-7.70.0/lib/.libs/libcurl.a)    

  find_library(C_ARES_LIB NAMES libcares.a libcares)
  target_link_libraries(mailsync ${C_ARES_LIB})

  #find_library(SASL2_LIB NAMES libsasl2.a libsasl2)
  #target_link_libraries(mailsync ${SASL2_LIB})

  find_library(SSL_LIB NAMES libssl.a libssl PATHS /opt/openssl/lib NO_DEFAULT_PATH)
  target_link_libraries(mailsync ${SSL_LIB})

  find_library(CRYPTO_LIB NAMES libcrypto.a libcrypto PATHS /opt/openssl/lib NO_DEFAULT_PATH)
  target_link_libraries(mailsync ${CRYPTO_LIB})

  #find_library(SASLEXTRA_LIB NAMES libgssapi.a libgssapi)
  #target_link_libraries(mailsync ${SASLEXTRA_LIB})

  #find_library(DL_LIB NAMES libdl.a libdl)
  #target_link_libraries(mailsync ${DL_LIB})

  #find_library(RT_LIB NAMES librt.a librt)
  #target_link_libraries(mailsync ${RT_LIB})

  #target_link_libraries(mailsync pthread sasl2 ssl crypto dl)
  target_link_libraries(mailsync pthread sasl2 dl)

ENDIF()
