bin_PROGRAMS = xhost

xhost_SOURCES = xhost.c

AM_CFLAGS = $(CWARNFLAGS) $(XHOST_CFLAGS)
xhost_LDADD = $(XHOST_LIBS)

appman_PRE = xhost.man

appmandir = $(APP_MAN_DIR)

appman_DATA = $(appman_PRE:man=@APP_MAN_SUFFIX@)

EXTRA_DIST = $(appman_PRE) 
MAINTAINERCLEANFILES = ChangeLog INSTALL
CLEANFILES = $(appman_DATA)

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

SUFFIXES = .$(APP_MAN_SUFFIX) .man

.man.$(APP_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
