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

Xwayland_built_sources =

if GLAMOR_EGL
Xwayland_SOURCES += 				\
	xwayland-glamor.c
if XV
Xwayland_SOURCES += 				\
	xwayland-glamor-xv.c
endif

glamor_built_sources =				\
	drm-client-protocol.h			\
	drm-protocol.c

Xwayland_built_sources += $(glamor_built_sources)

glamor_lib = $(top_builddir)/glamor/libglamor.la

Xwayland_LDADD += $(GLAMOR_LIBS) $(GBM_LIBS) -lEGL -lGL
Xwayland_DEPENDENCIES = $(glamor_lib) $(XWAYLAND_LIBS)
endif

Xwayland_built_sources +=					\
	relative-pointer-unstable-v1-client-protocol.h		\
	relative-pointer-unstable-v1-protocol.c			\
	pointer-constraints-unstable-v1-client-protocol.h	\
	pointer-constraints-unstable-v1-protocol.c

nodist_Xwayland_SOURCES = $(Xwayland_built_sources)
CLEANFILES = $(Xwayland_built_sources)

EXTRA_DIST = drm.xml


$(Xwayland_SOURCES): $(Xwayland_built_sources)

relink:
	$(AM_V_at)rm -f Xwayland$(EXEEXT) && $(MAKE) Xwayland$(EXEEXT)

relative-pointer-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
relative-pointer-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

pointer-constraints-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
pointer-constraints-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
