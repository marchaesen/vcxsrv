BLANKSPY = fc-blanks.py
BLANKS_H = fcblanks.h
TMPL = fcblanks.tmpl.h

$(BLANKS_H): $(TMPL) $(BLANKSPY)
	python $(BLANKSPY) < $< > $(BLANKS_H)

