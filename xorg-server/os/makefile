LIBRARY=libos

ifeq ($(DEBUG),1)
DEFINES += XSERVER_DTRACE
endif
SECURE_RPC=1
XDMCP=1
NEED_STRLCAT=1
NEED_STRNDUP=1

SECURERPC_SRCS = rpcauth.c
XDMCP_SRCS = xdmcp.c
STRLCAT_SRCS = strlcat.c strlcpy.c
XORG_SRCS = log.c
STRNDUP_SRCS = strndup.c

libos_la_SOURCES = 	\
	WaitFor.c	\
	access.c	\
	auth.c		\
	backtrace.c	\
	client.c	\
	connection.c	\
	io.c		\
	mitauth.c	\
	oscolor.c	\
	osdep.h		\
	osinit.c	\
	utils.c		\
	strcasecmp.c	\
	strcasestr.c	\
	xdmauth.c	\
	xsha1.c		\
	xstrans.c	\
	xprintf.c	\
	reallocarray.c  \
	$(XORG_SRCS)

if SECURE_RPC
libos_la_SOURCES += $(SECURERPC_SRCS)
endif
if XDMCP
libos_la_SOURCES += $(XDMCP_SRCS)
endif

if NEED_STRLCAT
libos_la_SOURCES += $(STRLCAT_SRCS)
endif

if NEED_STRNDUP
libos_la_SOURCES += $(STRNDUP_SRCS)
endif

CSRCS = $(filter %.c,$(libos_la_SOURCES))


