# SPDX-License-Identifier: GPL-2.0
#
# CMake build file for NFB debug tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz>
#

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
project(libnfb-ext-grpc C CXX)
include(GNUInstallDirs)

find_package(Protobuf CONFIG)

set(PROTO_FILES nfb_grpc.proto)

add_library(nfb-ext-grpc SHARED nfb_grpc_client.cc)
add_executable(nfb-grpc-server nfb_grpc_server.cc)

if (EXISTS ${Protobuf_DIR})
	find_package(gRPC CONFIG)

	include_directories(${Protobuf_INCLUDE_DIRS})
	include_directories(${CMAKE_CURRENT_BINARY_DIR})

	target_sources(nfb-ext-grpc PRIVATE ${PROTO_FILES})
	target_sources(nfb-grpc-server PRIVATE ${PROTO_FILES})

	get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
	protobuf_generate(TARGET nfb-ext-grpc LANGUAGE cpp)
	protobuf_generate(TARGET nfb-ext-grpc LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

	protobuf_generate(TARGET nfb-grpc-server LANGUAGE cpp)
	protobuf_generate(TARGET nfb-grpc-server LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

	set(gRPC_LIB_PREFIX "gRPC::")
else()
	set(protobuf_MODULE_COMPATIBLE TRUE)

	find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
	find_program(_PROTOBUF_PROTOC protoc)

	get_filename_component(nfb_proto ${PROTO_FILES} ABSOLUTE)
	get_filename_component(nfb_proto_path "${nfb_proto}" PATH)

	set(nfb_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/nfb_grpc.pb.cc")
	set(nfb_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/nfb_grpc.pb.h")
	set(nfb_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/nfb_grpc.grpc.pb.cc")
	set(nfb_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/nfb_grpc.grpc.pb.h")

	add_custom_command(
		  OUTPUT "${nfb_proto_srcs}" "${nfb_proto_hdrs}" "${nfb_grpc_srcs}" "${nfb_grpc_hdrs}"
		  COMMAND ${_PROTOBUF_PROTOC}
		  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
			--cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
			-I "${nfb_proto_path}"
			--plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
			"${nfb_proto}"
		  DEPENDS "${nfb_proto}")

	include_directories("${CMAKE_CURRENT_BINARY_DIR}")

	set(PROTO_OUT_FILES ${nfb_grpc_srcs} ${nfb_grpc_hdrs} ${nfb_proto_srcs} ${nfb_proto_hdrs})
	set(gRPC_LIB_PREFIX "")
	set(Protobuf_LIBRARIES "protobuf")
endif()

target_sources(nfb-ext-grpc PRIVATE ${PROTO_OUT_FILES})
target_sources(nfb-grpc-server PRIVATE ${PROTO_OUT_FILES})

target_link_libraries(nfb-ext-grpc ${Protobuf_LIBRARIES} ${gRPC_LIB_PREFIX}grpc++ nfb fdt)
target_link_libraries(nfb-grpc-server ${Protobuf_LIBRARIES} ${gRPC_LIB_PREFIX}grpc++ nfb)

set_target_properties(nfb-ext-grpc PROPERTIES
	OUTPUT_NAME nfb-ext-grpc
	VERSION ${GIT_VERSION}
	SOVERSION ${GIT_VERSION_MAJOR}
	PUBLIC_HEADER ${PROTO_FILES}
)

install(TARGETS nfb-ext-grpc
	COMPONENT NfbExtGrpc
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nfb/ext/protobuf/
)

install(TARGETS nfb-grpc-server
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	COMPONENT NfbExtGrpc
)
