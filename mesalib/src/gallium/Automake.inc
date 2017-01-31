GALLIUM_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(DEFINES)

# src/gallium/auxiliary must appear before src/gallium/drivers
# because there are stupidly two rbug_context.h files in
# different directories, and which one is included by the
# preprocessor is determined by the ordering of the -I flags.
GALLIUM_DRIVER_CFLAGS = \
	-I$(srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/winsys \
	$(DEFINES) \
	$(VISIBILITY_CFLAGS)

GALLIUM_DRIVER_CXXFLAGS = \
	-I$(srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/winsys \
	$(DEFINES) \
	$(VISIBILITY_CXXFLAGS)

GALLIUM_TARGET_CFLAGS = \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/loader \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/winsys \
	$(DEFINES) \
	$(PTHREAD_CFLAGS) \
	$(LIBDRM_CFLAGS) \
	$(VISIBILITY_CFLAGS)

GALLIUM_COMMON_LIB_DEPS = \
	-lm \
	$(LIBSENSORS_LIBS) \
	$(CLOCK_LIB) \
	$(PTHREAD_LIBS) \
	$(DLOPEN_LIBS)

if HAVE_LIBDRM
GALLIUM_COMMON_LIB_DEPS += \
	$(LIBDRM_LIBS)
endif

GALLIUM_WINSYS_CFLAGS = \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(DEFINES) \
	$(VISIBILITY_CFLAGS)


GALLIUM_PIPE_LOADER_WINSYS_LIBS = \
	$(top_builddir)/src/gallium/winsys/sw/null/libws_null.la \
	$(top_builddir)/src/gallium/winsys/sw/wrapper/libwsw.la

if HAVE_DRISW
GALLIUM_PIPE_LOADER_WINSYS_LIBS += \
	$(top_builddir)/src/gallium/winsys/sw/dri/libswdri.la
endif

if HAVE_DRISW_KMS
GALLIUM_PIPE_LOADER_WINSYS_LIBS += \
	$(top_builddir)/src/gallium/winsys/sw/kms-dri/libswkmsdri.la \
	$(LIBDRM_LIBS)
endif
