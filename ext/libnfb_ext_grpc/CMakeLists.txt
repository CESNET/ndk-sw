# SPDX-License-Identifier: BSD-3-Clause
#
# CMake build file for NFB debug tools
#
# Copyright (C) 2022 CESNET
# Author(s):
#   Martin Spinler <spinler@cesnet.cz>
#

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)
project(libnfb-ext-grpc C CXX)
include(GNUInstallDirs)

find_package(Protobuf CONFIG)
find_package(gRPC QUIET)

set(PROTO_NFB nfb/ext/protobuf/v1/nfb.proto)
set(PROTO_DMA nfb/ext/protobuf/v1/dma.proto)
set(PROTO_FILES ${PROTO_NFB} ${PROTO_DMA})

add_library(nfb-ext-proto INTERFACE ${PROTO_FILES})
add_library(nfb-ext-grpc SHARED client.cc client_dma_vas.cc)
add_executable(nfb-grpc-server server.cc)

if (${gRPC_FOUND})
	include_directories(${Protobuf_INCLUDE_DIRS})
	include_directories(${CMAKE_CURRENT_BINARY_DIR})

	target_sources(nfb-ext-grpc PRIVATE ${PROTO_NFB} ${PROTO_DMA})
	target_sources(nfb-grpc-server PRIVATE ${PROTO_NFB})

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

	# NFB proto
	get_filename_component(nfb_proto_abs ${PROTO_NFB} ABSOLUTE)
	get_filename_component(nfb_proto_path "${nfb_proto_abs}" PATH)

	set(nfb_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/nfb.pb.cc")
	set(nfb_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/nfb.pb.h")
	set(nfb_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/nfb.grpc.pb.cc")
	set(nfb_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/nfb.grpc.pb.h")

	add_custom_command(
		  OUTPUT "${nfb_proto_srcs}" "${nfb_proto_hdrs}" "${nfb_grpc_srcs}" "${nfb_grpc_hdrs}"
		  COMMAND ${_PROTOBUF_PROTOC}
		  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
			--cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
			-I "${nfb_proto_path}"
			--plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
			"${nfb_proto_abs}"
		  DEPENDS "${nfb_proto_abs}")

	include_directories("${CMAKE_CURRENT_BINARY_DIR}")

	# DMA proto
	get_filename_component(dma_proto_abs ${PROTO_DMA} ABSOLUTE)
	get_filename_component(dma_proto_path "${dma_proto_abs}" PATH)

	set(dma_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/dma.pb.cc")
	set(dma_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/dma.pb.h")
	set(dma_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/dma.grpc.pb.cc")
	set(dma_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/dma.grpc.pb.h")

	add_custom_command(
		  OUTPUT "${dma_proto_srcs}" "${dma_proto_hdrs}" "${dma_grpc_srcs}" "${dma_grpc_hdrs}"
		  COMMAND ${_PROTOBUF_PROTOC}
		  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
			--cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
			-I "${dma_proto_path}"
			--plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
			"${dma_proto_abs}"
		  DEPENDS "${dma_proto_abs}")


	set(NFB_PROTO_OUT_FILES
		${nfb_grpc_srcs} ${nfb_grpc_hdrs} ${nfb_proto_srcs} ${nfb_proto_hdrs}
	)
	set(DMA_PROTO_OUT_FILES
		${dma_grpc_srcs} ${dma_grpc_hdrs} ${dma_proto_srcs} ${dma_proto_hdrs}
	)

	set(gRPC_LIB_PREFIX "")
	set(Protobuf_LIBRARIES "protobuf")
endif()

target_sources(nfb-ext-grpc PRIVATE ${NFB_PROTO_OUT_FILES} ${DMA_PROTO_OUT_FILES})
target_sources(nfb-grpc-server PRIVATE ${NFB_PROTO_OUT_FILES})

target_link_libraries(nfb-ext-grpc ${Protobuf_LIBRARIES} ${gRPC_LIB_PREFIX}grpc++ nfb fdt)
target_link_libraries(nfb-grpc-server ${Protobuf_LIBRARIES} ${gRPC_LIB_PREFIX}grpc++ nfb fdt)

set_target_properties(nfb-ext-grpc PROPERTIES
	OUTPUT_NAME nfb-ext-grpc
	VERSION ${GIT_VERSION}
	SOVERSION ${GIT_VERSION_MAJOR}
)

set_target_properties(nfb-ext-proto
    PROPERTIES
	PUBLIC_HEADER "${PROTO_FILES}"
)

install(TARGETS nfb-ext-grpc
	COMPONENT NfbExtGrpc
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS nfb-ext-proto
	COMPONENT NfbExtGrpc
	PUBLIC_HEADER
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nfb/ext/protobuf/v1/
)

install(TARGETS nfb-grpc-server
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	COMPONENT NfbExtGrpc
)
