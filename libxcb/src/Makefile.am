lib_LTLIBRARIES = libxcb.la

EXTSOURCES =	xproto.c \
		bigreq.c \
		xc_misc.c

AM_CFLAGS = $(BASE_CFLAGS) $(NEEDED_CFLAGS) $(XDMCP_CFLAGS)
libxcb_la_LIBADD = $(NEEDED_LIBS) $(XDMCP_LIBS)
libxcb_la_SOURCES = \
		xcb_conn.c xcb_out.c xcb_in.c xcb_ext.c xcb_xid.c \
		xcb_list.c xcb_util.c xcb_auth.c c_client.py
nodist_libxcb_la_SOURCES = xproto.c bigreq.c xc_misc.c

# Explanation for -version-info:
# -version-info current:revision:age
# The library supports interface (current-age) to interface current.
# * If you changed the source code at all, increment revision.
# * If you add an interface, increment current and age and set revision to 0.
# * If you change or remove an interface, increment current and set revision
#   and age to 0.
libxcb_la_LDFLAGS = -version-info 2:0:1 -no-undefined @lt_enable_auto_import@

XCB_LIBS = libxcb.la

# FIXME: find a way to autogenerate this from the XML files.

EXTSOURCES += composite.c
if BUILD_COMPOSITE
lib_LTLIBRARIES += libxcb-composite.la
libxcb_composite_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_composite_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_composite_la_SOURCES = composite.c composite.h
endif

EXTSOURCES += damage.c
if BUILD_DAMAGE
lib_LTLIBRARIES += libxcb-damage.la
libxcb_damage_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_damage_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_damage_la_SOURCES = damage.c damage.h
endif

EXTSOURCES += dpms.c
if BUILD_DPMS
lib_LTLIBRARIES += libxcb-dpms.la
libxcb_dpms_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_dpms_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_dpms_la_SOURCES = dpms.c dpms.h
endif

EXTSOURCES += dri2.c
if BUILD_DRI2
lib_LTLIBRARIES += libxcb-dri2.la
libxcb_dri2_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_dri2_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_dri2_la_SOURCES = dri2.c dri2.h
endif

EXTSOURCES += dri3.c
if BUILD_DRI3
lib_LTLIBRARIES += libxcb-dri3.la
libxcb_dri3_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_dri3_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_dri3_la_SOURCES = dri3.c dri3.h
endif

EXTSOURCES += present.c
if BUILD_PRESENT
lib_LTLIBRARIES += libxcb-present.la
libxcb_present_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_present_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_present_la_SOURCES = present.c present.h
endif

EXTSOURCES += glx.c
if BUILD_GLX
lib_LTLIBRARIES += libxcb-glx.la
libxcb_glx_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_glx_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_glx_la_SOURCES = glx.c glx.h
endif

EXTSOURCES += randr.c
if BUILD_RANDR
lib_LTLIBRARIES += libxcb-randr.la
libxcb_randr_la_LDFLAGS = -version-info 1:0:1 -no-undefined @lt_enable_auto_import@
libxcb_randr_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_randr_la_SOURCES = randr.c randr.h
endif

EXTSOURCES += record.c
if BUILD_RECORD
lib_LTLIBRARIES += libxcb-record.la
libxcb_record_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_record_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_record_la_SOURCES = record.c record.h
endif

EXTSOURCES += render.c
if BUILD_RENDER
lib_LTLIBRARIES += libxcb-render.la
libxcb_render_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_render_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_render_la_SOURCES = render.c render.h
endif

EXTSOURCES += res.c
if BUILD_RESOURCE
lib_LTLIBRARIES += libxcb-res.la
libxcb_res_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_res_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_res_la_SOURCES = res.c res.h
endif

EXTSOURCES += screensaver.c
if BUILD_SCREENSAVER
lib_LTLIBRARIES += libxcb-screensaver.la
libxcb_screensaver_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_screensaver_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_screensaver_la_SOURCES = screensaver.c screensaver.h
endif

EXTSOURCES += shape.c
if BUILD_SHAPE
lib_LTLIBRARIES += libxcb-shape.la
libxcb_shape_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_shape_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_shape_la_SOURCES = shape.c shape.h
endif

EXTSOURCES += shm.c
if BUILD_SHM
lib_LTLIBRARIES += libxcb-shm.la
libxcb_shm_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_shm_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_shm_la_SOURCES = shm.c shm.h
endif

EXTSOURCES += sync.c
if BUILD_SYNC
lib_LTLIBRARIES += libxcb-sync.la
libxcb_sync_la_LDFLAGS = -version-info 1:0:0 -no-undefined @lt_enable_auto_import@
libxcb_sync_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_sync_la_SOURCES = sync.c sync.h
endif

