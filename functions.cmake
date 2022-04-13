# SPDX-License-Identifier: BSD-3-Clause
# Common CMake functions file
#
# Copyright (c) 2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz

function(ndk_check_build_dependency)
	if (DEFINED "${ARGV0}")
		return()
	endif()

	message(CHECK_START "Checking for ${ARGV4}")
	find_library(${ARGV0} ${ARGV1})
	find_path(${ARGV2} ${ARGV3})

	if (NOT ${ARGV0} OR NOT ${ARGV2})
		message(SEND_ERROR "${ARGV4} not found")
	else ()
		message(CHECK_PASS "found")
	endif ()
endfunction()

function(get_git_version)
	execute_process(
		COMMAND git describe --tags --dirty
		RESULT_VARIABLE GIT_RETVAL
		OUTPUT_VARIABLE GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	if (GIT_RETVAL)
		message(WARNING "Cannot determine version from Git, trying .gitversion")
		file(READ "${CMAKE_CURRENT_LIST_DIR}/.gitversion" GIT_VERSION)
	endif (GIT_RETVAL)

	if (NOT GIT_VERSION)
		message(FATAL_ERROR "Cannot determine version.")
	endif (NOT GIT_VERSION)

	file(WRITE "${CMAKE_CURRENT_LIST_DIR}/.gitversion" ${GIT_VERSION})

	string(REGEX REPLACE "^v" "" GIT_VERSION_STRIPPED "${GIT_VERSION}")
	string(REGEX MATCHALL "[0-9]+" VERSION_COMPONENTS "${GIT_VERSION_STRIPPED}")
	string(REGEX REPLACE "^v[0-9]+[.][0-9]+[.][0-9]+-?" "" GIT_RELEASE "${GIT_VERSION}")

	list(POP_FRONT VERSION_COMPONENTS VERSION_MAJOR VERSION_MINOR VERSION_PATCH)
	set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

	if (GIT_RELEASE)
		string(REGEX REPLACE "-" "_" CENTOS_RELEASE "${GIT_RELEASE}")
		string(REGEX REPLACE "-" "~" DEBIAN_RELEASE "${GIT_RELEASE}")
	else (GIT_RELEASE)
		set(CENTOS_RELEASE 1)
		set(DEBIAN_RELEASE 1)
	endif (GIT_RELEASE)

	set(GIT_VERSION_MAJOR      ${VERSION_MAJOR}      PARENT_SCOPE)
	set(GIT_VERSION_MINOR      ${VERSION_MINOR}      PARENT_SCOPE)
	set(GIT_VERSION_PATCH      ${VERSION_PATCH}      PARENT_SCOPE)
	set(GIT_VERSION_RELEASE    ${CENTOS_RELEASE}     PARENT_SCOPE)
	set(GIT_VERSION_DEBRELEASE ${DEBIAN_RELEASE}     PARENT_SCOPE)

	set(GIT_VERSION            ${VERSION}            PARENT_SCOPE)
	set(GIT_VERSION_FULL       ${VERSION}-${RELEASE} PARENT_SCOPE)
endfunction ()

# \brief Setup environment for NFB
function (nfb_cmake_env)
	set(CMAKE_C_FLAGS "-std=gnu99 -pedantic -Wall -Wextra" PARENT_SCOPE)
endfunction ()
