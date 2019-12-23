LIBRARY=librandr
XINERAMA=1

XINERAMA_SRCS = rrxinerama.c

librandr_la_SOURCES =	\
	randr.c		\
	randrstr.h	\
	rrcrtc.c	\
	rrdispatch.c	\
	rrinfo.c	\
	rrlease.c	\
	rrmode.c	\
	rrmonitor.c	\
	rroutput.c	\
	rrpointer.c	\
	rrproperty.c	\
	rrprovider.c    \
	rrproviderproperty.c    \
	rrscreen.c	\
	rrsdispatch.c	\
	rrtransform.h	\
	rrtransform.c

if XINERAMA
librandr_la_SOURCES += ${XINERAMA_SRCS}
endif

CSRCS = $(filter %.c,$(librandr_la_SOURCES))

DEFINES += PIXMAN_API=
