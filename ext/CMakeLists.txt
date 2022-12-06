# SPDX-License-Identifier: BSD-3-Clause
# nfb-framework libnfb-extensions CMake build file
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

OPTION (LIB_GRPC "Add library for grpc" OFF)
if (LIB_GRPC)
    add_subdirectory(libnfb_ext_grpc)
endif()
