$(OBJDIR)\fcobjshash.gperf: fcobjshash.gperf.h fcobjs.h
	cp $< $<.c
	$(CPP) -EP -I.. $<.c | \
	python cleangperf.py | \
	gawk '/CUT_OUT_BEGIN/ { no_write=1; next; }; /CUT_OUT_END/ { no_write=0; next; }; { if (!no_write) print; next; };' - > $@.tmp
	mv -f $@.tmp $@
	rm $<.c

$(OBJDIR)\fcobjshash.h: $(OBJDIR)\fcobjshash.gperf
	gperf -m 100 $< > $@
	python converttype.py $@

DEFINES+=FC_TEMPLATEDIR=\"./conf.avail\"

load_makefile NORELDBG=1 ..\fc-case\makefile
load_makefile NORELDBG=1 ..\fc-lang\makefile

CSRCS = \
	fcatomic.c \
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
	fcptrlist.c \
	fchash.c \
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
	ftglue.c \
	gen_uuid.c \
	copy.c \
	parse.c \
	pack.c \
	unpack.c \
	unparse.c

LIBRARY = libfontconfig

INCLUDES := .. uuid $(OBJDIR) $(INCLUDES) $(MHMAKECONF)\freetype\include\freetype $(MHMAKECONF)\freetype\include $(MHMAKECONF)\libxml2\include $(MHMAKECONF)\iconv\include \
	..\fc-lang\$(OBJDIR)\..	..\fc-case\$(OBJDIR)\..

load_makefile IS64=0 NORELDBG=1 makefile.srcs

