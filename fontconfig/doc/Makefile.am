# -*- encoding: utf-8 -*-
#
# fontconfig/doc/Makefile.am
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

NULL =
EXTRA_DIST =			\
	$(BUILT_DOCS)		\
	$(DOC_FUNCS_FNCS)	\
	$(DOC_FUNCS_SGML)	\
	$(HTML_DIR)/*		\
	$(SGML_FILES)		\
	$(check_SCRIPTS)	\
	confdir.sgml.in		\
	func.sgml		\
	$(NULL)
MAINTAINERCLEANFILES =		\
	$(DOC_FUNCS_SGML)	\
	$(NULL)
CLEANFILES =			\
	$(BUILT_DOCS)		\
	$(LOCAL_SGML_FILES)	\
	confdir.sgml		\
	func.refs		\
	$(NULL)
BUILT_SOURCES =			\
	$(NULL)
SUFFIXES =	\
	.fncs	\
	.sgml	\
	.txt	\
	.html	\
	$(NULL)
TESTS =				\
	check-missing-doc	\
	$(NULL)
TESTS_ENVIRONMENT=top_srcdir=${top_srcdir} sh
#
DOC2HTML = docbook2html
DOC2TXT  = docbook2txt
DOC2MAN  = docbook2man
DOC2PDF  = docbook2pdf

DOC_FUNCS_FNCS =		\
	fcatomic.fncs		\
	fcblanks.fncs		\
	fccache.fncs		\
	fccharset.fncs		\
	fcconfig.fncs		\
	fcconstant.fncs		\
	fcdircache.fncs		\
	fcfile.fncs		\
	fcfontset.fncs		\
	fcformat.fncs		\
	fcfreetype.fncs		\
	fcinit.fncs		\
	fclangset.fncs		\
	fcmatrix.fncs		\
	fcobjectset.fncs	\
	fcobjecttype.fncs	\
	fcpattern.fncs		\
	fcstring.fncs		\
	fcstrset.fncs		\
	fcvalue.fncs		\
	$(NULL)
SGML_FILES =				\
	$(srcdir)/fontconfig-user.sgml	\
	$(srcdir)/fontconfig-devel.sgml	\
	$(NULL)
LOCAL_SGML_FILES =			\
	local-fontconfig-user.sgml	\
	local-fontconfig-devel.sgml	\
	$(NULL)

DOC_FUNCS_SGML = $(DOC_FUNCS_FNCS:.fncs=.sgml)
BUILT_DOCS =		\
	$(HTML_FILES)	\
	$(PDF_FILES)	\
	$(TXT_FILES)	\
	$(man3_MANS)	\
	$(man5_MANS)	\
	$(NULL)
DOCS_DEPS =			\
	$(DOC_FUNCS_SGML)	\
	confdir.sgml		\
	version.sgml		\
	$(NULL)

TXT_FILES = $(SGML_FILES:.sgml=.txt)
PDF_FILES = $(SGML_FILES:.sgml=.pdf)
HTML_FILES =			\
	fontconfig-user.html	\
	$(NULL)
HTML_DIR = fontconfig-devel
#
noinst_PROGRAMS =	\
	$(NULL)
##
edit_sgml_SOURCES =	\
	edit-sgml.c	\
	$(NULL)
#
check_SCRIPTS =			\
	check-missing-doc	\
	$(NULL)
#
man3_MANS =		\
	$(DOCMAN3)	\
	$(NULL)
man5_MANS =		\
	fonts-conf.5	\
	$(NULL)
#
doc_DATA =		\
	$(TXT_FILES)	\
	$(PDF_FILES)	\
	$(HTML_FILES)	\
	$(NULL)
#
htmldocdir = $(docdir)/$(HTML_DIR)
htmldoc_DATA =		\
	$(NULL)

if USEDOCBOOK
BUILT_SOURCES +=		\
	$(LOCAL_SGML_FILES)	\
	func.refs		\
	$(NULL)
noinst_PROGRAMS +=	\
	edit-sgml	\
	$(NULL)
htmldoc_DATA += $(HTML_DIR)/*

##
if CROSS_COMPILING
.fncs.sgml:
	@echo Warning: cannot rebuild $@ when cross-compiling
else
.fncs.sgml: edit-sgml$(EXEEXT) $(srcdir)/func.sgml
	-rm $@
	$(AM_V_GEN) $(builddir)/edit-sgml$(EXEEXT) $(srcdir)/func.sgml < '$<' > $*.sgml
endif
.sgml.txt: $(DOCS_DEPS)
	-rm $@
	$(AM_V_GEN) $(DOC2TXT) $<
.sgml.pdf: $(DOCS_DEPS)
	-rm $@
	$(AM_V_GEN) $(DOC2PDF) $<
.sgml.html: $(DOCS_DEPS)
	-rm $@
	$(AM_V_GEN) $(DOC2HTML) -u $< > $@
##
fonts-conf.5: local-fontconfig-user.sgml version.sgml confdir.sgml
	-rm $@
	$(AM_V_GEN) $(DOC2MAN) $< && \
	$(RM) manpage.*
##
$(man3_MANS): func.refs
func.refs: local-fontconfig-devel.sgml $(DOCS_DEPS)
	-rm $@
	[ "x$(builddir)" != "x$(srcdir)" ] &&		\
	for f in $(DOC_FUNCS_SGML); do			\
		$(RM) $(builddir)/$$f || :;		\
		$(LN_S) $(srcdir)/$$f $(builddir)/$$f;	\
	done || :
	$(AM_V_GEN) $(DOC2MAN) -o devel-man local-fontconfig-devel.sgml && \
	mv devel-man/manpage.refs func.refs &&	\
	mv devel-man/*.3 . &&			\
	$(RM) devel-man/manpage.* &&		\
	rmdir devel-man
confdir.sgml: $(srcdir)/confdir.sgml.in
	$(AM_V_GEN) sed -e 's,@CONFDIR\@,${CONFDIR},' $< | awk '{if (NR > 1) printf("\n"); printf("%s", $$0);}' > $@
##
$(HTML_DIR)/*: $(HTML_DIR)
$(HTML_DIR): local-fontconfig-devel.sgml $(DOCS_DEPS)
	-rm -r $@
	$(AM_V_GEN) $(DOC2HTML) -V '%use-id-as-filename%' -o $@ local-fontconfig-devel.sgml
local-fontconfig-user.sgml: $(srcdir)/fontconfig-user.sgml
	$(AM_V_GEN) $(LN_S) $< $@
local-fontconfig-devel.sgml: $(srcdir)/fontconfig-devel.sgml
	$(AM_V_GEN) $(LN_S) $< $@
#
all-local: $(BUILT_DOCS) $(HTML_DIR)/*
clean-local:
	-rm -r $(HTML_DIR) devel-man
else
htmldoc_DATA += $(srcdir)/$(HTML_DIR)/*
all-local:
clean-local:
endif
