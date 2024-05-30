# SPDX-License-Identifier: BSD-3-Clause
# nfb-framework libnfb-extensions CMake build file
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz
#

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)

find_program(_PROTOBUF_PROTOC protoc)
find_library(_GRPC_LIB grpc)
if (EXISTS ${_PROTOBUF_PROTOC} AND EXISTS ${_GRPC_LIB})
    add_subdirectory(libnfb_ext_grpc)
else()
    message(STATUS "library grpc or binary protoc not found, gRPC features disabled")
endif()
