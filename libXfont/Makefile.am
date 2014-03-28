#
#  Copyright © 2003 Keith Packard, Noah Levitt
#
#  Permission to use, copy, modify, distribute, and sell this software and its
#  documentation for any purpose is hereby granted without fee, provided that
#  the above copyright notice appear in all copies and that both that
#  copyright notice and this permission notice appear in supporting
#  documentation, and that the name of Keith Packard not be used in
#  advertising or publicity pertaining to distribution of the software without
#  specific, written prior permission.  Keith Packard makes no
#  representations about the suitability of this software for any purpose.  It
#  is provided "as is" without express or implied warranty.
#
#  KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
#  EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
#  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.

SUBDIRS=doc src

libXfontincludedir = $(includedir)/X11/fonts
libXfontinclude_HEADERS = \
	include/X11/fonts/bdfint.h   \
	include/X11/fonts/bitmap.h   \
	include/X11/fonts/bufio.h    \
	include/X11/fonts/fntfil.h   \
	include/X11/fonts/fntfilio.h \
	include/X11/fonts/fntfilst.h \
	include/X11/fonts/fontencc.h \
	include/X11/fonts/fontmisc.h \
	include/X11/fonts/fontshow.h \
	include/X11/fonts/fontutil.h \
	include/X11/fonts/fontxlfd.h \
	include/X11/fonts/pcf.h      \
	src/FreeType/ft.h	     \
	src/FreeType/ftfuncs.h
nodist_libXfontinclude_HEADERS = \
	include/X11/fonts/fontconf.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xfont.pc

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
