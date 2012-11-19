AM_CPPFLAGS= \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/X11 \
	-I$(top_builddir)/include \
	-I$(top_builddir)/include/X11 \
	-I$(top_srcdir)/src/xcms \
	-I$(top_srcdir)/src/xkb \
	-I$(top_srcdir)/src/xlibi18n \
	-D_BSD_SOURCE

AM_CFLAGS= \
	$(X11_CFLAGS) \
	$(BIGFONT_CFLAGS) \
	$(CWARNFLAGS)

if XLIB_LOADABLE_I18N

commonlibdir = $(X11_LOCALELIBDIR)/common
commonlib_LTLIBRARIES=xlocale.la
xlocale_la_LDFLAGS = -module -version-number 2:0:0
xlocale_la_LIBADD = $(I18N_MODULE_LIBS)

else

noinst_LTLIBRARIES = libxlocale.la

endif


xlocale_la_SOURCES = \
	lcEuc.c \
	lcJis.c \
	lcSjis.c
	
libxlocale_la_SOURCES = $(xlocale_la_SOURCES)
