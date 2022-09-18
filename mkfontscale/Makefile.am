#
#  Copyright 2005  Red Hat, Inc.
#
#  Permission to use, copy, modify, distribute, and sell this software
#  and its documentation for any purpose is hereby granted without fee,
#  provided that the above copyright notice appear in all copies and that
#  both that copyright notice and this permission notice appear in
#  supporting documentation, and that the name of Red Hat not be used in
#  advertising or publicity pertaining to distribution of the software
#  without specific, written prior permission.  Red Hat makes no
#  representations about the suitability of this software for any
#  purpose.  It is provided "as is" without express or implied warranty.
#
#  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
#  NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
#  USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
#  OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.

SUBDIRS = man
bin_PROGRAMS = mkfontscale
bin_SCRIPTS = mkfontdir

CLEANFILES = $(bin_SCRIPTS)
EXTRA_DIST = $(bin_SCRIPTS:=.in)
DIR_SUBSTS = -e 's|@bindir[@]|$(bindir)|g'

mkfontdir: mkfontdir.in
	$(AM_V_GEN)$(SED) $(DIR_SUBSTS) < $(srcdir)/mkfontdir.in > $@

AM_CPPFLAGS = @MKFONTSCALE_CFLAGS@ @X11_CFLAGS@ @ZLIB_CFLAGS@ -D_BSD_SOURCE
AM_CFLAGS = $(CWARNFLAGS)
mkfontscale_LDADD  = @MKFONTSCALE_LIBS@ @ZLIB_LIBS@

mkfontscale_SOURCES = \
	constlist.c \
	constlist.h \
	data.h \
	hash.c \
	hash.h \
	ident.c \
	ident.h \
	list.c \
	list.h \
	mkfontscale.c

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

if LINT
ALL_LINT_FLAGS=$(LINT_FLAGS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
		$(AM_CPPFLAGS) $(CPPFLAGS)

lint:
	$(LINT) $(ALL_LINT_FLAGS) $(mkfontscale_SOURCES)
endif LINT

EXTRA_DIST += README.md
