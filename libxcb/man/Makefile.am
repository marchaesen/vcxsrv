
libmandir = $(LIB_MAN_DIR)

libman_PRE =			\
	xcb-examples.man	\
	xcb-requests.man

libman_DATA = $(libman_PRE:man=$(LIB_MAN_SUFFIX))

EXTRA_DIST = $(libman_PRE)

CLEANFILES = $(libman_DATA)

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
SUFFIXES = .$(LIB_MAN_SUFFIX) .man

.man.$(LIB_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
