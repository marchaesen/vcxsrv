noinst_LIBRARIES = libglxproxy.a

libglxproxy_a_SOURCES = compsize.c \
                        compsize.h \
                        g_disptab.c \
                        g_disptab.h \
                        global.c \
                        glxcmds.c \
                        glxcmds.h \
                        glxcmdsswap.c \
                        glxcontext.h \
                        glxdrawable.h \
                        glxerror.h \
                        glxext.c \
                        glxext.h \
                        glxfbconfig.c \
                        glxfbconfig.h \
                        glxscreens.c \
                        glxscreens.h \
                        glxserver.h \
                        glxsingle.c \
                        glxsingle.h \
                        glxswap.c \
                        glxswap.h \
                        glxutil.h \
                        glxvendor.c \
                        glxvendor.h \
                        glxvisuals.c \
                        glxvisuals.h \
                        g_renderswap.c \
                        render2swap.c \
                        renderpixswap.c \
                        unpack.h

AM_CFLAGS = \
            @GLX_ARCH_DEFINES@ \
            $(DIX_CFLAGS) \
            -I$(top_srcdir)/hw/dmx \
            -I$(top_srcdir)/include \
            -DHAVE_DMX_CONFIG_H \
            @DMXMODULES_CFLAGS@ \
            -I$(top_srcdir)/hw/xfree86/os-support
