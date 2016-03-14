bin_PROGRAMS = Xwayland

Xwayland_CFLAGS =				\
	-I$(top_srcdir)/glamor			\
	-I$(top_srcdir)/dri3			\
	-DHAVE_DIX_CONFIG_H			\
	$(XWAYLANDMODULES_CFLAGS)		\
	$(DIX_CFLAGS)				\
	$(GLAMOR_CFLAGS)			\
	$(GBM_CFLAGS)

Xwayland_SOURCES =				\
	xwayland.c				\
	xwayland-input.c			\
	xwayland-cursor.c			\
	xwayland-shm.c				\
	xwayland-output.c			\
	xwayland-cvt.c				\
	xwayland-vidmode.c			\
	xwayland.h				\
	$(top_srcdir)/Xext/dpmsstubs.c		\
	$(top_srcdir)/Xi/stubs.c		\
	$(top_srcdir)/mi/miinitext.c

Xwayland_LDADD =				\
	$(glamor_lib)				\
	$(XWAYLAND_LIBS)			\
	$(XWAYLAND_SYS_LIBS)			\
	$(top_builddir)/Xext/libXvidmode.la	\
	$(XSERVER_SYS_LIBS)
Xwayland_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)


if GLAMOR_EGL
Xwayland_SOURCES += 				\
	xwayland-glamor.c
if XV
Xwayland_SOURCES += 				\
	xwayland-glamor-xv.c
endif

nodist_Xwayland_SOURCES =			\
	drm-client-protocol.h			\
	drm-protocol.c

CLEANFILES = $(nodist_Xwayland_SOURCES)

xwayland-glamor.c : $(nodist_Xwayland_SOURCES)

glamor_lib = $(top_builddir)/glamor/libglamor.la

Xwayland_LDADD += $(GLAMOR_LIBS) $(GBM_LIBS) -lEGL -lGL
Xwayland_DEPENDENCIES = $(glamor_lib)
endif

EXTRA_DIST = drm.xml


relink:
	$(AM_V_at)rm -f Xwayland$(EXEEXT) && $(MAKE) Xwayland$(EXEEXT)

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
