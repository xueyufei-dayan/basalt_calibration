vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO IntelRealSense/librealsense
    REF v2.51.1
    SHA512 e03a0188f11634ba645f321f190758807670fcaeaa2695c1d1ddc4585e43c2b00ca42b4e820b07f5e0c1ee7c026347b3802d4a86717a3d0500c359c08494aa32
    HEAD_REF master
    PATCHES
        fix_openni2.patch
        fix_macos_arm64_flags.patch
)

# GCC 13 rejects the curly quotes (U+2018/U+2019) that librealsense embeds
# in the #if 0 block of src/libusb/libusb.h (and in bundled glfw sources).
# Replace them with ASCII quotes so the port compiles with modern toolchains.
foreach(_rs_fix_file
    "${SOURCE_PATH}/src/libusb/libusb.h"
    "${SOURCE_PATH}/third-party/glfw/deps/tinycthread.h"
    "${SOURCE_PATH}/third-party/glfw/src/wl_window.c"
    "${SOURCE_PATH}/third-party/glfw/src/wl_init.c")
  if(EXISTS "${_rs_fix_file}")
    file(READ "${_rs_fix_file}" _rs_fix_content)
    string(REPLACE "‘" "'" _rs_fix_content "${_rs_fix_content}")
    string(REPLACE "’" "'" _rs_fix_content "${_rs_fix_content}")
    file(WRITE "${_rs_fix_file}" "${_rs_fix_content}")
  endif()
endforeach()

file(COPY "${SOURCE_PATH}/src/win7/drivers/IntelRealSense_D400_series_win7.inf" DESTINATION "${SOURCE_PATH}")
string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" BUILD_CRT_LINKAGE)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        rs-usb-backend FORCE_RSUSB_BACKEND
        tm2            BUILD_WITH_TM2
        tools          BUILD_TOOLS
)

set(BUILD_OPENNI2_BINDINGS OFF)
if(("openni2" IN_LIST FEATURES) AND (VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic"))
  set(BUILD_OPENNI2_BINDINGS ON)
endif()

set(PLATFORM_OPTIONS)
if("rs-usb-backend" IN_LIST FEATURES AND (VCPKG_TARGET_IS_ANDROID OR VCPKG_TARGET_IS_LINUX OR VCPKG_TARGET_IS_OSX))
    list(APPEND PLATFORM_OPTIONS -DFORCE_RSUSB_BACKEND=ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
        -DENFORCE_METADATA=ON
        -DBUILD_WITH_OPENMP=OFF
        -DBUILD_UNIT_TESTS=OFF
        -DBUILD_WITH_STATIC_CRT=${BUILD_CRT_LINKAGE}
        -DBUILD_OPENNI2_BINDINGS=${BUILD_OPENNI2_BINDINGS}
        -DOPENNI2_DIR=${CURRENT_INSTALLED_DIR}/include/openni2
        ${PLATFORM_OPTIONS}
        -DBUILD_EXAMPLES=OFF
        -DBUILD_GRAPHICAL_EXAMPLES=OFF
    MAYBE_UNUSED_VARIABLES
        OPENNI2_DIR
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/realsense2)
if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/realsense2/realsense2Targets.cmake" "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel" "\${_IMPORT_PREFIX}")
endif()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if(BUILD_TOOLS)
    set(TOOL_NAMES rs-convert rs-embed rs-enumerate-devices rs-fw-logger rs-fw-update rs-record rs-terminal)
    vcpkg_copy_tools(TOOL_NAMES ${TOOL_NAMES} AUTO_CLEAN)
endif()

if(BUILD_OPENNI2_BINDINGS)
    file(GLOB RS2DRIVER "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/_out/rs2driver*")
    if(RS2DRIVER)
        file(COPY ${RS2DRIVER} DESTINATION "${CURRENT_PACKAGES_DIR}/tools/openni2/OpenNI2/Drivers")
    endif()
endif()

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
vcpkg_fixup_pkgconfig()
