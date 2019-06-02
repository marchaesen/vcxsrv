EXTRA_DIST = man.xsl

if HAVE_XSLTPROC
miscmandir = $(MISC_MAN_DIR)
miscman_PRE = xkeyboard-config.man
miscman_DATA = $(miscman_PRE:man=@MISC_MAN_SUFFIX@)
CLEANFILES = $(miscman_DATA) $(miscman_PRE)
SUFFIXES = .$(MISC_MAN_SUFFIX) .man
MAN_SUBSTS += -e 's|__xkb_base__|$(xkb_base)|g'

xkeyboard-config.man: $(top_srcdir)/rules/evdev.xml $(srcdir)/man.xsl
	$(XSLTPROC) -nonet $(srcdir)/man.xsl $(top_srcdir)/rules/evdev.xml > $@

.man.$(MISC_MAN_SUFFIX):
	$(SED) $(MAN_SUBSTS) < $< > $@

endif
