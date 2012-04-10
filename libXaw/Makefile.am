SUBDIRS = include src man specs

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =

if BUILD_XAW6

pkgconfig_DATA += xaw6.pc
if PLATFORM_DARWIN
DEFAULT_LIB = libXaw.6.@LIBEXT@
else
DEFAULT_LIB = libXaw6.@LIBEXT@
endif

endif

if BUILD_XAW7

pkgconfig_DATA += xaw7.pc
if PLATFORM_DARWIN
DEFAULT_LIB = libXaw.7.@LIBEXT@
else
DEFAULT_LIB = libXaw7.@LIBEXT@
endif

endif

MAINTAINERCLEANFILES = ChangeLog INSTALL
EXTRA_DIST = 		   \
        autogen.sh	   \
	old-doc/Changelog  \
	old-doc/CHANGES

install-exec-hook:
	cd $(DESTDIR)$(libdir) && rm -f libXaw.@LIBEXT@ && $(LN_S) $(DEFAULT_LIB) libXaw.@LIBEXT@

uninstall-local:
	-rm -f $(DESTDIR)$(libdir)/libXaw.@LIBEXT@


.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
