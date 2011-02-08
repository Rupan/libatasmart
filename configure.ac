dnl -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

dnl This file is part of libatasmart.
dnl
dnl Copyright 2008 Lennart Poettering
dnl
dnl libatasmart is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU Lesser General Public License as
dnl published by the Free Software Foundation, either version 2.1 of the
dnl License, or (at your option) any later version.
dnl
dnl libatasmart is distributed in the hope that it will be useful, but
dnl WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
dnl Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public
dnl License along with libatasmart. If not, If not, see
dnl <http://www.gnu.org/licenses/>.

AC_PREREQ(2.63)

AC_INIT([libatasmart], 0.17, [mzyvongnfzneg (at) 0pointer (dot) net])
AC_CONFIG_SRCDIR([atasmart.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR(m4)

AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax])

AC_SUBST(PACKAGE_URL, [http://git.0pointer.de/?p=libatasmart.git])

AC_SUBST(LIBATASMART_VERSION_INFO, [4:3:0])

AM_SILENT_RULES([yes])

if type -p stow > /dev/null && test -d /usr/local/stow ; then
   AC_MSG_NOTICE([*** Found /usr/local/stow: default install prefix set to /usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION} ***])
   ac_default_prefix="/usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION}"
fi

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AX_CC_FOR_BUILD

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AC_PROG_LIBTOOL

dnl Check compiler flags
CC_CHECK_CFLAGS_APPEND([-Wall -W -Wextra -pedantic -pipe -Wformat -Wold-style-definition -Wdeclaration-after-statement -Wfloat-equal -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wmissing-noreturn -Wshadow -Wendif-labels -Wpointer-arith -Wcast-align -Wwrite-strings -Winline -Wno-unused-parameter -ffast-math])

PKG_CHECK_MODULES([LIBUDEV], [libudev >= 143])

dnl###################################
dnl#            Output               #
dnl###################################

AC_CONFIG_FILES([
Makefile
strpool/Makefile
libatasmart.pc
])
AC_OUTPUT

echo "
 ---{ $PACKAGE_NAME $VERSION }---

    prefix:                 ${prefix}
    sysconfdir:             ${sysconfdir}
    localstatedir:          ${localstatedir}
    Compiler:               ${CC}
    CFLAGS:                 ${CFLAGS}
"
