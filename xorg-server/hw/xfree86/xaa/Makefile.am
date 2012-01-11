LSB_FIRST = l-xaaBitmap.c l-xaaStipple.c l-xaaTEGlyph.c
LSB_3_FIRST = l3-xaaBitmap.c l3-xaaStipple.c
MSB_FIRST = m-xaaBitmap.c m-xaaStipple.c m-xaaTEGlyph.c
MSB_3_FIRST = m3-xaaBitmap.c m3-xaaStipple.c
LSB_FIXED = lf-xaaBitmap.c lf-xaaStipple.c lf-xaaTEGlyph.c
LSB_3_FIXED = lf3-xaaBitmap.c lf3-xaaStipple.c
MSB_FIXED = mf-xaaBitmap.c mf-xaaStipple.c mf-xaaTEGlyph.c
MSB_3_FIXED = mf3-xaaBitmap.c mf3-xaaStipple.c
POLYSEG = s-xaaLine.c s-xaaDashLine.c

if XAA

libxaa_la_LDFLAGS = -module -avoid-version
if COMPOSITE
libxaa_la_LIBADD = $(top_builddir)/miext/cw/libcw.la
endif

module_LTLIBRARIES = libxaa.la
libxaa_la_SOURCES = xaaInit.c xaaGC.c xaaInitAccel.c xaaFallback.c \
                   xaaBitBlt.c xaaCpyArea.c xaaGCmisc.c xaaCpyWin.c \
                   xaaCpyPlane.c xaaFillRect.c xaaTEText.c xaaNonTEText.c \
                   xaaPCache.c xaaSpans.c xaaROP.c xaaImage.c \
                   xaaRect.c xaaLineMisc.c xaaBitOrder.c \
                   xaaFillPoly.c xaaWideLine.c xaaTables.c xaaFillArc.c \
                   xaaLine.c xaaDashLine.c xaaOverlay.c xaaOffscreen.c \
                   xaaOverlayDF.c xaaStateChange.c xaaPict.c $(POLYSEG) \
                   $(LSB_FIRST) $(MSB_FIRST) $(LSB_FIXED) $(MSB_FIXED) \
                   $(LSB_3_FIRST) $(MSB_3_FIRST) $(LSB_3_FIXED) $(MSB_3_FIXED)
${POLYSEG}:
	$(AM_V_GEN)echo "#define POLYSEGMENT" > $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:s-%=%}"' >> $@
${LSB_FIRST}:
	$(AM_V_GEN)echo "#define LSBFIRST" > $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:l-%=%}"' >> $@
${LSB_3_FIRST}:
	$(AM_V_GEN)echo "#define LSBFIRST" > $@
	$(AM_V_GEN)echo "#define TRIPLE_BITS" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:l3-%=%}"' >> $@
${LSB_FIXED}:
	$(AM_V_GEN)echo "#define LSBFIRST" > $@
	$(AM_V_GEN)echo "#define FIXEDBASE" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:lf-%=%}"' >> $@
${LSB_3_FIXED}:
	$(AM_V_GEN)echo "#define LSBFIRST" > $@
	$(AM_V_GEN)echo "#define TRIPLE_BITS" >> $@
	$(AM_V_GEN)echo "#define FIXEDBASE" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:lf3-%=%}"' >> $@
${MSB_FIRST}:
	$(AM_V_GEN)echo "#define MSBFIRST" > $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:m-%=%}"' >> $@
${MSB_3_FIRST}:
	$(AM_V_GEN)echo "#define MSBFIRST" > $@
	$(AM_V_GEN)echo "#define TRIPLE_BITS" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:m3-%=%}"' >> $@
${MSB_FIXED}:
	$(AM_V_GEN)echo "#define MSBFIRST" > $@
	$(AM_V_GEN)echo "#define FIXEDBASE" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:mf-%=%}"' >> $@
${MSB_3_FIXED}:
	$(AM_V_GEN)echo "#define MSBFIRST" > $@
	$(AM_V_GEN)echo "#define TRIPLE_BITS" >> $@
	$(AM_V_GEN)echo "#define FIXEDBASE" >> $@
	$(AM_V_GEN)echo '#include "$(srcdir)/${@:mf3-%=%}"' >> $@

endif # XAA

DISTCLEANFILES = $(POLYSEG) \
	$(LSB_FIRST) $(LSB_FIXED) $(MSB_FIRST) $(MSB_FIXED) \
	$(LSB_3_FIRST) $(LSB_3_FIXED) $(MSB_3_FIRST) $(MSB_3_FIXED)

sdk_HEADERS = xaa.h xaalocal.h xaarop.h
EXTRA_DIST = xaacexp.h xaawrap.h xaaLine.c xaaDashLine.c \
             xaaStipple.c xaaTEGlyph.c xaaNonTEGlyph.c xaaBitmap.c \
             XAA.HOWTO

INCLUDES = $(XORG_INCS) -I$(srcdir)/../../../miext/cw

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)
