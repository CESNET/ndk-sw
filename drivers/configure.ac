# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
#
# Configure script for NFB driver
#
# Copyright (C) 2017-2022 CESNET
#
# Author(s):
#     Martin Spinler <spinler@cesnet.cz>

DEFAULT_PACKAGE_NAME="nfb"
AC_PREREQ([2.63])
AC_INIT(m4_esyscmd([ if [ -z $CMAKE_PACKAGE_NAME ]; then echo -n $DEFAULT_PACKAGE_NAME; else echo -n $CMAKE_PACKAGE_NAME; fi;]), m4_esyscmd([ if [ -z $CMAKE_PACKAGE_VERSION ]; then cut -f1 ../VERSION | tr -d '\n'; else echo -n $CMAKE_PACKAGE_VERSION; fi;]))

AC_ARG_ENABLE([parallel-config], AS_HELP_STRING([--disable-parallel-config], [Disable parallel checking of configure flags]))

AC_CONFIG_HEADERS(config.h)

AC_DEFUN([KERNEL_TRY_COMPILE],[
AS_IF([test "x$enable_parallel_config" != "xno"],
[
test_cnt=$((test_cnt+1)) # increment test counter before running in subshell
{ # run as a background job in the subshell
$(
# prepare the test enviroment
mkdir -p .test${test_cnt} >/dev/null 2>&1
if ! test -d .test${test_cnt}; then
	AC_MSG_FAILURE([Failed to enter .test${test_cnt} directory])
fi
cd .test${test_cnt}
echo KDIR=$KDIR >Makefile
cat >>Makefile <<EOF
KBUILD=\$(MAKE) -C \$(KDIR) M=\$(PWD)
ccflags-y+=-Werror

obj-m := test.o

all:
EOF
printf '\t$(KBUILD) test.s' >>Makefile
echo '$1' >test.c
# run the test, save the output to temp file and exit code to $retval
make >./make_out 2>&1
retval=$?
# log the results
exec 200>../flock_lockfile
flock -x 200
echo '---------------------------start test parallel--------------------------' >&AS_MESSAGE_LOG_FD
$2 # checking whether ...
echo 'Using this program for test:' >&AS_MESSAGE_LOG_FD
cat test.c >&AS_MESSAGE_LOG_FD
printf '\nUsing this Makefile for test:\n' >&AS_MESSAGE_LOG_FD
cat Makefile >&AS_MESSAGE_LOG_FD
printf '\nMakefile output:\n' >&AS_MESSAGE_LOG_FD
cat ./make_out >&AS_MESSAGE_LOG_FD
cd .. # back to work directory to handle results
if test $retval -eq 0; then
	$3 # result yes; define config value
else
	$4 # result no
fi
echo '^^^^^^^^^^^^^^^^^^^^^^^^^^^^end test parallel^^^^^^^^^^^^^^^^^^^^^^^^^^^' >&AS_MESSAGE_LOG_FD
flock -u 200 
# cleanup
rm -rf .test${test_cnt}/ >/dev/null 2>/dev/null
)
} &
],[ # ELSE
mkdir .test >/dev/null 2>&1
if ! test -d .test; then
	AC_MSG_FAILURE([Failed to enter .test directory])
fi
cd .test
echo KDIR=$KDIR >Makefile
cat >>Makefile <<EOF
KBUILD=\$(MAKE) -C \$(KDIR) M=\$(PWD)
ccflags-y+=-Werror

obj-m := test.o

all:
EOF
echo -e '\t$(KBUILD) test.s' >>Makefile

echo '$1' >test.c
echo '---------------------------start test sequential--------------------------' >&AS_MESSAGE_LOG_FD
$2 # checking whether ...
echo 'Using this program for test:' >&AS_MESSAGE_LOG_FD
cat test.c >&AS_MESSAGE_LOG_FD
cat Makefile >&AS_MESSAGE_LOG_FD
make >&AS_MESSAGE_LOG_FD 2>&1
retval=$?
cd ..
rm -rf .test/ >/dev/null 2>/dev/null
if test $retval -eq 0; then
	$3 # result yes; define config value
else
	$4 # result no;
	true
fi
echo '^^^^^^^^^^^^^^^^^^^^^^^^^^^^end test sequential^^^^^^^^^^^^^^^^^^^^^^^^^^^' >&AS_MESSAGE_LOG_FD
] # ENDIF
)]) # KERNEL_TRY_COMPILE

