LIBRARY=libglx

DEFINES += GLAPIENTRY=__stdcall _GLAPI_NO_EXPORTS PIXMAN_API=


indirect_sources =					\
	indirect_dispatch.c			\
	indirect_dispatch_swap.c		\
	indirect_reqsize.c			\
	indirect_size_get.c			\
	indirect_table.c			\
	glapi.c					\
	remap.c


CSRCS = \
	$(indirect_sources) \
	clientinfo.c \
	createcontext.c \
	extension_string.c \
	indirect_util.c \
	indirect_program.c \
	indirect_texture_compression.c \
        glxcmds.c \
        glxcmdsswap.c \
        glxext.c \
	glxdriswrast.c \
	glxdricommon.c \
        glxscreens.c \
        render2.c \
        render2swap.c \
        renderpix.c \
        renderpixswap.c \
        rensize.c \
        single2.c \
        single2swap.c \
        singlepix.c \
        singlepixswap.c \
        singlesize.c \
        swap_interval.c \
        xfont.c

libglxvnd_la_SOURCES = \
	vndcmds.c \
	vndext.c \
	vndservermapping.c \
	vndservervendor.c

CSRCS += $(libglxvnd_la_SOURCES)

INCLUDES := $(MHMAKECONF)\mesalib\include . ../composite $(INCLUDES) $(MHMAKECONF)\gl\include

load_makefile IS64=0 NORELDBG=1 $(MHMAKECONF)\mesalib\src\mapi\glapi\gen\makefile.srcs
