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

find_package(PkgConfig)

ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)

pkg_check_modules(LIBPCI libpci>=3.6.3)

link_libraries(nfb ${FDT_LIBRARIES})

add_executable(nfb-busdebugctl busdebugctl/busdebugctl.c)
add_executable(nfb-busdump busdump/busdump.c)
add_executable(nfb-busreplay busreplay/busreplay.c)
add_executable(nfb-xvc nfb-xvc/xvc_pcie.c nfb-xvc/xvcserver.c)

set (PIE_TARGETS nfb-busdebugctl nfb-busdump nfb-busreplay nfb-xvc)

if (${LIBPCI_FOUND})
	target_compile_definitions(nfb-xvc PUBLIC USE_LIBPCI)
	target_link_libraries(nfb-xvc PRIVATE pci)
endif()

foreach(PIE_TARGET IN LISTS PIE_TARGETS)
	set_property(TARGET ${PIE_TARGET} PROPERTY POSITION_INDEPENDENT_CODE TRUE)
endforeach()

install(
	TARGETS ${PIE_TARGETS}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	COMPONENT DebugTools
)
