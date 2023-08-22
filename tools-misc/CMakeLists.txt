# SPDX-License-Identifier: GPL-2.0
#
# CMake build file for NFB miscellaneous tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
project(ndk-tools LANGUAGES C)
include(GNUInstallDirs)
include(${CMAKE_CURRENT_LIST_DIR}/../functions.cmake)

nfb_cmake_env()

find_package(Threads)
find_package(PkgConfig)

pkg_check_modules(CONFIG REQUIRED libconfig)
ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)

link_libraries(nfb ${FDT_LIBRARIES})

add_executable(nfb-flash-ident nfb-flash-ident/flashident.c)
target_link_libraries(nfb-flash-ident ${CONFIG_LIBRARIES})

add_executable(nfb-flash-read nfb-flash-read/flashread.c)

foreach(NFB IN ITEMS nfb-flash-read nfb-flash-ident)
	target_include_directories(${NFB}
		PUBLIC include
		PRIVATE src ${NFB_DRIVER_INCLUDE_DIRS}/../..
	)
endforeach()

add_executable(nfb-spi-max nfb-spi-max/max-spi.c)

set (PIE_TARGETS nfb-flash-ident nfb-flash-read nfb-spi-max)
foreach(PIE_TARGET IN LISTS PIE_TARGETS)
	set_property(TARGET ${PIE_TARGET} PROPERTY POSITION_INDEPENDENT_CODE TRUE)
endforeach()
