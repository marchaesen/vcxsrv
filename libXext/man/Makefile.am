libmandir = $(LIB_MAN_DIR)

libman_PRE =					\
	$(all_shadows:=.man)			\
	DBE.man					\
	DPMSCapable.man				\
	DPMSDisable.man				\
	DPMSEnable.man				\
	DPMSForceLevel.man			\
	DPMSGetTimeouts.man			\
	DPMSGetVersion.man			\
	DPMSInfo.man				\
	DPMSQueryExtension.man			\
	DPMSSetTimeouts.man			\
	XShape.man				\
	XShm.man				\
	XcupGetReservedColormapEntries.man	\
	XcupQueryVersion.man			\
	XcupStoreColors.man			\
	XdbeAllocateBackBufferName.man		\
	XdbeBeginIdiom.man			\
	XdbeDeallocateBackBufferName.man	\
	XdbeEndIdiom.man			\
	XdbeFreeVisualInfo.man			\
	XdbeGetBackBufferAttributes.man		\
	XdbeGetVisualInfo.man			\
	XdbeQueryExtension.man			\
	XdbeSwapBuffers.man			\
	Xevi.man				\
	Xmbuf.man

XShape_shadows =				\
	XShapeQueryExtension			\
	XShapeQueryVersion			\
	XShapeCombineRegion			\
	XShapeCombineRectangles			\
	XShapeCombineMask			\
	XShapeCombineShape			\
	XShapeOffsetShape			\
	XShapeQueryExtents			\
	XShapeSelectInput			\
	XShapeInputSelected			\
	XShapeGetRectangles

XShm_shadows =					\
	XShmQueryExtension			\
	XShmQueryVersion			\
	XShmPixmapFormat			\
	XShmAttach				\
	XShmDetach				\
	XShmCreateImage				\
	XShmPutImage				\
	XShmGetImage				\
	XShmCreatePixmap			\
	XShmGetEventBase

Xmbuf_shadows =					\
	XmbufQueryExtension			\
	XmbufGetVersion				\
	XmbufCreateBuffers			\
	XmbufDestroyBuffers			\
	XmbufDisplayBuffers			\
	XmbufGetWindowAttributes		\
	XmbufChangeWindowAttributes		\
	XmbufGetBufferAttributes		\
	XmbufChangeBufferAttributes		\
	XmbufGetScreenInfo			\
	XmbufCreateStereoWindow

Xevi_shadows =					\
	XeviQueryVersion			\
	XeviGetVisualInfo			\
	XeviQueryExtension

all_shadows = $(XShape_shadows) $(XShm_shadows) $(Xmbuf_shadows) $(Xevi_shadows)

libman_DATA = $(libman_PRE:man=@LIB_MAN_SUFFIX@)

EXTRA_DIST  = $(libman_PRE)

CLEANFILES  = $(libman_DATA)

SUFFIXES = .$(LIB_MAN_SUFFIX) .man

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
.man.$(LIB_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
