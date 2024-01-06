#  Copyright 2005 Red Hat, Inc.
#
#  Permission to use, copy, modify, distribute, and sell this software
#  and its documentation for any purpose is hereby granted without
#  fee, provided that the above copyright notice appear in all copies
#  and that both that copyright notice and this permission notice
#  appear in supporting documentation, and that the name of Red Hat
#  not be used in advertising or publicity pertaining to distribution
#  of the software without specific, written prior permission.  Red
#  Hat makes no representations about the suitability of this software
#  for any purpose.  It is provided "as is" without express or implied
#  warranty.
#
#  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
#  NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
#  OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
#  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
#  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

SUBDIRS = man
bin_PROGRAMS = bdftruncate ucs2any

AM_CFLAGS = $(CWARNFLAGS)

ucs2any_SOURCES = ucs2any.c
bdftruncate_SOURCES = bdftruncate.c

aclocaldir = $(datadir)/aclocal
aclocal_DATA = fontutil.m4

mapfilesdir = @MAPDIR@
mapfiles_DATA = \
	map-ISO8859-1 \
	map-ISO8859-2 \
	map-ISO8859-3 \
	map-ISO8859-4 \
	map-ISO8859-5 \
	map-ISO8859-6 \
	map-ISO8859-7 \
	map-ISO8859-8 \
	map-ISO8859-9 \
	map-ISO8859-10 \
	map-ISO8859-11 \
	map-ISO8859-13 \
	map-ISO8859-14 \
	map-ISO8859-15 \
	map-ISO8859-16 \
	map-JISX0201.1976-0 \
	map-KOI8-R

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = fontutil.pc

CLEANFILES = bdftruncate

EXTRA_DIST = $(mapfiles_DATA) README.md

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
