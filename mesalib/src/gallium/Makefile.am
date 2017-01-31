SUBDIRS =

##
## Gallium auxiliary module
##

SUBDIRS += auxiliary
SUBDIRS += auxiliary/pipe-loader

##
## Gallium pipe drivers and their respective winsys'
##

SUBDIRS += \
	drivers/ddebug \
	drivers/noop \
	drivers/trace \
	drivers/rbug

## freedreno/msm/kgsl
if HAVE_GALLIUM_FREEDRENO
SUBDIRS += drivers/freedreno winsys/freedreno/drm
endif

## i915g/i915
if HAVE_GALLIUM_I915
SUBDIRS += drivers/i915 winsys/i915/drm
endif

## ilo/i965
if HAVE_GALLIUM_ILO
SUBDIRS += drivers/ilo winsys/intel/drm
endif

## nouveau
if HAVE_GALLIUM_NOUVEAU
SUBDIRS += drivers/nouveau winsys/nouveau/drm
endif

## vmwgfx/svga
if HAVE_GALLIUM_SVGA
SUBDIRS += drivers/svga winsys/svga/drm
endif

## r300
if HAVE_GALLIUM_R300
SUBDIRS += drivers/r300
endif

## radeon - linked into r600 and radeonsi
if HAVE_GALLIUM_RADEON_COMMON
SUBDIRS += drivers/radeon
endif

## r600
if HAVE_GALLIUM_R600
SUBDIRS += drivers/r600
endif

## radeonsi
if HAVE_GALLIUM_RADEONSI
SUBDIRS += drivers/radeonsi
SUBDIRS += winsys/amdgpu/drm
endif

## the radeon winsys - linked in by r300, r600 and radeonsi
if NEED_RADEON_DRM_WINSYS
SUBDIRS += winsys/radeon/drm
endif

if HAVE_GALLIUM_ETNAVIV
SUBDIRS += drivers/etnaviv winsys/etnaviv/drm
endif

if HAVE_GALLIUM_IMX
SUBDIRS += drivers/imx winsys/imx/drm
endif

## swrast/softpipe
if HAVE_GALLIUM_SOFTPIPE
SUBDIRS += drivers/softpipe

## swrast/llvmpipe
if HAVE_GALLIUM_LLVMPIPE
SUBDIRS += drivers/llvmpipe
endif
endif

if HAVE_GALLIUM_SWR
SUBDIRS += drivers/swr
endif

## vc4/rpi
if HAVE_GALLIUM_VC4
SUBDIRS += drivers/vc4 winsys/vc4/drm
endif

## virgl
if HAVE_GALLIUM_VIRGL
SUBDIRS += drivers/virgl winsys/virgl/drm winsys/virgl/vtest
endif

## the sw winsys'
SUBDIRS += winsys/sw/null

if NEED_WINSYS_XLIB
SUBDIRS += winsys/sw/xlib
endif

if HAVE_DRISW
SUBDIRS += winsys/sw/dri
endif

if HAVE_DRISW_KMS
SUBDIRS += winsys/sw/kms-dri
endif

SUBDIRS += winsys/sw/wrapper

##
## Don't forget to bundle the remaining (non autotools) winsys'
##

EXTRA_DIST = \
	docs \
	README.portability \
	SConscript \
	winsys/sw/gdi \
	winsys/sw/hgl


##
## Gallium state trackers and their users (targets)
##

## XXX: Rename the conditional once we have a config switch for static/dynamic pipe-drivers
if HAVE_CLOVER
SUBDIRS += targets/pipe-loader
endif

if HAVE_CLOVER
SUBDIRS += state_trackers/clover targets/opencl
endif

if HAVE_DRICOMMON
SUBDIRS += state_trackers/dri targets/dri
endif

if HAVE_GALLIUM_XLIB_GLX
SUBDIRS += state_trackers/glx/xlib targets/libgl-xlib
endif

if HAVE_ST_OMX
SUBDIRS += state_trackers/omx targets/omx
endif

if HAVE_GALLIUM_OSMESA
SUBDIRS += state_trackers/osmesa targets/osmesa
endif

if HAVE_ST_VA
SUBDIRS += state_trackers/va targets/va
endif

if HAVE_ST_VDPAU
SUBDIRS += state_trackers/vdpau targets/vdpau
endif

if HAVE_ST_XA
SUBDIRS += state_trackers/xa targets/xa
endif

if HAVE_ST_XVMC
SUBDIRS += state_trackers/xvmc targets/xvmc
endif

if HAVE_ST_NINE
SUBDIRS += state_trackers/nine targets/d3dadapter9
endif

##
## Don't forget to bundle the remaining (non autotools) state-trackers/targets
##

EXTRA_DIST += \
	include \
	state_trackers/README \
	state_trackers/wgl targets/libgl-gdi \
	targets/graw-gdi targets/graw-null  targets/graw-xlib \
	state_trackers/hgl targets/haiku-softpipe \
	tools


##
## Gallium tests
##

if HAVE_GALLIUM_TESTS
SUBDIRS += \
	tests/trivial \
	tests/unit
endif

EXTRA_DIST += \
	tests/graw \
	tests/python
