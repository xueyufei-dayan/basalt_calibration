# NOTE: switched from vcpkg_from_git to vcpkg_from_github (archive download)
# to support environments where github.com git access is blocked.
vcpkg_from_github(
    OUT_SOURCE_PATH ROS_COMM_SOURCE
    REPO ros/ros_comm
    REF 194c776737134a429619f6851f1850734843e210
    SHA512 a73e677b3051403325824f5a4ea8ad34a907ea03c19aa3bcf35ccb40de46e1aa393d076e971394eb799066c842e23f010762478f65b8e2ab86032dd6c7610f43
)

vcpkg_from_github(
    OUT_SOURCE_PATH ROSCPP_CORE_SOURCE
    REPO NikolausDemmel/roscpp_core
    REF 1a74781ce707b1a2198d1bd6799b892b3c1fa195
    SHA512 eac5a163b7c721bf90ad75157c6debc9cfaf506947806d95c4425f28daa12d4bed8af803d9015e3923c16f72e1915dafbc4a3280ec6972bb1e622e31c3b964f9
)

set(PORT_SOURCE "${CURRENT_BUILDTREES_DIR}/src/rosbag-src")
file(MAKE_DIRECTORY "${PORT_SOURCE}")

configure_file("${CURRENT_PORT_DIR}/cmake/CMakeLists.txt.in" "${PORT_SOURCE}/CMakeLists.txt" @ONLY)
configure_file("${CURRENT_PORT_DIR}/cmake/rosbagConfig.cmake.in" "${PORT_SOURCE}/rosbagConfig.cmake.in" COPYONLY)

vcpkg_cmake_configure(
    SOURCE_PATH "${PORT_SOURCE}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/rosbag)

vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "ROS sources (BSD-style licenses) bundled for Basalt compatibility.\n")
