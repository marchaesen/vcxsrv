
include Makefile.sources

vulkan_includedir = $(includedir)/vulkan

noinst_LTLIBRARIES = libvulkan_wsi.la

AM_CPPFLAGS = \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/include

AM_CFLAGS = \
	$(VISIBILITY_CFLAGS)

VULKAN_LIB_DEPS =

VULKAN_WSI_SOURCES = \
	$(VULKAN_WSI_FILES)

if HAVE_PLATFORM_X11
AM_CPPFLAGS += \
	$(XCB_DRI3_CFLAGS) \
	-DVK_USE_PLATFORM_XCB_KHR \
	-DVK_USE_PLATFORM_XLIB_KHR

VULKAN_WSI_SOURCES += $(VULKAN_WSI_X11_FILES)

# FIXME: Use pkg-config for X11-xcb ldflags.
VULKAN_LIB_DEPS += $(XCB_DRI3_LIBS) -lX11-xcb
endif

if HAVE_PLATFORM_WAYLAND
AM_CPPFLAGS += \
	-I$(top_builddir)/src/egl/wayland/wayland-drm \
	-I$(top_srcdir)/src/egl/wayland/wayland-drm \
	$(WAYLAND_CFLAGS) \
	-DVK_USE_PLATFORM_WAYLAND_KHR

VULKAN_WSI_SOURCES += $(VULKAN_WSI_WAYLAND_FILES)

VULKAN_LIB_DEPS += \
	$(top_builddir)/src/egl/wayland/wayland-drm/libwayland-drm.la \
	$(WAYLAND_LIBS)
endif

libvulkan_wsi_la_SOURCES = $(VULKAN_WSI_SOURCES)
