# SPDX-License-Identifier: GPL-2.0
#
# CMake build file for NFB debug tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
include(GNUInstallDirs)

ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)

link_libraries(nfb ${FDT_LIBRARIES})

add_executable(nfb-busdebugctl busdebugctl/busdebugctl.c)
add_executable(nfb-busdump busdump/busdump.c)
add_executable(nfb-busreplay busreplay/busreplay.c)

add_executable(nfb-xvc nfb-xvc/xvc_pcie.c nfb-xvc/xvcserver.c)

install(
	TARGETS nfb-busdebugctl nfb-busdump nfb-busreplay nfb-xvc
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	COMPONENT DebugTools
)
