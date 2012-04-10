bin_PROGRAMS = cxpm

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/include/X11
AM_CFLAGS = $(CWARNFLAGS) $(XPM_CFLAGS)

cxpm_SOURCES = cxpm.c

if USE_GETTEXT
noinst_DATA = cxpm.po

cxpm.po: $(cxpm_SOURCES:%=$(srcdir)/%)
	$(AM_V_GEN)xgettext -c"L10N_Comments" -d cxpm -n $(cxpm_SOURCES:%=$(srcdir)/%)

CLEANFILES = cxpm.po
endif
