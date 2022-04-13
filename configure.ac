#
# NFB framework configure script
#
# Copyright (C) 2018 CESNET
# Author(s):
#     Martin Spinler <spinler@cesnet.cz>
#
# SPDX-License-Identifier: GPL-2.0
#

# ----[ SECTION: Autoconf setup ]---- #
AC_PREREQ([2.63])
AC_INIT([nfb-framework], m4_esyscmd([cut -f1 ./VERSION | tr -d '\n']))

RELEASE=$(grep 'RELEASE *=' Makefile | cut -d= -f2 | xargs)
AC_SUBST(RELEASE)

# ----[ SECTION: Settings ]---- #
check_drivers=yes
AC_ARG_ENABLE([drivers],
			[AS_HELP_STRING([--disable-drivers], [Disable checking for driver-related features])],
			[check_drivers=$enableval],
			[check_drivers=yes]
)

# ----[ SECTION: Check installed stuff ]---- #
AC_PROG_CC

AC_CHECK_LIB([fdt], [fdt_check_header], [], [AC_MSG_FAILURE([libfdt not available])])
AC_CHECK_LIB([numa], [numa_available], [], [AC_MSG_FAILURE([libnuma not available])])
AC_CHECK_LIB([ncurses], [initscr], [], [AC_MSG_FAILURE([libncurses not available])])
AC_CHECK_LIB([archive], [archive_read_open_filename], [], [AC_MSG_FAILURE([libarchive not available])])

# We should check standard headers here, but they really are present usually,
# plus the compilation will fail and complain, plus we cannot do anything but
# fail the ./configure here anyway.

# The same goes for types and functions

# ----[ SECTION: Configure paths to required headers ]---- #
AC_ARG_WITH([driver-headers],
			[AC_HELP_STRING([--with-driver-headers=PATH], [Set path to nfb driver headers])],
			[ # --with-driver-headers was specified
				DRIVER_CFLAGS="-I$withval"
			]
)
AC_SUBST(DRIVER_CFLAGS)

AC_ARG_WITH([libnfb],
			[AC_HELP_STRING([--with-libnfb=PATH], [Set path to libnfb])],
			[ # --with-libnfb was specified
				LIBNFB_CFLAGS="-I$withval/include"
				LIBNFB_LDFLAGS="-L$withval"
			]
)
AC_SUBST(LIBNFB_CFLAGS)
AC_SUBST(LIBNFB_LDFLAGS)

AC_ARG_WITH([kernel],
			AS_HELP_STRING([--with-kernel=DIR],[use kernel build directory DIR]),
			[ # --with-kernel was specified
				test "$withval" == "yes" && AC_MSG_ERROR([no path specified in --with-kernel]);
				KDIR="$withval"
			],
			[
				KDIR="/lib/modules/$(uname -r)/build"
			]
)
AC_SUBST(KDIR)

if test "x${check_drivers}" == "xyes"; then
	test -d "$KDIR" ||
		AC_MSG_ERROR([kernel build directory $KDIR does not exist])

	test -f "$KDIR/include/linux/types.h" ||
		AC_MSG_ERROR([cannot find include/linux/types.h in $KDIR, use proper kernel build directory])
fi

# ----[ SECTION: Enable optional features ]---- #
NFB_DEBUG=no
AC_ARG_WITH([debug],
			[AC_HELP_STRING([--with-debug], [Enable debugging messages])],
			[
				NFB_DEBUG=yes
			]
)
AC_SUBST(NFB_DEBUG)

# ----[ SECTION: Fix variables for x86_64 ]---- #
libdir='${prefix}/lib64'


# ----[ SECTION: Generate output ]---- #
AC_CONFIG_FILES(mk/common.mk spec/nfb-framework.spec drivers/dkms.conf drivers/Makefile utils/nfb-framework.pc)
AC_CONFIG_SUBDIRS([drivers])
AC_OUTPUT
