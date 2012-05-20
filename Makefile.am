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

AM_CFLAGS = $(WARNINGFLAGS)
AM_LDFLAGS = $(GCLDFLAGS)

dist_doc_DATA = README

EXTRA_DIST = \
	autogen.sh \
	LGPL \
	README \
	atasmart.c \
	blob-examples/README \
	blob-examples/FUJITSU_MHY2120BH--0084000D \
	blob-examples/FUJITSU_MHY2120BH--0085000B \
	blob-examples/FUJITSU_MHY2250BH--0085000B \
	blob-examples/FUJITSU_MHZ2160BH_G1--0084000A \
	blob-examples/INTEL_SSDSA2MH080G1GC--045C8820 \
	blob-examples/Maxtor_96147H8--BAC51KJ0 \
	blob-examples/Maxtor_96147H8--BAC51KJ0--2 \
	blob-examples/MCCOE64GEMPP--2.9.09 \
	blob-examples/SAMSUNG_HD501LJ--CR100-12 \
	blob-examples/SAMSUNG_MMCQE28G8MUP--0VA_VAM08L1Q \
	blob-examples/SAMSUNG_MP0804H--UE100-14 \
	blob-examples/ST320410A--3.39 \
	blob-examples/ST9160821AS--3.CLH \
	blob-examples/ST9100821AS--3.CME \
	blob-examples/TOSHIBA_MK1651GSY--38IGT0G5T \
	blob-examples/WDC_WD2500JB--00REA0-20.00K20 \
	blob-examples/WDC_WD2500JS-75NCB3--10.02E04 \
	blob-examples/WDC_WD5000AAKS--00TMA0-12.01C01 \
	vala/atasmart.vapi

# build the strpool tool first
SUBDIRS = strpool .

CLEANFILES = atasmart.strpool.c

MAINTAINERCLEANFILES =
noinst_DATA =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libatasmart.pc

vapidir = $(datadir)/vala/vapi
vapi_DATA = vala/atasmart.vapi

include_HEADERS = \
	atasmart.h

sbin_PROGRAMS = \
	skdump \
	sktest

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
	-version-info $(LIBATASMART_VERSION_INFO) \
	-export-symbols-regex '(^sk_.*)'
libatasmart_la_LIBADD = \
	$(LIBUDEV_LIBS)
libatasmart_la_CFLAGS = \
	$(LIBUDEV_CFLAGS)

BUILT_SOURCES = \
	atasmart.strpool.c

strpool/strpool:
	$(MAKE) -C strpool strpool

atasmart.strpool.c: atasmart.c strpool/strpool
	$(top_builddir)/strpool/strpool $< $@

ACLOCAL_AMFLAGS = -I m4
