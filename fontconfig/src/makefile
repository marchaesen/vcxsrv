
#INCLUDES = 						\
#	-I$(top_srcdir)					\
#	-I$(top_srcdir)/src				\
#	$(FREETYPE_CFLAGS)				\
#	$(LIBXML2_CFLAGS)				\
#	$(EXPAT_CFLAGS)					\
#	$(WARN_CFLAGS)					\
#	-DFC_CACHEDIR='"$(FC_CACHEDIR)"'                \
#	-DFONTCONFIG_PATH='"$(CONFDIR)"'

load_makefile NORELDBG=1 ..\fc-case\makefile
load_makefile NORELDBG=1 ..\fc-glyphname\makefile
load_makefile NORELDBG=1 ..\fc-lang\makefile

CSRCS = \
	fcatomic.c \
	fcblanks.c \
	fccache.c \
	fccfg.c \
	fccharset.c \
	fcdbg.c \
	fcdefault.c \
	fcdir.c \
	fcformat.c \
	fcfreetype.c \
	fcfs.c \
	fcinit.c \
	fclang.c \
	fclist.c \
	fcmatch.c \
	fcmatrix.c \
	fcname.c \
	fcpat.c \
	fcserialize.c \
	fcstat.c \
	fcstr.c \
	fcxml.c \
	\
	ftglue.c

LIBRARY = libfontconfig

PUBLIC_FILES = \
	..\fontconfig\fontconfig.h \
	fcdeprecate.h \
	..\fontconfig\fcprivate.h
	
PUBLIC_FT_FILES = \
	..\fontconfig\fcfreetype.h

DEFINES += FC_CACHEDIR="""getenv(\"""TEMP\""")""" FC_DEFAULT_FONTS="""WINDOWSFONTDIR"""
INCLUDES := .. $(INCLUDES) $(MHMAKECONF)\freetype\include $(MHMAKECONF)\libxml2\include $(MHMAKECONF)\iconv\include

fcalias.h fcaliastail.h: makealias.py $(PUBLIC_FILES)
	python makealias.py . fcalias.h fcaliastail.h $(PUBLIC_FILES)

fcftalias.h fcftaliastail.h: makealias.py $(PUBLIC_FT_FILES)
	python makealias.py . fcftalias.h fcftaliastail.h $(PUBLIC_FT_FILES)