# Kernel build dir
KSRC=""
AC_MSG_CHECKING([for kernel build dir])
AC_ARG_WITH([kernel], AS_HELP_STRING([--with-kernel=DIR],[use this kernel build dir]),
	[test "$withval" = "yes" && AC_MSG_FAILURE(no path passed in --with-kernel);
		KDIR=$withval],
	[KDIR=/lib/modules/`uname -r`/build/ KSRC=/lib/modules/`uname -r`/source/])
test -d $KDIR || AC_MSG_FAILURE($KDIR does not exist)
AC_MSG_RESULT($KDIR)

# Is kernel build dir usable?
KERNEL_TRY_COMPILE([],[AC_MSG_CHECKING([whether kernel build dir is usable])],[AC_MSG_RESULT([yes])],[AC_MSG_RESULT([no])
	AC_MSG_FAILURE([Unusable kernel build dir. Install kernel sources.])])

# wait for this check before running parallel tests for symbols, don't continue with unusable kernel dir
wait $!
if test ! $? -eq 0; then
	exit $?
fi

# Kernel source dir
AC_MSG_CHECKING([for kernel source dir])
AC_ARG_WITH([source], AS_HELP_STRING([--with-source=DIR],[use this kernel source dir]),
	[test "$withval" = "yes" && AC_MSG_FAILURE(no path passed in --with-kernel);
		KSRC=$withval],
	[])
AC_MSG_RESULT($KSRC)

