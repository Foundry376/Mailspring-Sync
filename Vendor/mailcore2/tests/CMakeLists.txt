include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/../src/include
    ${GLIB2_INCLUDE_DIRS}
)

link_directories(
    ${additional_lib_searchpath}
)

IF(APPLE)
    set(test_mac_files test-all-mac.mm)
    set(mac_libraries iconv)
ENDIF()

set(dynamic_tidy_sources)
set(dynamic_tidy_libraries)
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(dynamic_tidy_sources ../../../MailSync/MailspringDynamicTidy.cpp)
    set(dynamic_tidy_libraries dl)
ENDIF()

add_executable (tests main.cpp test-all.cpp ${test_mac_files} ${dynamic_tidy_sources})

set(CTEMPLATE_LINK_LIB)
if(CTEMPLATE_LIBRARY)
    set(CTEMPLATE_LINK_LIB ${CTEMPLATE_LIBRARY})
endif()

target_link_libraries (
    tests MailCore
    ${ZLIB_LIBRARY} ${LIBETPAN_LIBRARY} ${LIBXML_LIBRARY} ${UCHARDET_LIBRARY} sasl2
    ${TIDY_LIBRARY} ${CTEMPLATE_LINK_LIB} ssl crypto ${linux_libraries} ${mac_libraries}
    ${dynamic_tidy_libraries}
    ${GLIB2_LIBRARIES} ${FOUNDATIONFRAMEWORK} ${SECURITYFRAMEWORK} ${CORESERVICESFRAMEWORK}
)
