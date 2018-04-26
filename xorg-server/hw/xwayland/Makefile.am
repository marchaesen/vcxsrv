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
	$(top_srcdir)/Xi/stubs.c		\
	$(top_srcdir)/mi/miinitext.c

Xwayland_LDADD =				\
	$(glamor_lib)				\
	$(XWAYLAND_LIBS)			\
	$(top_builddir)/glx/libglxvnd.la	\
	$(XWAYLAND_SYS_LIBS)			\
	$(top_builddir)/Xext/libXvidmode.la	\
	$(XSERVER_SYS_LIBS)
Xwayland_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

Xwayland_built_sources =

if GLAMOR_EGL
Xwayland_SOURCES += 				\
	xwayland-glamor.c			\
	xwayland-glamor-gbm.c			\
	xwayland-present.c
if XV
Xwayland_SOURCES += 				\
	xwayland-glamor-xv.c
endif

if XWAYLAND_EGLSTREAM
Xwayland_SOURCES +=				\
	xwayland-glamor-eglstream.c
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
	pointer-constraints-unstable-v1-protocol.c		\
	tablet-unstable-v2-client-protocol.h			\
	tablet-unstable-v2-protocol.c				\
	xwayland-keyboard-grab-unstable-v1-protocol.c		\
	xwayland-keyboard-grab-unstable-v1-client-protocol.h	\
	xdg-output-unstable-v1-protocol.c			\
	xdg-output-unstable-v1-client-protocol.h		\
	linux-dmabuf-unstable-v1-client-protocol.h		\
	linux-dmabuf-unstable-v1-protocol.c

if XWAYLAND_EGLSTREAM
Xwayland_built_sources +=					\
	wayland-eglstream-client-protocol.h			\
	wayland-eglstream-protocol.c				\
	wayland-eglstream-controller-client-protocol.h		\
	wayland-eglstream-controller-protocol.c
endif

nodist_Xwayland_SOURCES = $(Xwayland_built_sources)
CLEANFILES = $(Xwayland_built_sources)

EXTRA_DIST = drm.xml

$(Xwayland_SOURCES): $(Xwayland_built_sources)

relink:
	$(AM_V_at)rm -f Xwayland$(EXEEXT) && $(MAKE) Xwayland$(EXEEXT)

relative-pointer-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
relative-pointer-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

pointer-constraints-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
pointer-constraints-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

tablet-unstable-v2-protocol.c: $(WAYLAND_PROTOCOLS_DATADIR)/unstable/tablet/tablet-unstable-v2.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
tablet-unstable-v2-client-protocol.h: $(WAYLAND_PROTOCOLS_DATADIR)/unstable/tablet/tablet-unstable-v2.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

xwayland-keyboard-grab-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
xwayland-keyboard-grab-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
xdg-output-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xdg-output/xdg-output-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
xdg-output-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xdg-output/xdg-output-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

linux-dmabuf-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
linux-dmabuf-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

wayland-eglstream-client-protocol.h : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
wayland-eglstream-controller-client-protocol.h : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream-controller.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

wayland-eglstream-protocol.c : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
wayland-eglstream-controller-protocol.c : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream-controller.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
