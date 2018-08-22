SHAREDLIB=libxcb

CSRCS = \
		xcb_conn.c xcb_out.c xcb_in.c xcb_ext.c xcb_xid.c \
		xcb_list.c xcb_util.c xcb_auth.c \
		icccm.c xcb_aux.c ewmh.c xcb_image.c

DEFINES += PTW32_STATIC_LIB HAVE_GETADDRINFO LIBXCB_DLL

INCLUDELIBFILES = \
 $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\xcb-util-errors\$(OBJDIR)\libxcb-errors.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

LINKLIBS += $(PTHREADLIB)

XCBPROTO_XCBINCLUDEDIR = ..\xcb-proto\src

XMLFILES := $(notdir $(wildcard $(XCBPROTO_XCBINCLUDEDIR)\*.xml))
XMLFILES := $(filter-out xkb.xml, $(XMLFILES))
XMLFILES := $(filter-out xprint.xml, $(XMLFILES))

EXTSOURCES := $(XMLFILES:%.xml=%.c)
CSRCS += $(EXTSOURCES)

EXTHEADERS = $(XMLFILES:%.xml=%.h)

$(EXTHEADERS) $(EXTSOURCES): c_client.py

load_makefile IS64=0 NORELDBG=1 makefile.srcs
