SHAREDLIB=libxcb

CSRCS = \
		xcb_conn.c xcb_out.c xcb_in.c xcb_ext.c xcb_xid.c \
		xcb_list.c xcb_util.c xcb_auth.c

DEFINES += PTW32_STATIC_LIB HAVE_GETADDRINFO LIBXCB_DLL

INCLUDELIBFILES = \
 $(MHMAKECONF)\libxau\$(OBJDIR)\libXau.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

XCBPROTO_XCBINCLUDEDIR = ..\xcb-proto\src

XMLFILES := $(notdir $(wildcard $(XCBPROTO_XCBINCLUDEDIR)\*.xml))
XMLFILES := $(filter-out xkb.xml, $(XMLFILES))

EXTSOURCES := $(XMLFILES:%.xml=%.c)
CSRCS += $(EXTSOURCES)

EXTHEADERS = $(XMLFILES:%.xml=%.h)

$(EXTHEADERS) $(EXTSOURCES): c_client.py

load_makefile NORELDBG=1 makefile.srcs
