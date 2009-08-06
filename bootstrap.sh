#!/bin/bash

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

VERSION=1.11

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

if [ "x$1" = "xam" ] ; then
    run_versioned automake "$VERSION" -a -c --foreign
    ./config.status
else
    rm -rf autom4te.cache
    rm -f config.cache

    touch config.rpath
    test "x$LIBTOOLIZE" = "x" && LIBTOOLIZE=libtoolize

    mkdir -p m4
    "$LIBTOOLIZE" -c --force
    run_versioned aclocal "$VERSION"
    run_versioned autoconf 2.63 -Wall
    run_versioned autoheader 2.63
    run_versioned automake "$VERSION" --copy --foreign --add-missing

    if test "x$NOCONFIGURE" = "x"; then
        CFLAGS="-g -O0" ./configure --sysconfdir=/etc --localstatedir=/var "$@"
        make clean
    fi
fi
