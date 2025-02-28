# SPDX-License-Identifier: BSD-3-Clause
#
# CMake build file for NFB debug tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)
include(GNUInstallDirs)
include(CheckCCompilerFlag)

find_package(PkgConfig)

ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)

pkg_check_modules(LIBPCI libpci>=3.6.3)

check_c_compiler_flag("-mavx2" CONFIG_HAVE_MAVX2)
if (${CONFIG_HAVE_MAVX2})
	add_compile_definitions(CONFIG_HAVE_MAVX2)
endif()

link_libraries(nfb ${FDT_LIBRARIES})

add_executable(nfb-busdebugctl busdebugctl/busdebugctl.c)
add_executable(nfb-busdump busdump/busdump.c)
add_executable(nfb-busreplay busreplay/busreplay.c)
add_executable(nfb-xvc nfb-xvc/xvc_pcie.c nfb-xvc/xvcserver.c)
add_executable(nfb-mi-test nfb-mi-test/nfb-mi-test.c)


if (${CONFIG_HAVE_MAVX2})
	set_source_files_properties(nfb-mi-test/nfb-mi-test.c PROPERTIES COMPILE_FLAGS -mavx2)
endif()


set (PIE_TARGETS nfb-busdebugctl nfb-busdump nfb-busreplay nfb-xvc nfb-mi-test)

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
