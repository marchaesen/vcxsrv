LIBRARY=libglx

DEFINES += GLAPIENTRY=__stdcall _GLAPI_NO_EXPORTS


glapi_sources =					\
	indirect_dispatch.c			\
	indirect_dispatch_swap.c		\
	indirect_reqsize.c			\
	indirect_size_get.c			\
	indirect_table.c			\
	glapi.c					\
	glthread.c				\
	remap.c


CSRCS = \
	$(indirect_sources) \
	$(glapi_sources) \
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

INCLUDES := $(MHMAKECONF)\mesalib\include $(INCLUDES)

