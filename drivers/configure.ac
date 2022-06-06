# SPDX-License-Identifier: GPL-2.0
#
# Configure script for NFB driver
#
# Copyright (C) 2017-2022 CESNET
#
# Author(s):
#     Martin Spinler <spinler@cesnet.cz>

DEFAULT_PACKAGE_NAME="nfb"
AC_PREREQ([2.63])
AC_INIT(m4_esyscmd([ if [[ -z $CMAKE_PACKAGE_NAME ]]; then echo -n $DEFAULT_PACKAGE_NAME; else echo -n $CMAKE_PACKAGE_NAME; fi;]), m4_esyscmd([ if [[ -z $CMAKE_PACKCAGE_VERSION ]]; then cut -f1 ../VERSION | tr -d '\n'; else echo -n $CMAKE_PACKCAGE_VERSION; fi;]))

AC_CONFIG_HEADERS(config.h)

AC_DEFUN([KERNEL_TRY_COMPILE],[
mkdir .test >/dev/null 2>&1
if ! test -d .test; then
	AC_MSG_FAILURE([Failed to enter .test directory])
fi
cd .test
echo KDIR=$KDIR >Makefile
cat >>Makefile <<EOF
KBUILD=\$(MAKE) -C \$(KDIR) M=\$(PWD)
EXTRA_CFLAGS+=-Werror

obj-m := test.o

all:
EOF
echo -e '\t$(KBUILD) test.s' >>Makefile

echo '$1' >test.c
echo 'Using this program for test:' >&AS_MESSAGE_LOG_FD
cat test.c >&AS_MESSAGE_LOG_FD
cat Makefile >&AS_MESSAGE_LOG_FD
make >&AS_MESSAGE_LOG_FD 2>&1
retval=$?
cd ..
if test $retval -eq 0; then
	rm -rf .test/ >/dev/null 2>/dev/null
	$2
else
	$3
	true
fi
])

# Kernel build dir
KSRC=""
AC_MSG_CHECKING([for kernel build dir])
AC_ARG_WITH([kernel], AS_HELP_STRING([--with-kernel=DIR],[use this kernel build dir]),
	[test "$withval" = "yes" && AC_MSG_FAILURE(no path passed in --with-kernel);
		KDIR=$withval],
	[KDIR=/lib/modules/`uname -r`/build/ KSRC=/lib/modules/`uname -r`/source/])
test -d $KDIR || AC_MSG_FAILURE($KDIR doesn't exist)
AC_MSG_RESULT($KDIR)

# Is kernel build dir usable?
AC_MSG_CHECKING([whether kernel build dir is usable])
KERNEL_TRY_COMPILE([],[AC_MSG_RESULT([yes])],[AC_MSG_RESULT([no])
	AC_MSG_FAILURE([Unusable kernel build dir. Install kernel sources.])])

# Kernel source dir
AC_MSG_CHECKING([for kernel source dir])
AC_ARG_WITH([source], AS_HELP_STRING([--with-source=DIR],[use this kernel source dir]),
	[test "$withval" = "yes" && AC_MSG_FAILURE(no path passed in --with-kernel);
		KSRC=$withval],
	[])
AC_MSG_RESULT($KSRC)

# Tests for symbols
AC_MSG_CHECKING([whether kernel has struct timespec])
KERNEL_TRY_COMPILE([[
#include <linux/time.h>
struct timespec ts;
]],[AC_DEFINE([CONFIG_HAVE_TIMESPEC], [1], [Define if kernel have struct timespec]) AC_MSG_RESULT([yes])],[AC_MSG_RESULT([no])])

# Tests for symbols
AC_MSG_CHECKING([whether kernel has netif_rx_ni])
KERNEL_TRY_COMPILE([[
#include <linux/netdevice.h>
void test(void) {netif_rx_ni(NULL);}
]],[AC_DEFINE([CONFIG_HAVE_NETIF_RX_NI], [1], [Define if kernel have netif_rx_ni]) AC_MSG_RESULT([yes])],[AC_MSG_RESULT([no])])

AC_SUBST(KDIR)
AC_SUBST(KSRC)

AC_OUTPUT