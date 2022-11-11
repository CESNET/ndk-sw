# SPDX-License-Identifier: BSD-3-Clause
#
# libnfb CMake build file
#
# Copyright (C) 2020-2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
project(libnfb LANGUAGES C)
include(GNUInstallDirs)
include(CheckSymbolExists)
include(CheckIncludeFile)
include(${CMAKE_CURRENT_LIST_DIR}/../functions.cmake)

find_package(PkgConfig)

nfb_cmake_env()
get_git_version()

set(CMAKE_REQUIRED_LIBRARIES "numa")

ndk_check_build_dependency(NUMA_LIBRARIES numa NUMA_INCLUDE_DIRS numa.h)
ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)

find_path(NFB_DRIVER_INCLUDE_DIRS
	NAMES nfb.h
	PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/kernel/include
	PATH_SUFFIXES linux/nfb
	NO_DEFAULT_PATH
)

if (NOT NFB_DRIVER_INCLUDE_DIRS)
	message(FATAL_ERROR "NFB driver headers not found, cannot continue")
endif ()

set(LIBNFB_SOURCES
	src/bus/mi.c src/nfb.c src/ndp/ndp.c src/info.c
	src/boot/mtd.c src/boot/boot.c src/boot/filetype_mcs.c src/boot/filetype_bit.c src/boot/filetype_rpd.c src/boot/bit_reverse_table.c
)

add_library(nfb SHARED ${LIBNFB_SOURCES})
add_library(nfb_static STATIC ${LIBNFB_SOURCES})
foreach(NFB IN ITEMS nfb nfb_static)
	target_link_libraries(${NFB}
		PRIVATE ${FDT_LIBRARIES} ${NUMA_LIBRARIES} dl
	)
	target_include_directories(${NFB}
		PUBLIC include
		PRIVATE src ${NFB_DRIVER_INCLUDE_DIRS}/../..
	)
	set_target_properties(${NFB} PROPERTIES
		OUTPUT_NAME nfb
		VERSION ${GIT_VERSION}
		SOVERSION ${GIT_VERSION_MAJOR}
	)
endforeach()

install(TARGETS nfb nfb_static
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/nfb include/netcope
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
