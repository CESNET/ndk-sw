# SPDX-License-Identifier: BSD-3-Clause
# ndk-sw tools-debug CMake build file
#
# Copyright (c) 2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
project(nfb-framework LANGUAGES C)
include(${CMAKE_CURRENT_LIST_DIR}/functions.cmake)

get_git_version()

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "Main")

#check_function_exists(cmake_host_system_information HAVE_HSI)
#cmake_host_system_information(RESULT DISTRIB_ID_LIKE QUERY DISTRIB_ID_LIKE)
if (EXISTS "/etc/debian_version")
	set(PLATFORM          "Debian")
	set(CPACK_GENERATOR   "DEB" )
elseif (EXISTS "/etc/centos-release" OR EXISTS "/etc/sl-release" OR EXISTS "/etc/redhat-release")
	set(PLATFORM          "Centos")
	set(CPACK_GENERATOR   "RPM")
else ()
	message(WARNING "Failed to determine platform, cannot build package")
	set(CPACK_GENERATOR   "Invalid" )
endif ()

set(PACKAGE_NAME "${PROJECT_NAME}")
set(PACKAGE_VERSION "${GIT_VERSION}")
set(RELEASE "${GIT_VERSION_RELEASE}")

add_subdirectory(drivers)
add_subdirectory(libnfb)
add_subdirectory(ext)
add_subdirectory(tools)
add_subdirectory(tools-debug)
add_subdirectory(tools-misc)
add_subdirectory(utils)
add_subdirectory(pynfb)

configure_file(package/preinst.in package/preinst @ONLY)
configure_file(package/postinst.in package/postinst @ONLY)
configure_file(package/prerm.in package/prerm @ONLY)
configure_file(package/postrm.in package/postrm @ONLY)

set(RPM_DEP_LIST
	dkms gcc kernel-devel libarchive libfdt-devel make numactl ncurses
)
set(RPM_PYTHON_DEP_LIST
	nfb-framework python3-fdt
)

set(DEB_DEP_LIST
	dkms gcc libarchive-dev libfdt-dev libncurses-dev libnuma-dev linux-headers make
)
set(DEB_PYTHON_DEP_LIST
	nfb-framework
)


# Common package settings
if(USE_DPDK)
	set(CPACK_PACKAGE_NAME                  "${PROJECT_NAME}+ndp_tools_dpdk")
else()
	set(CPACK_PACKAGE_NAME                  "${PROJECT_NAME}")
endif()
set(CPACK_PACKAGE_VENDOR                "CESNET")
set(CPACK_PACKAGE_VERSION_MAJOR         ${GIT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR         ${GIT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH         ${GIT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION               ${GIT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY   "NDK software library")
set(CPACK_PACKAGE_CONTACT               "spinler@cesnet.cz")
set(CPACK_PACKAGE_RELOCATABLE           OFF)
set(CPACK_PACKAGE_DESCRIPTION_FILE      "${CMAKE_CURRENT_LIST_DIR}/package/description.txt")

set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/")

# RPM (CentOS) package settings
set(CPACK_RPM_FILE_NAME                 RPM-DEFAULT)
set(CPACK_RPM_Main_PACKAGE_NAME         "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_DebugTools_PACKAGE_NAME   "${CPACK_PACKAGE_NAME}-debug")
# FIXME: use real Python version
set(CPACK_RPM_python_PACKAGE_NAME       "python3-nfb")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define __python python3")

set(CPACK_RPM_PACKAGE_RELEASE           ${GIT_VERSION_RELEASE})
set(CPACK_RPM_PACKAGE_RELEASE_DIST      ON)
set(CPACK_RPM_PACKAGE_LICENSE           "BSD and GPLv2")
string(JOIN " " CPACK_RPM_PACKAGE_REQUIRES ${RPM_DEP_LIST})
string(JOIN " " CPACK_RPM_python_PACKAGE_REQUIRES ${RPM_PYTHON_DEP_LIST})

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
	/etc/dracut.conf.d
	/etc/udev
	/etc/udev/rules.d
	/usr/include/linux
	/usr/lib/tmpfiles.d
	/usr/lib64/pkgconfig
	/usr/src
)
set(CPACK_RPM_COMPONENT_INSTALL          ON)
set(CPACK_RPM_CHANGELOG_FILE             "${CMAKE_CURRENT_LIST_DIR}/Changelog")
set(CPACK_RPM_Main_PACKAGE_OBSOLETES          "netcope-common")
set(CPACK_RPM_DebugTools_PACKAGE_OBSOLETES    "netcope-common-debug")
set(CPACK_RPM_Main_PACKAGE_PROVIDES           "netcope-common")
set(CPACK_RPM_DebugTools_PACKAGE_PROVIDES     "netcope-common-debug")
#set(CPACK_RPM_DebugTools_PACKAGE_REQUIRES "${PACKAGE_NAME} = ${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}")

if(USE_DPDK)
	set(CPACK_RPM_ndp-tool-dpdk_PACKAGE_NAME "ndp-tool-dpdk")
	set(CPACK_RPM_ndp-tool-dpdk_PACKAGE_REQUIRES "nfb-framework, dpdk-nfb, dpdk-nfb-tools")
	set (CPACK_RPM_SOURCE_PKG_BUILD_PARAMS "-DUSE_DPDK=true")
endif()

set(CPACK_RPM_Main_PRE_INSTALL_SCRIPT_FILE    "${CMAKE_CURRENT_BINARY_DIR}/package/preinst")
set(CPACK_RPM_Main_PRE_UNINSTALL_SCRIPT_FILE  "${CMAKE_CURRENT_BINARY_DIR}/package/prerm")
set(CPACK_RPM_Main_POST_INSTALL_SCRIPT_FILE   "${CMAKE_CURRENT_BINARY_DIR}/package/postinst")
set(CPACK_RPM_Main_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/package/postrm")

# DEB (Ubuntu) package settings
set(CPACK_DEB_COMPONENT_INSTALL         ON)
set(CPACK_DEBIAN_PACKAGE_NAME           "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION        "${CPACK_PACKAGE_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE        "${GIT_VERSION_DEBRELEASE}")
set(CPACK_DEBIAN_FILE_NAME              "${CPACK_DEBIAN_PACKAGE_NAME}-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.deb")

set(CPACK_DEBIAN_DEBUGTOOLS_PACKAGE_NAME "nfb-framework-debug")
set(CPACK_DEBIAN_DEBUGTOOLS_FILE_NAME "nfb-framework-debug-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.deb")
set(CPACK_DEBIAN_PYTHON_PACKAGE_NAME "python3-nfb")
set(CPACK_DEBIAN_PYTHON_FILE_NAME "python3-nfb-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.deb")
string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS ${DEB_DEP_LIST})
string(JOIN ", " CPACK_DEBIAN_PYTHON_PACKAGE_DEPENDS ${DEB_PYTHON_DEP_LIST})
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS      ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
	"${CMAKE_CURRENT_BINARY_DIR}/package/preinst"
	"${CMAKE_CURRENT_BINARY_DIR}/package/postinst"
	"${CMAKE_CURRENT_BINARY_DIR}/package/prerm"
)

include(CPack)
