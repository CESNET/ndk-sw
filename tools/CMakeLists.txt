# SPDX-License-Identifier: GPL-2.0
#
# CMake build file for NFB tools
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

set(CMAKE_THREAD_PREFER_PTHREAD TRUE CACHE BOOL "Prefer 'pthread' implementation of threads")
set(THREADS_PREFER_PTHREAD_FLAG TRUE CACHE BOOL "Prefer using the -pthread compiler flag over -lpthread")

find_package(Threads)
find_package(PkgConfig)

pkg_check_modules(NCURSES REQUIRED ncurses)
pkg_check_modules(ARCHIVE REQUIRED libarchive)
ndk_check_build_dependency(FDT_LIBRARIES fdt FDT_INCLUDE_DIRS libfdt.h libfdt)
ndk_check_build_dependency(NUMA_LIBRARIES numa NUMA_INCLUDE_DIRS numa.h)

link_libraries(nfb ${FDT_LIBRARIES})
include_directories(${CMAKE_CURRENT_LIST_DIR}/../drivers/kernel/include)

add_executable(nfb-info info/info.c)
target_compile_definitions(nfb-info PRIVATE PACKAGE_VERSION="${PACKAGE_VERSION}-${GIT_VERSION_RELEASE}")

add_executable(nfb-boot boot/boot.c)
target_link_libraries(nfb-boot PRIVATE ${ARCHIVE_LIBRARIES} z)

add_executable(nfb-bus bus/bus.c)

add_executable(nfb-dma dma/dma.c)

add_executable(nfb-eth
	eth/cfp2.c eth/ieee802_3.c eth/main.c eth/pcspma.c eth/qsfpp.c
	eth/query.c eth/rxmac.c eth/transceiver.c eth/txmac.c
)
target_link_libraries(nfb-eth PRIVATE m)

add_executable(nfb-tsu tsu/tsu.c)

add_executable(nfb-mdio mdio/mdio.c)

add_executable(ndp-tool
	ndptool/common.c ndptool/generate.c ndptool/loopback.c
	ndptool/loopback_hw.c ndptool/main.c ndptool/modules.c ndptool/pcap.c
	ndptool/read.c ndptool/receive.c ndptool/stats.c ndptool/transmit.c
)

set (PIE_TARGETS nfb-info nfb-boot nfb-bus nfb-dma nfb-eth nfb-tsu nfb-mdio ndp-tool)
foreach(PIE_TARGET IN LISTS PIE_TARGETS)
	set_property(TARGET ${PIE_TARGET} PROPERTY POSITION_INDEPENDENT_CODE TRUE)
endforeach()

target_compile_options(ndp-tool PRIVATE -O3 -funroll-loops)
target_link_libraries(ndp-tool
	PRIVATE ${NUMA_LIBRARIES} ${NCURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
)

set(NDP_TARGETS receive read transmit loopback loopback-hw generate)
foreach(NDP_TARGET IN LISTS NDP_TARGETS)
	add_custom_target(ndp-${NDP_TARGET}
		ALL DEPENDS ndp-tool
		COMMAND ${CMAKE_COMMAND} -E create_symlink ndp-tool ndp-${NDP_TARGET}
	)
endforeach()

install(
	TARGETS nfb-boot nfb-bus nfb-dma nfb-eth nfb-info nfb-mdio nfb-tsu ndp-tool
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

foreach(NDP_TARGET IN LISTS NDP_TARGETS)
	install(
		FILES ${CMAKE_CURRENT_BINARY_DIR}/ndp-${NDP_TARGET}
		DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
endforeach()

if(USE_DPDK)
	message("ndp-tool: DPDK portion of the app is enabled use -UUSE_DPDK with cmake if you don't want to compile with DPDK")
	pkg_check_modules(DPDK REQUIRED libdpdk>=20.11)

	add_executable(ndp-tool-dpdk
	ndptool/common.c ndptool/generate.c ndptool/loopback.c
	ndptool/loopback_hw.c ndptool/main.c ndptool/modules.c ndptool/pcap.c
	ndptool/read.c ndptool/receive.c ndptool/stats.c ndptool/transmit.c
	ndptool/dpdk/dpdk_generate.c ndptool/dpdk/dpdk_read.c ndptool/dpdk/dpdk_loopback.c
	ndptool/dpdk/dpdk_receive.c ndptool/dpdk/dpdk_transmit.c
	)

	set_property(TARGET ndp-tool-dpdk PROPERTY POSITION_INDEPENDENT_CODE TRUE)

	set(DPDK_TARGETS dpdk-generate dpdk-read dpdk-loopback dpdk-receive dpdk-transmit)

	target_compile_options(ndp-tool-dpdk PRIVATE -O3 -funroll-loops ${DPDK_CFLAGS})
	target_link_libraries(ndp-tool-dpdk
	PRIVATE ${NUMA_LIBRARIES} ${NCURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${DPDK_LDFLAGS}
	)
	target_compile_definitions(ndp-tool-dpdk PRIVATE USE_DPDK)

	foreach(DPDK_TARGET IN LISTS DPDK_TARGETS)
	add_custom_target(ndp-${DPDK_TARGET}
		ALL DEPENDS ndp-tool-dpdk
		COMMAND ${CMAKE_COMMAND} -E create_symlink ndp-tool-dpdk ndp-${DPDK_TARGET}
	)
	endforeach()

	install(
	TARGETS ndp-tool-dpdk
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	COMPONENT ndp-tool-dpdk
	)

	foreach(DPDK_TARGET IN LISTS DPDK_TARGETS)
	install(
		FILES ${CMAKE_CURRENT_BINARY_DIR}/ndp-${DPDK_TARGET}
		DESTINATION ${CMAKE_INSTALL_BINDIR}
		COMPONENT ndp-tool-dpdk
	)
	endforeach()
else()
	message("ndp-tool: DPDK portion of the app not enabled use -DUSE_DPDK=true with cmake if you want to compile with DPDK")
endif()