EXTSOURCES += xevie.c
if BUILD_XEVIE
lib_LTLIBRARIES += libxcb-xevie.la
libxcb_xevie_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xevie_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xevie_la_SOURCES = xevie.c xevie.h
endif

EXTSOURCES += xf86dri.c
if BUILD_XFREE86_DRI
lib_LTLIBRARIES += libxcb-xf86dri.la
libxcb_xf86dri_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xf86dri_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xf86dri_la_SOURCES = xf86dri.c xf86dri.h
endif

EXTSOURCES += xfixes.c
if BUILD_XFIXES
lib_LTLIBRARIES += libxcb-xfixes.la
libxcb_xfixes_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xfixes_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xfixes_la_SOURCES = xfixes.c xfixes.h
endif

EXTSOURCES += xinerama.c
if BUILD_XINERAMA
lib_LTLIBRARIES += libxcb-xinerama.la
libxcb_xinerama_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xinerama_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xinerama_la_SOURCES = xinerama.c xinerama.h
endif

EXTSOURCES += xinput.c
if BUILD_XINPUT
lib_LTLIBRARIES += libxcb-xinput.la
libxcb_xinput_la_LDFLAGS = -version-info 1:0:1 -no-undefined @lt_enable_auto_import@
libxcb_xinput_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xinput_la_SOURCES = xinput.c xinput.h
endif

EXTSOURCES += xkb.c
if BUILD_XKB
lib_LTLIBRARIES += libxcb-xkb.la
libxcb_xkb_la_LDFLAGS = -version-info 1:0:0 -no-undefined
libxcb_xkb_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xkb_la_SOURCES = xkb.c xkb.h
endif

EXTSOURCES += xprint.c
if BUILD_XPRINT
lib_LTLIBRARIES += libxcb-xprint.la
libxcb_xprint_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xprint_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xprint_la_SOURCES = xprint.c xprint.h
endif

EXTSOURCES += xselinux.c
if BUILD_SELINUX
lib_LTLIBRARIES += libxcb-xselinux.la
libxcb_xselinux_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xselinux_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xselinux_la_SOURCES = xselinux.c xselinux.h
endif

EXTSOURCES += xtest.c
if BUILD_XTEST
lib_LTLIBRARIES += libxcb-xtest.la
libxcb_xtest_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xtest_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xtest_la_SOURCES = xtest.c xtest.h
endif

EXTSOURCES += xv.c
if BUILD_XV
lib_LTLIBRARIES += libxcb-xv.la
libxcb_xv_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xv_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xv_la_SOURCES = xv.c xv.h
endif

EXTSOURCES += xvmc.c
if BUILD_XVMC
lib_LTLIBRARIES += libxcb-xvmc.la
libxcb_xvmc_la_LDFLAGS = -version-info 0:0:0 -no-undefined @lt_enable_auto_import@
libxcb_xvmc_la_LIBADD = $(XCB_LIBS)
nodist_libxcb_xvmc_la_SOURCES = xvmc.c xvmc.h
endif


EXTHEADERS=$(EXTSOURCES:.c=.h)
xcbinclude_HEADERS = xcb.h xcbext.h
if XCB_HAVE_WIN32
xcbinclude_HEADERS += xcb_windefs.h
endif
nodist_xcbinclude_HEADERS = $(EXTHEADERS)
noinst_HEADERS = xcbint.h

BUILT_MAN_PAGES = man/xcb_*
libmandir = $(LIB_MAN_DIR)
libman_DATA = $(BUILT_MAN_PAGES)

BUILT_SOURCES = $(EXTSOURCES) $(BUILT_MAN_PAGES)
CLEANFILES = $(EXTSOURCES) $(EXTHEADERS) $(BUILT_MAN_PAGES)

C_CLIENT_PY_EXTRA_ARGS =
if XCB_SERVERSIDE_SUPPORT
C_CLIENT_PY_EXTRA_ARGS += --server-side
endif

$(EXTSOURCES): c_client.py $(XCBPROTO_XCBINCLUDEDIR)/$(@:.c=.xml)
	$(AM_V_GEN)$(PYTHON) $(srcdir)/c_client.py	-c "$(PACKAGE_STRING)" -l "$(XORG_MAN_PAGE)" \
		-s "$(LIB_MAN_SUFFIX)" -p $(XCBPROTO_XCBPYTHONDIR) \
		$(C_CLIENT_PY_EXTRA_ARGS) \
		$(XCBPROTO_XCBINCLUDEDIR)/$(@:.c=.xml)

$(BUILT_MAN_PAGES): $(EXTSOURCES)
