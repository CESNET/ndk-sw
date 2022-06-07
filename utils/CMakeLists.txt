# SPDX-License-Identifier: BSD-3-Clause
#
# CMake build file for NFB utils
#
# Copyright (C) 2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
include(GNUInstallDirs)

set(UDEV_NAME 80-nfb.rules)

include(${CMAKE_CURRENT_LIST_DIR}/../functions.cmake)

get_git_version()

set(prefix          "/usr")
set(includedir      "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
set(libdir          "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(datarootdir     "\${prefix}/${CMAKE_INSTALL_DATAROOTDIR}")
set(datadir         "\${datarootdir}")
set(PACKAGE_VERSION "${GIT_VERSION}")

configure_file(nfb-framework.pc.in nfb-framework.pc @ONLY)
configure_file(netcope-common.pc.in netcope-common.pc @ONLY)

install(
	FILES nfb.udev.rules
	DESTINATION /etc/udev/rules.d
	RENAME ${UDEV_NAME}
)

install(
	FILES
		${CMAKE_CURRENT_BINARY_DIR}/nfb-framework.pc
		${CMAKE_CURRENT_BINARY_DIR}/netcope-common.pc
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

install(
	FILES dracut/nfb.conf
	DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dracut.conf.d
)
