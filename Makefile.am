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

EXTRA_DIST = \
	bootstrap.sh \
	LGPL \
	README \
	atasmart.vapi \
	atasmart.c \
	blob-examples/README \
	blob-examples/FUJITSU_MHY2120BH--0084000D \
	blob-examples/FUJITSU_MHY2120BH--0085000B \
	blob-examples/FUJITSU_MHZ2160BH_G1--0084000A \
	blob-examples/Maxtor_96147H8--BAC51KJ0 \
	blob-examples/SAMSUNG_HD501LJ--CR100-12 \
	blob-examples/ST320410A--3.39 \
	blob-examples/ST9160821AS--3.CLH \
	blob-examples/WDC_WD2500JB--00REA0-20.00K20 \
	blob-examples/WDC_WD2500JS-75NCB3--10.02E04 \
	blob-examples/WDC_WD5000AAKS--00TMA0-12.01C01

SUBDIRS =

CLEANFILES = atasmart.strpool.c

MAINTAINERCLEANFILES =
noinst_DATA =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libatasmart.pc

include_HEADERS = \
	atasmart.h

sbin_PROGRAMS = \
	skdump \
	sktest

noinst_PROGRAMS = \
	strpool

lib_LTLIBRARIES = \
	libatasmart.la

skdump_SOURCES = \
	skdump.c
skdump_LDADD = \
	libatasmart.la

sktest_SOURCE = \
	sktest.c
sktest_LDADD = \
	libatasmart.la

libatasmart_la_SOURCES = \
	atasmart.strpool.c atasmart.h
libatasmart_la_LDFLAGS = \
	-version-info $(LIBATASMART_VERSION_INFO)
libatasmart_la_LIBADD = \
	$(LIBUDEV_LIBS)
libatasmart_la_CFLAGS = \
	$(LIBUDEV_CFLAGS)

strpool_SOURCES = \
	strpool.c

BUILT_SOURCES = \
	atasmart.strpool.c

atasmart.strpool.c: atasmart.c strpool
	$(top_builddir)/strpool $< $@

ACLOCAL_AMFLAGS = -I m4
