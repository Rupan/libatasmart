#!/bin/bash

# This file is part of libatasmart.
#
# Copyright 2008-2011 Lennart Poettering
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
# License along with libatasmart; If not, see
# <http://www.gnu.org/licenses/>.

AM_VERSION=1.11
AC_VERSION=2.63

run_versioned() {
    local P
    local V

    V=$(echo "$2" | sed -e 's,\.,,g')

    if [ -e "`which $1$V 2> /dev/null`" ] ; then
        P="$1$V"
    else
        if [ -e "`which $1-$2 2> /dev/null`" ] ; then
            P="$1-$2"
        else
            P="$1"
        fi
    fi

    shift 2
    "$P" "$@"
}

set -ex

if [ -f .git/hooks/pre-commit.sample -a ! -f .git/hooks/pre-commit ] ; then
    cp -p .git/hooks/pre-commit.sample .git/hooks/pre-commit && \
    chmod +x .git/hooks/pre-commit && \
    echo "Activated pre-commit hook."
fi

if type -p colorgcc > /dev/null ; then
   export CC=colorgcc
fi

libdir() {
    echo $(cd $1/$(gcc -print-multi-os-directory); pwd)
}

if [ "x$1" = "xam" ] ; then
    run_versioned automake "$AM_VERSION" -a -c --foreign
    ./config.status
else
    rm -rf autom4te.cache
    rm -f config.cache

    libtoolize -c --force
    run_versioned aclocal "$AM_VERSION" -I m4
    run_versioned autoconf "$AC_VERSION" -Wall
    run_versioned autoheader "$AC_VERSION"
    run_versioned automake "$AM_VERSION" --copy --foreign --add-missing

    if [ "x$1" != "xac" ]; then
        CFLAGS="$CFLAGS -g -O0" ./configure \
          --sysconfdir=/etc \
          --localstatedir=/var \
          --libexecdir=/usr/lib \
          --libdir=$(libdir /usr/local/lib) \
          "$@"
        make clean
    fi
fi
