/*
 * Copyright 2024 Red Hat, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Compatibility stub for Xorg. This responds to just enough of the legacy DRI
 * interface to allow the X server to initialize GLX and enable direct
 * rendering clients. It implements the screen creation hook and provides a
 * (static, unambitious) list of framebuffer configs. It will not create an
 * indirect context; Indirect contexts have been disabled by default since
 * 2014 and would be limited to GL 1.4 in any case, so this is no great loss.
 *
 * If you do want indirect contexts to work, you have options. This stub is
 * new with Mesa 24.1, so one option is to use an older Mesa release stream.
 * Another option is to use an X server that does not need this interface. For
 * Xwayland and Xephyr that's XX.X or newer, and for Xorg drivers using glamor
 * for acceleration that's YY.Y or newer.
 */

#include "util/detect_os.h"

#include "target-helpers/drm_helper.h"
#include "target-helpers/sw_helper.h"

#include "dri_screen.h"

#define DEFINE_LOADER_DRM_ENTRYPOINT(drivername)                          \
PUBLIC const __DRIextension **__driDriverGetExtensions_##drivername(void);       \
PUBLIC const __DRIextension **__driDriverGetExtensions_##drivername(void) \
{                                                                         \
   return galliumdrm_driver_extensions;                                   \
}

PUBLIC const __DRIextension **__driDriverGetExtensions_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_swrast(void)
{
   return galliumsw_driver_extensions;
}

DEFINE_LOADER_DRM_ENTRYPOINT(i915)
DEFINE_LOADER_DRM_ENTRYPOINT(iris)
DEFINE_LOADER_DRM_ENTRYPOINT(crocus)
DEFINE_LOADER_DRM_ENTRYPOINT(nouveau)
DEFINE_LOADER_DRM_ENTRYPOINT(r300)
DEFINE_LOADER_DRM_ENTRYPOINT(r600)
DEFINE_LOADER_DRM_ENTRYPOINT(radeonsi)
DEFINE_LOADER_DRM_ENTRYPOINT(vmwgfx)
DEFINE_LOADER_DRM_ENTRYPOINT(msm)
DEFINE_LOADER_DRM_ENTRYPOINT(kgsl)
DEFINE_LOADER_DRM_ENTRYPOINT(virtio_gpu)
DEFINE_LOADER_DRM_ENTRYPOINT(v3d)
DEFINE_LOADER_DRM_ENTRYPOINT(vc4)
DEFINE_LOADER_DRM_ENTRYPOINT(panfrost)
DEFINE_LOADER_DRM_ENTRYPOINT(panthor)
DEFINE_LOADER_DRM_ENTRYPOINT(asahi)
DEFINE_LOADER_DRM_ENTRYPOINT(etnaviv)
DEFINE_LOADER_DRM_ENTRYPOINT(tegra)
DEFINE_LOADER_DRM_ENTRYPOINT(armada_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(exynos)
DEFINE_LOADER_DRM_ENTRYPOINT(gm12u320)
DEFINE_LOADER_DRM_ENTRYPOINT(hdlcd)
DEFINE_LOADER_DRM_ENTRYPOINT(hx8357d)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9163)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9225)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9341)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9486)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_dcss)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_lcdif)
DEFINE_LOADER_DRM_ENTRYPOINT(ingenic_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(kirin)
DEFINE_LOADER_DRM_ENTRYPOINT(komeda)
DEFINE_LOADER_DRM_ENTRYPOINT(mali_dp)
DEFINE_LOADER_DRM_ENTRYPOINT(mcde)
DEFINE_LOADER_DRM_ENTRYPOINT(mediatek)
DEFINE_LOADER_DRM_ENTRYPOINT(meson)
DEFINE_LOADER_DRM_ENTRYPOINT(mi0283qt)
DEFINE_LOADER_DRM_ENTRYPOINT(mxsfb_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(panel_mipi_dbi)
DEFINE_LOADER_DRM_ENTRYPOINT(pl111)
DEFINE_LOADER_DRM_ENTRYPOINT(rcar_du)
DEFINE_LOADER_DRM_ENTRYPOINT(repaper)
DEFINE_LOADER_DRM_ENTRYPOINT(rockchip)
DEFINE_LOADER_DRM_ENTRYPOINT(rzg2l_du)
DEFINE_LOADER_DRM_ENTRYPOINT(ssd130x)
DEFINE_LOADER_DRM_ENTRYPOINT(st7586)
DEFINE_LOADER_DRM_ENTRYPOINT(st7735r)
DEFINE_LOADER_DRM_ENTRYPOINT(sti)
DEFINE_LOADER_DRM_ENTRYPOINT(stm)
DEFINE_LOADER_DRM_ENTRYPOINT(sun4i_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(udl)
DEFINE_LOADER_DRM_ENTRYPOINT(zynqmp_dpsub)
DEFINE_LOADER_DRM_ENTRYPOINT(lima)
DEFINE_LOADER_DRM_ENTRYPOINT(d3d12)
DEFINE_LOADER_DRM_ENTRYPOINT(zink)
