noinst_PROGRAMS = makestrs

CC = @CC_FOR_BUILD@
CPPFLAGS = @CPPFLAGS_FOR_BUILD@
CFLAGS = @CFLAGS_FOR_BUILD@
LDFLAGS = @LDFLAGS_FOR_BUILD@

EXTRA_DIST = \
	Shell.ht \
	StrDefs.ct \
	StrDefs.ht \
	string.list

AM_CFLAGS = $(XT_CFLAGS)
makestrs_SOURCES = makestrs.c


# Man page
appmandir = $(APP_MAN_DIR)

appman_PRE = makestrs.man

noinst_DATA = $(appman_PRE:man=@APP_MAN_SUFFIX@)

EXTRA_DIST += $(appman_PRE)
CLEANFILES = $(noinst_DATA)

SUFFIXES = .$(APP_MAN_SUFFIX) .man

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
.man.$(APP_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
