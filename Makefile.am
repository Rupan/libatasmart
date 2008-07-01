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

EXTRA_DIST = bootstrap.sh LGPL README
SUBDIRS =

MAINTAINERCLEANFILES =
noinst_DATA =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libatasmart.pc

bin_PROGRAMS = \
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
	atasmart.c atasmart.h
libatasmart_la_LDFLAGS = \
	-version-info 0:0:0
