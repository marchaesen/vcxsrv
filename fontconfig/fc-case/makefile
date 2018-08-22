ifneq ($(NORELDBG),1)
$(error NORELDBG should have been set to 1)
endif

INCLUDES+=..\src

TMPL=fccase.tmpl.h
STMPL=${TMPL}
TARG=$(OBJDIR)\fccase.h

TTYAPP=fc-case

CSRCS= \
	fc-case.c

CASEFOLDING=CaseFolding.txt
SCASEFOLDING=CaseFolding.txt

DEFINES += HAVE_CONFIG_H
INCLUDES := .. $(INCLUDES) $(OBJDIR)

$(TARG): $(STMPL) $(OBJDIR)\fc-case.exe $(SCASEFOLDING)
	del -e $(TARG)
	$(OBJDIR)\fc-case.exe $(SCASEFOLDING) < $(STMPL) > $(TARG).tmp
	move $(TARG).tmp $(TARG)


