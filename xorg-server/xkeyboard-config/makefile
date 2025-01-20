ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata

$(DESTDIR):
	mkdir -p $(DESTDIR)
	wsl ./build.sh

SUBDIRS = compat geometry keycodes symbols types

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

$(SUBDIRS:%=%\all): $(DESTDIR)

all: $(SUBDIRS:%=%\all)

