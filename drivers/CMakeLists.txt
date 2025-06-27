# SPDX-License-Identifier: BSD-3-Clause
# ndk-sw driver CMake build file
#
# Copyright (c) 2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)
include(GNUInstallDirs)
include(${CMAKE_CURRENT_LIST_DIR}/../functions.cmake)
get_git_version()

if (NOT PROJECT_NAME)
	message(FATAL_ERROR "Project name not set")
endif ()

set(PACKAGE_NAME    nfb-dkms)
set(PACKAGE_VERSION ${GIT_VERSION})
set(RELEASE ${GIT_VERSION_RELEASE})

set(DKMS_DIR "/usr/src/${PACKAGE_NAME}-${PACKAGE_VERSION}-${RELEASE}")

execute_process(COMMAND uname -r
	RESULT_VARIABLE KERNEL_VERSION_RES
	OUTPUT_VARIABLE KERNEL_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(KDIR "/lib/modules/${KERNEL_VERSION}/build" CACHE PATH "linux kernel build path")

function(kernel_try_compile C_CONTENT RES CHECK_TEXT)
	message(CHECK_START "Checking for ${CHECK_TEXT}")
	set(TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/.drivers-test")
	file(MAKE_DIRECTORY "${TEST_DIR}")
	file(WRITE "${TEST_DIR}/Makefile"
"KDIR=${KDIR}
KBUILD=$(MAKE) -C $(KDIR) M=${TEST_DIR}
ccflags-y+=-Werror
obj-m := test.o
all:
	$(KBUILD) test.s
")

	file(WRITE "${TEST_DIR}/test.c" "${C_CONTENT}")
	execute_process(COMMAND make -C "${TEST_DIR}" RESULT_VARIABLE MAKE_RETVAL OUTPUT_QUIET)
	if (${MAKE_RETVAL})
		set(${RES} false PARENT_SCOPE)
		message(CHECK_FAIL "FAILED")
	else ()
		set(${RES} true PARENT_SCOPE)
		message(CHECK_PASS "OK")
	endif ()
	file(REMOVE_RECURSE "${TEST_DIR}")
endfunction()

kernel_try_compile("" KDIR_RES "kernel build dir usability")
if (NOT ${KDIR_RES})
	message(WARNING "Kernel build dir is unusable!")
endif ()

configure_file(${CMAKE_CURRENT_LIST_DIR}/Makefile.in ${CMAKE_CURRENT_LIST_DIR}/Makefile @ONLY)

configure_file(${CMAKE_CURRENT_LIST_DIR}/Makefile.in ${CMAKE_CURRENT_BINARY_DIR}/configured/Makefile @ONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/dkms.conf.in ${CMAKE_CURRENT_BINARY_DIR}/configured/dkms.conf @ONLY)

install(
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include/linux
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel
	DESTINATION ${DKMS_DIR}
)

install (
	FILES
		${CMAKE_CURRENT_SOURCE_DIR}/Makefile.conf
		${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
		${CMAKE_CURRENT_BINARY_DIR}/configured/Makefile
		${CMAKE_CURRENT_BINARY_DIR}/configured/dkms.conf
	DESTINATION ${DKMS_DIR}
)

install (
	PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/configure
	DESTINATION ${DKMS_DIR}
)

include(ExternalProject)
ExternalProject_Add(drivers
	SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
	CONFIGURE_COMMAND CMAKE_PACKCAGE_VERSION=${PACKAGE_VERSION}-${RELEASE} CMAKE_PACKAGE_NAME=${PACKAGE_NAME} autoreconf
	PREFIX ${CMAKE_CURRENT_SOURCE_DIR}
	BUILD_COMMAND true
	BUILD_IN_SOURCE 1
	INSTALL_COMMAND true
)
