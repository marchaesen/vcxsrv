ifneq ($(NORELDBG),1)
$(error NORELDBG should have been set to 1)
endif

TMPL=fccase.tmpl.h
TARG=$(OBJDIR)\fccase.h

CASEFOLDING=CaseFolding.txt

$(TARG): $(TMPL) fc-case.py $(CASEFOLDING)
	$(PYTHON3) fc-case.py --template $(TMPL) $(CASEFOLDING) --output $@