# Tests for symbols
AC_MSG_NOTICE([Starting the check for kernel symbols, see config.log for detailed results.])
KERNEL_TRY_COMPILE([[
#include <linux/time.h>
struct timespec ts;
]],
[AC_MSG_CHECKING([whether kernel has struct timespec])],
[AC_DEFINE([CONFIG_HAVE_TIMESPEC], [1], [Define if kernel have struct timespec]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/netdevice.h>
void test(void);
void test(void) {netif_rx_ni(NULL);}
]],
[AC_MSG_CHECKING([whether kernel has netif_rx_ni])],
[AC_DEFINE([CONFIG_HAVE_NETIF_RX_NI], [1], [Define if kernel have netif_rx_ni]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/etherdevice.h>
void test(void);
void test(void) {eth_hw_addr_set(NULL, NULL);}
]],
[AC_MSG_CHECKING([whether kernel has eth_hw_addr_set])],
[AC_DEFINE([CONFIG_HAVE_ETH_HW_ADDR_SET], [1], [Define if kernel have eth_hw_addr_set]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/regmap.h>
#include <linux/iopoll.h>
void test(void);
static const struct regmap_range regmap_range_test[] = {regmap_reg_range(0,0)};
static int trd(off_t a) {return 0;} void test(void) {int ret; read_poll_timeout(trd, ret, (!ret), 1, 2, false, 0);}
]],
[AC_MSG_CHECKING([whether kernel has regmap_reg_range and read_poll_timeout])],
[AC_DEFINE([CONFIG_NFB_ENABLE_PMCI], [1], [Define if kernel have regmap_reg_range and read_poll_timeout]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/device.h>
void test(void);
struct device dev;
void test(void) {devm_kstrdup_const(&dev, "test", GFP_KERNEL);}
]],
[AC_MSG_CHECKING([whether kernel has devm_kstrdup_const])],
[AC_DEFINE([CONFIG_NFB_ENABLE_HWMON], [1], [Define if kernel has devm_kstrdup_const()]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/mm.h>
void test(void);
void test(void) {vm_flags_set(NULL, 0);}
]],
[AC_MSG_CHECKING([whether kernel has vm_flags_set])],
[AC_DEFINE([CONFIG_HAVE_VM_FLAGS_SET], [1], [Define if kernel has vm_flags_set()]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/ptp_clock_kernel.h>
void test(void);
void test(void) {struct ptp_clock_info ci; ci.ajdfreq = NULL;}
]],
[AC_MSG_CHECKING([whether struct ptp_clock_info has adjfreq])],
[AC_DEFINE([CONFIG_HAVE_PTP_CLOCK_INFO_ADJFREQ], [1], [Define if struct ptp_clock_info has adjfreq]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/device.h>
void test(void);
void test(void) {struct class *c = class_create(""); if (c) {} }
]],
[AC_MSG_CHECKING([whether class_create has only one parameter])],
[AC_DEFINE([CONFIG_CLASS_CREATE_HAVE_ONE_PARAMETER], [1], [Define if class_create has only one parameter]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/iopoll.h>
void test(void);
static int trd(off_t a) {return 0;} void test(void) {int ret; read_poll_timeout_atomic(trd, ret, (!ret), 1, 2, false, 0);}
]],
[AC_MSG_CHECKING([whether kernel has read_poll_timeout_atomic])],
[AC_DEFINE([CONFIG_HAVE_READ_POLL_TIMEOUT_ATOMIC], [1], [Define if kernel has read_poll_timeout_atomic]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/spi/spi.h>
struct spi_controller *sc;
void test(void);
void test(void) {spi_get_chipselect(NULL, 0);}
]],
[AC_MSG_CHECKING([whether kernel has struct spi_controller])],
[AC_DEFINE([CONFIG_HAVE_SPI_CONTROLLER], [1], [Define if kernel has struct spi_controller]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
struct spi_bitbang sb;
void test(void);
void test(void) {sb.ctlr = NULL;}
]],
[AC_MSG_CHECKING([whether kernel has ctrl in struct spi_bitbang])],
[AC_DEFINE([CONFIG_HAVE_SPI_BITBANG_CTRL], [1], [Define if kernel has ctrl in struct spi_bitbang]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/spi/altera.h>
struct spi_controller sc;
void test(void);
#if !IS_ENABLED(CONFIG_SPI_ALTERA)
#error CONFIG_SPI_ALTERA is not enabled, failing altera_spi_init_master test explicitly
#endif
void test(void) {altera_spi_init_master(&sc);}
]],
[AC_MSG_CHECKING([whether kernel has altera_spi_init_master])],
[AC_DEFINE([CONFIG_HAVE_SPI_INIT_MASTER], [1], [Define if kernel has altera_spi_init_master]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/spi/altera.h>
struct spi_controller sc;
void test(void);
#if !IS_ENABLED(CONFIG_SPI_ALTERA)
#error CONFIG_SPI_ALTERA is not enabled, failing altera_spi_init_host test explicitly
#endif
void test(void) {altera_spi_init_host(&sc);}
]],
[AC_MSG_CHECKING([whether kernel has altera_spi_init_host])],
[AC_DEFINE([CONFIG_HAVE_SPI_INIT_HOST], [1], [Define if kernel has altera_spi_init_host]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/eventfd.h>
void test(void);
void test(void) {eventfd_signal(NULL);}
]],
[AC_MSG_CHECKING([whether kernel has simplified eventfd_signal])],
[AC_DEFINE([CONFIG_HAVE_SIMPLIFIED_EVENTFD_SIGNAL], [1], [Define if kernel has simplified eventfd_signal]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <net/page_pool/helpers.h>
]],
[AC_MSG_CHECKING([whether kernel has net/page_pool/helpers.h])],
[AC_DEFINE([CONFIG_HAVE_PAGE_POOL_HELPERS], [1], [Define if kernel has net/page_pool/helpers.h]) AC_MSG_RESULT([yes])]
,[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/netdevice.h>
void test(void);
void test(void) {netif_napi_add_tx_weight(NULL, NULL, NULL, NAPI_POLL_WEIGHT);}
]],
[AC_MSG_CHECKING([whether kernel has netif_napi_add_tx_weight])],
[AC_DEFINE([CONFIG_HAVE_NETIF_NAPI_ADD_TX_WEIGHT], [1], [Define if kernel has netif_napi_add_tx_weight]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/netdevice.h>
void test(void);
void test(void) {netif_napi_add(NULL, NULL, NULL, NAPI_POLL_WEIGHT);}
]],
[AC_MSG_CHECKING([whether kernel has netif_napi_add with weight argument])],
[AC_DEFINE([CONFIG_HAVE_NETIF_NAPI_ADD_WITH_WEIGHT], [1], [Define if kernel has netif_napi_add with weight argument]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <net/xdp_sock_drv.h>
void test(void);
void test(void) {xsk_buff_alloc_batch(NULL, NULL, 64);}
]],
[AC_MSG_CHECKING([whether kernel has xsk_buff_alloc_batch])],
[AC_DEFINE([CONFIG_HAVE_XSK_BUFF_ALLOC_BATCH], [1], [Define if kernel has xsk_buff_alloc_batch]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <net/xdp_sock_drv.h>
void test(void);
void test(void) {xsk_buff_set_size(NULL, 512);}
]],
[AC_MSG_CHECKING([whether kernel has xsk_buff_set_size])],
[AC_DEFINE([CONFIG_HAVE_XSK_BUFF_SET_SIZE], [1], [Define if kernel has xsk_buff_set_size]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <net/xdp.h>
xdp_features_t val;
void test(void);
void test(void) {xdp_set_features_flag(NULL, val);}
]],
[AC_MSG_CHECKING([whether kernel has xdp_set_features_flag])],
[AC_DEFINE([CONFIG_HAVE_XDP_SET_FEATURES_FLAG], [1], [Define if kernel has xdp_set_features_flag]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <net/xdp.h>
xdp_features_t val;
]],
[AC_MSG_CHECKING([whether kernel has xdp_features_t])],
[AC_DEFINE([CONFIG_HAVE_XDP_FEATURES_T], [1], [Define if kernel has xdp_features_t]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])
KERNEL_TRY_COMPILE([[
#include <linux/unaligned.h>
]],
[AC_MSG_CHECKING([whether kernel has linux/unaligned.h])],
[AC_DEFINE([CONFIG_HAVE_LINUX_UNALIGNED], [1], [Define if kernel has linux/unaligned.h]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/platform_device.h>
static void t_remove(struct platform_device *pdev) {}
struct platform_driver pd = {
.remove = t_remove,
};
]],
[AC_MSG_CHECKING([whether kernel has platfom_driver remove as void return type])],
[AC_DEFINE([CONFIG_HAVE_PLATFORM_DRIVER_REMOVE_VOID], [1], [Define if kernel has platform_driver remove as void]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/device.h>
void test(void);
void test(void) {devm_device_add_groups(NULL, NULL);}
]],
[AC_MSG_CHECKING([whether kernel has devm_device_add_groups])],
[AC_DEFINE([CONFIG_HAVE_DEVM_DEVICE_ADD_GROUPS], [1], [Define if kernel has devm_evice_add_groups]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/timer.h>
void test(void);
void test(void) {timer_delete_sync(NULL);}
]],
[AC_MSG_CHECKING([whether kernel has timer_delete_sync])],
[AC_DEFINE([CONFIG_HAVE_TIMER_DELETE_SYNC], [1], [Define if kernel has timer_delete_sync]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

KERNEL_TRY_COMPILE([[
#include <linux/hrtimer.h>
void test(void);
void test(void) {hrtimer_setup(NULL, NULL, 0, 0);}
]],
[AC_MSG_CHECKING([whether kernel has hrtimer_setup])],
[AC_DEFINE([CONFIG_HAVE_HRTIMER_SETUP], [1], [Define if kernel has httimer_setup]) AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no])])

# wait for the jobs to finnish, remove file used for locking if it exists
wait
rm -rf flock_lockfile >/dev/null 2>/dev/null

AC_SUBST(KDIR)
AC_SUBST(KSRC)
AC_OUTPUT
