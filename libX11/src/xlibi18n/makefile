#AM_CFLAGS= \
#	-I$(top_srcdir)/include \
#	-I$(top_srcdir)/include/X11 \
#	-I$(top_builddir)/include \
#	-I$(top_builddir)/include/X11 \
#	-I$(top_srcdir)/src/xcms \
#	-I$(top_srcdir)/src/xkb \
#	-I$(top_srcdir)/src/xlibi18n \
#	-I$(top_srcdir)/src \
#	$(X11_CFLAGS) \
#	$(BIGFONT_CFLAGS) \
#	$(XDMCP_CFLAGS) \
#	-D_BSD_SOURCE \
#	$(XMALLOC_ZERO_CFLAGS)

LIBRARY = libi18n


#
# Dynamic loading code for i18n modules
#
#if XLIB_LOADABLE_I18N
#XI18N_DL_SOURCES = \
#                  XlcDL.c \
#                  XlcSL.c
#else
#
# Static interfaces to input/output methods
#
#IM_LIBS = \
#	${top_builddir}/modules/im/ximcp/libximcp.la

#LC_LIBS = \
#	${top_builddir}/modules/lc/def/libxlcDef.la \
#	${top_builddir}/modules/lc/gen/libxlibi18n.la \
#	${top_builddir}/modules/lc/Utf8/libxlcUTF8Load.la \
#	${top_builddir}/modules/lc/xlocale/libxlocale.la

#OM_LIBS = \
#	${top_builddir}/modules/om/generic/libxomGeneric.la
#endif

#libi18n_la_LIBADD = \
#	 $(IM_LIBS) $(LC_LIBS) $(OM_LIBS)

INCLUDES += ..\..\include\X11
DEFINES += XLOCALELIBDIR="\".\"" LIB11_DLL

CSRCS = \
	$(XI18N_DL_SOURCES) \
	XDefaultIMIF.c \
	XDefaultOMIF.c \
	xim_trans.c\
	ICWrap.c\
	IMWrap.c\
	imKStoUCS.c\
	lcCT.c\
	lcCharSet.c\
	lcConv.c\
	lcDB.c\
	lcDynamic.c\
	lcFile.c\
	lcGeneric.c\
	lcInit.c\
	lcPrTxt.c\
	lcPubWrap.c\
	lcPublic.c\
	lcRM.c\
	lcStd.c\
	lcTxtPr.c\
	lcUTF8.c\
	lcUtil.c\
	lcWrap.c\
	mbWMProps.c\
	mbWrap.c\
	utf8WMProps.c\
	utf8Wrap.c\
	wcWrap.c
	

