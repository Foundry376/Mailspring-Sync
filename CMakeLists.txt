cmake_minimum_required (VERSION 3.5 FATAL_ERROR)
project (mailsync) 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET (CMAKE_CXX_FLAGS   "-Wno-unknown-pragmas")

#Set Linker flags
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")


find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB REQUIRED glib-2.0)

include_directories (${GLIB_INCLUDE_DIRS})
link_directories (${GLIB_LIBRARY_DIRS})

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

  # Use pkg-config to find dependencies which is more robust on Arch/CachyOS
  pkg_check_modules(XML REQUIRED libxml-2.0)
  pkg_check_modules(LZMA REQUIRED liblzma)
  pkg_check_modules(ZLIB REQUIRED zlib)
  pkg_check_modules(UUID REQUIRED uuid)
  pkg_check_modules(ICU_I18N REQUIRED icu-i18n)
  pkg_check_modules(ICU_UC REQUIRED icu-uc)
  # ctemplate doesn't always have a .pc file, we'll find it manually if needed

  include_directories(${XML_INCLUDE_DIRS})
  include_directories(${LZMA_INCLUDE_DIRS})
  include_directories(${ZLIB_INCLUDE_DIRS})
  include_directories(${UUID_INCLUDE_DIRS})
  include_directories(${ICU_I18N_INCLUDE_DIRS})
  include_directories(${ICU_UC_INCLUDE_DIRS})

  set(additional_includes
    /usr/include/tidy
    /usr/include/libxml2
    /usr/include/curl
  )
  
  set(icu_libraries icudata icui18n icuio icutest icutu icuuc)
  set(linux_libraries ${icu_libraries} pthread uuid xml2)

  link_directories(/opt/openssl/lib)
  link_directories(Vendor/mailcore2/build/src)
  link_directories(Vendor/libetpan/src/.libs)
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
  target_link_libraries(mailsync ${XML_LIBRARIES})
  target_link_libraries(mailsync ${LZMA_LIBRARIES})
  target_link_libraries(mailsync ${ZLIB_LIBRARIES})
  target_link_libraries(mailsync ${UUID_LIBRARIES})
  target_link_libraries(mailsync ${ICU_I18N_LIBRARIES})
  target_link_libraries(mailsync ${ICU_UC_LIBRARIES})

  find_library(RESOLV_LIB NAMES libresolv.a libresolv)
  target_link_libraries(mailsync ${RESOLV_LIB})

  find_library(CTEMPLATE_LIB NAMES libctemplate.a libctemplate ctemplate)
  target_link_libraries(mailsync ${CTEMPLATE_LIB})

  find_library(CRYPTO_LIB NAMES libcrypto.a crypto)
  target_link_libraries(mailsync ${CRYPTO_LIB})
  
  find_library(SSL_LIB NAMES libssl.a ssl)
  target_link_libraries(mailsync ${SSL_LIB})

  pkg_check_modules(CARES REQUIRED libcares)
  target_link_libraries(mailsync ${CARES_LINK_LIBRARIES})

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
