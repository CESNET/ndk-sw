# SPDX-License-Identifier: BSD-3-Clause
#
# CMake build file for NFB python tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)

project(python-nfb-tools LANGUAGES C)

set(Python_FIND_UNVERSIONED_NAMES FIRST)
find_package(Python COMPONENTS Interpreter)

set(CMAKE_USE_RELATIVE_PATHS TRUE)

set(SETUP_PY_IN     "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(SETUP_PY        "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
set(SETUP_DEPS      "${CMAKE_CURRENT_SOURCE_DIR}/nfbmeter/__init__.py")
set(SETUP_OUTPUT    "${CMAKE_CURRENT_BINARY_DIR}/build-python")

configure_file(${SETUP_PY_IN} ${SETUP_PY})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/setup_timestamp
    COMMAND ${Python_EXECUTABLE} ARGS ${SETUP_PY} install --root ${SETUP_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/setup_timestamp
    DEPENDS ${SETUP_DEPS}
)

add_custom_target(python_nfb_tools ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/setup_timestamp)

install(
    DIRECTORY ${SETUP_OUTPUT}/
    DESTINATION "/" # FIXME may cause issues with other cpack generators
    COMPONENT pythontools
	USE_SOURCE_PERMISSIONS
)
