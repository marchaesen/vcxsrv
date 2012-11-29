# -*- encoding: utf-8 -*-
#
# $Id $
#
# Copyright © 2003 Keith Packard
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation, and that the name of the author(s) not be used in
# advertising or publicity pertaining to distribution of the software without
# specific, written prior permission.  The authors make no
# representations about the suitability of this software for any purpose.  It
# is provided "as is" without express or implied warranty.
#
# THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
# EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
# DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
# TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.
#

INCLUDES=-I${top_builddir}/src -I${top_srcdir}/src -I${top_srcdir} $(WARN_CFLAGS)

TMPL=fcglyphname.tmpl.h
STMPL=${top_srcdir}/fc-glyphname/${TMPL}
TARG=fcglyphname.h

noinst_PROGRAMS=fc-glyphname

GLYPHNAME=zapfdingbats.txt
SGLYPHNAME=${top_srcdir}/fc-glyphname/zapfdingbats.txt

EXTRA_DIST = $(TARG) $(TMPL) $(GLYPHNAME)

if CROSS_COMPILING
$(TARG): $(STMPL) fc-glyphname.c $(SGLYPHNAME)
	@echo Warning: cannot rebuild $(TARG) when cross-compiling
else
$(TARG): $(STMPL) fc-glyphname$(EXEEXT) $(SGLYPHNAME)
	$(AM_V_GEN) $(RM) $(TARG); \
	./fc-glyphname$(EXEEXT) $(SGLYPHNAME) < $(STMPL) > $(TARG).tmp && \
	mv $(TARG).tmp $(TARG)
endif

ALIAS_FILES = fcalias.h fcaliastail.h

BUILT_SOURCES = $(ALIAS_FILES) $(TARG)

$(ALIAS_FILES):
	touch $(ALIAS_FILES)

CLEANFILES = $(ALIAS_FILES)

DISTCLEANFILES = $(TARG)

MAINTAINERCLEANFILES = $(TARG)
