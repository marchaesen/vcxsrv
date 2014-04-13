bin_PROGRAMS = Xwayland

Xwayland_CFLAGS =				\
	-I$(top_srcdir)/dri3			\
	-DHAVE_DIX_CONFIG_H			\
	$(XWAYLANDMODULES_CFLAGS)		\
	$(DIX_CFLAGS)

Xwayland_SOURCES =				\
	xwayland.c				\
	xwayland-input.c			\
	xwayland-cursor.c			\
	xwayland-shm.c				\
	xwayland-output.c			\
	xwayland-cvt.c				\
	xwayland.h				\
	$(top_srcdir)/Xext/dpmsstubs.c		\
	$(top_srcdir)/Xi/stubs.c		\
	$(top_srcdir)/mi/miinitext.c

Xwayland_LDADD =				\
	$(XWAYLAND_LIBS)			\
	$(XWAYLAND_SYS_LIBS)			\
	$(XSERVER_SYS_LIBS)
Xwayland_DEPENDENCIES = $(XWAYLAND_LIBS)
Xwayland_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)


relink:
	$(AM_V_at)rm -f Xwayland$(EXEEXT) && $(MAKE) Xwayland$(EXEEXT)
