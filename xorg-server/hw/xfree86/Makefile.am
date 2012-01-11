
if DRI
DRI_SUBDIR = dri
endif

if DRI2
DRI2_SUBDIR = dri2
endif

if XF86UTILS
XF86UTILS_SUBDIR = utils
endif

XAA_SUBDIR = xaa

if VGAHW
VGAHW_SUBDIR = vgahw
endif

if VBE
VBE_SUBDIR = vbe
endif

if INT10MODULE
INT10_SUBDIR = int10
endif

SUBDIRS = common ddc i2c x86emu $(INT10_SUBDIR) fbdevhw os-support parser \
	  ramdac shadowfb $(VBE_SUBDIR) $(VGAHW_SUBDIR) $(XAA_SUBDIR) \
	  loader dixmods exa modes \
	  $(DRI_SUBDIR) $(DRI2_SUBDIR) $(XF86UTILS_SUBDIR) doc man

DIST_SUBDIRS = common ddc i2c x86emu int10 fbdevhw os-support \
               parser ramdac shadowfb vbe vgahw xaa \
               loader dixmods dri dri2 exa modes \
	       utils doc man

bin_PROGRAMS = Xorg
nodist_Xorg_SOURCES = sdksyms.c

AM_CFLAGS = $(DIX_CFLAGS) @XORG_CFLAGS@
INCLUDES = $(XORG_INCS) -I$(srcdir)/parser -I$(top_srcdir)/miext/cw \
	-I$(srcdir)/ddc -I$(srcdir)/i2c -I$(srcdir)/modes -I$(srcdir)/ramdac

LOCAL_LIBS = \
            $(MAIN_LIB) \
            $(XSERVER_LIBS) \
            loader/libloader.la \
            common/libcommon.la \
            os-support/libxorgos.la \
            parser/libxf86config_internal.la \
            dixmods/libdixmods.la \
            modes/libxf86modes.la \
            ramdac/libramdac.la \
            ddc/libddc.la \
            i2c/libi2c.la \
            dixmods/libxorgxkb.la \
            $(XORG_LIBS) \
            $(top_builddir)/mi/libmi.la \
            $(top_builddir)/os/libos.la
Xorg_LDADD = \
            $(LOCAL_LIBS) \
            $(XORG_SYS_LIBS) \
            $(XSERVER_SYS_LIBS)
Xorg_DEPENDENCIES = $(LOCAL_LIBS)

Xorg_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

BUILT_SOURCES = xorg.conf.example
DISTCLEANFILES = xorg.conf.example
EXTRA_DIST = xorgconf.cpp

if SOLARIS_ASM_INLINE
# Needs to be built before any files are compiled when using Sun compilers
# so in*/out* inline definitions are properly processed.

BUILT_SOURCES += os-support/solaris/solaris-@SOLARIS_INOUT_ARCH@.il

os-support/solaris/solaris-@SOLARIS_INOUT_ARCH@.il:
	cd os-support/solaris ; \
	 $(MAKE) $(AM_MAKEFLAGS) solaris-@SOLARIS_INOUT_ARCH@.il
endif

# do not use $(mkdir_p) if you want automake 1.7 to work
install-data-local:
	mkdir -p $(DESTDIR)$(logdir)


install-exec-local: install-binPROGRAMS
	(cd $(DESTDIR)$(bindir) && rm -f X && ln -s Xorg X)
if INSTALL_SETUID
	chown root $(DESTDIR)$(bindir)/Xorg
	chmod u+s $(DESTDIR)$(bindir)/Xorg
endif

# Use variables from XORG_MANPAGE_SECTIONS and X Server configuration
# Do not include manpages.am as values are not appropriate for rc files
CONF_SUBSTS =	-e 's|__filemansuffix__|$(FILE_MAN_SUFFIX)|g' \
		-e 's|MODULEPATH|$(DEFAULT_MODULE_PATH)|g' \
		-e 's|DEFAULTFONTPATH|$(COMPILEDDEFAULTFONTPATH)|g'

xorg.conf.example: xorgconf.cpp
	$(AM_V_GEN)$(SED) $(CONF_SUBSTS) < $(srcdir)/xorgconf.cpp > $@

relink:
	$(AM_V_at)rm -f Xorg && $(MAKE) Xorg

CLEANFILES = sdksyms.c sdksyms.dep
EXTRA_DIST += sdksyms.sh

sdksyms.dep sdksyms.c: sdksyms.sh
	CPP='$(CPP)' AWK='$(AWK)' $(SHELL) $(srcdir)/sdksyms.sh $(top_srcdir) $(CFLAGS) $(AM_CFLAGS) $(INCLUDES)

SDKSYMS_DEP = sdksyms.dep
include $(SDKSYMS_DEP)
