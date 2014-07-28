$(OBJDIR)\fcobjshash.gperf: fcobjshash.gperf.h fcobjs.h
	cp $< $<.c
	$(CPP) -EP -I.. $<.c | \
	python cleangperf.py | \
	gawk '/CUT_OUT_BEGIN/ { no_write=1; next; }; /CUT_OUT_END/ { no_write=0; next; }; { if (!no_write) print; next; };' - > $@.tmp
	mv -f $@.tmp $@
	rm $<.c

$(OBJDIR)\fcobjshash.h: $(OBJDIR)\fcobjshash.gperf
	gperf -m 100 $< > $@


load_makefile NORELDBG=1 ..\fc-case\makefile
load_makefile NORELDBG=1 ..\fc-glyphname\makefile
load_makefile NORELDBG=1 ..\fc-lang\makefile

CSRCS = \
	fcatomic.c \
	fcblanks.c \
	fccache.c \
	fccfg.c \
	fccharset.c \
	fccompat.c \
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
	fcobjs.c \
	fcpat.c \
	fcrange.c \
	fcserialize.c \
	fcstat.c \
	fcstr.c \
	fcweight.c \
	fcxml.c \
	ftglue.c

LIBRARY = libfontconfig

PUBLIC_FILES = \
	..\fontconfig\fontconfig.h \
	fcdeprecate.h \
	..\fontconfig\fcprivate.h

PUBLIC_FT_FILES = \
	..\fontconfig\fcfreetype.h

INCLUDES := .. $(OBJDIR) $(INCLUDES) $(MHMAKECONF)\freetype\include $(MHMAKECONF)\libxml2\include $(MHMAKECONF)\iconv\include

fcalias.h fcaliastail.h: makealias.py $(PUBLIC_FILES)
	python makealias.py . fcalias.h fcaliastail.h $(PUBLIC_FILES)

fcftalias.h fcftaliastail.h: makealias.py $(PUBLIC_FT_FILES)
	python makealias.py . fcftalias.h fcftaliastail.h $(PUBLIC_FT_FILES)

