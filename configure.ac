# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# This file is part of libatasmart.
#
# Copyright 2008 Lennart Poettering
#
# libatasmart is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 2.1 of the
# License, or (at your option) any later version.
#
# libatasmart is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with libatasmart. If not, If not, see
# <http://www.gnu.org/licenses/>.

AC_PREREQ(2.63)

AC_INIT([libatasmart], 0.17, [mzyvongnfzneg (at) 0pointer (dot) net])
AC_CONFIG_SRCDIR([atasmart.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR(m4)

AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax])

AC_SUBST(PACKAGE_URL, [http://git.0pointer.de/?p=libatasmart.git])

AC_SUBST(LIBATASMART_VERSION_INFO, [4:3:0])

AC_CANONICAL_HOST

AM_SILENT_RULES([yes])

if type -p stow > /dev/null && test -d /usr/local/stow ; then
   AC_MSG_NOTICE([*** Found /usr/local/stow: default install prefix set to /usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION} ***])
   ac_default_prefix="/usr/local/stow/${PACKAGE_NAME}-${PACKAGE_VERSION}"
fi

#### Checks for programs. ####

# CC

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_USE_SYSTEM_EXTENSIONS

# C++

AC_PROG_CXX

# GCC flags

test_gcc_flag() {
    AC_LANG_CONFTEST([int main(int argc, char*argv[]) {}])
    $CC -c conftest.c $CFLAGS "$1" -o conftest.o > /dev/null 2> /dev/null
    ret=$?
    rm -f conftest.o
    return $ret
}

# If using GCC specify some additional parameters
if test "x$GCC" = "xyes" ; then

    # We use gnu99 instead of c99 because many have interpreted the standard
    # in a way that int64_t isn't defined on non-64 bit platforms.
    DESIRED_FLAGS="-std=gnu99 -Wall -W -Wextra -pedantic -pipe -Wformat -Wold-style-definition -Wdeclaration-after-statement -Wfloat-equal -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wmissing-noreturn -Wshadow -Wendif-labels -Wpointer-arith -Wcast-align -Wwrite-strings -Winline -Wno-unused-parameter -ffast-math"

    for flag in $DESIRED_FLAGS ; do
        AC_MSG_CHECKING([whether $CC accepts $flag])
        if test_gcc_flag $flag ; then
           CFLAGS="$CFLAGS $flag"
           CXXFLAGS="$CXXFLAGS $flag"
           AC_MSG_RESULT([yes])
        else
           AC_MSG_RESULT([no])
        fi
    done
fi

#### libtool stuff ####

AC_PROG_LIBTOOL

#### Checks for header files. ####

# ISO
AC_HEADER_STDC

# XPG4-UNIX
AC_CHECK_HEADERS([sys/poll.h])

# Other
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([byteswap.h])

#### Typdefs, structures, etc. ####

AC_C_CONST
AC_C_BIGENDIAN
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_TYPES(ssize_t, , [AC_DEFINE([ssize_t], [signed long],
    [Define ssize_t if it is not done by the standard libs.])])
AC_TYPE_OFF_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T

#### Large File-Support (LFS) ####

AC_SYS_LARGEFILE

PKG_CHECK_MODULES([LIBUDEV], [libudev])
AC_DEFINE([LIBUDEV_I_KNOW_THE_API_IS_SUBJECT_TO_CHANGE], 1, [I know the API is subject to change.])

###################################
#            Output               #
###################################

AC_CONFIG_FILES([
Makefile
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
