if BUILD_SXPM

bin_PROGRAMS = sxpm

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = $(CWARNFLAGS) $(SXPM_CFLAGS)

sxpm_SOURCES = sxpm.c

LDADD = $(SXPM_LIBS) $(top_builddir)/src/libXpm.la

if USE_GETTEXT
noinst_DATA = sxpm.po

sxpm.po: $(sxpm_SOURCES:%=$(srcdir)/%)
	$(AM_V_GEN)xgettext -c"L10N_Comments" -d sxpm -n $(sxpm_SOURCES:%=$(srcdir)/%)

CLEANFILES = sxpm.po
endif
endif

EXTRA_DIST = \
	plaid_ext.xpm \
	plaid_mask.xpm \
	plaid.xpm
