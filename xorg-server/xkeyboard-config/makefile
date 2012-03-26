ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

SUBDIRS = compat geometry keycodes rules symbols types

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

all: $(SUBDIRS:%=%\all)
