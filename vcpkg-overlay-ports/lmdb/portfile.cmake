# Upstream vcpkg's lmdb port at our baseline, fetching from the official GitHub mirror
# instead of git.openldap.org, whose archive endpoint rate-limits CI with HTTP 429.
# LMDB_0.9.33 is the same tag object (d9bac16) and commit (3a29a24) on both hosts.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LMDB/lmdb
    REF "LMDB_${VERSION}"
    SHA512 5c769936372cf3c9ce3a555a19506e8bd0567f2f3fc8e2b199e0404904c34ad2baac273a21b547d2049d99873ab6319baafb34bd5dd4fe3c48129e993d774f64
    HEAD_REF mdb.master
    PATCHES
        getopt-win32.diff
)
file(COPY "${CMAKE_CURRENT_LIST_DIR}/cmake/" DESTINATION "${SOURCE_PATH}/libraries/liblmdb")

vcpkg_check_features(OUT_FEATURE_OPTIONS options_release
    FEATURES
        tools   LMDB_BUILD_TOOLS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/libraries/liblmdb"
    OPTIONS
        "-DLMDB_VERSION=${VERSION}"
    OPTIONS_RELEASE
        ${options_release}
    OPTIONS_DEBUG
        -DLMDB_INSTALL_HEADERS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-lmdb)

if(LMDB_BUILD_TOOLS)
    vcpkg_copy_tools(TOOL_NAMES mdb_copy mdb_dump mdb_load mdb_stat AUTO_CLEAN)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(COPY "${CURRENT_PORT_DIR}/lmdb-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/libraries/liblmdb/COPYRIGHT"
        "${SOURCE_PATH}/libraries/liblmdb/LICENSE"
)
