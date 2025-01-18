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

#include "main/glconfig.h"
#include "main/mtypes.h"
#include <GL/internal/dri_interface.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "gbm/main/gbm.h"
#include "drm-uapi/drm_fourcc.h"

#define EGL_PLATFORM_GBM_MESA             0x31D7

/* avoid needing X11 headers */
#define GLX_NONE 0x8000
#define GLX_DONT_CARE 0xFFFFFFFF

#define CONFIG_ZS(color, zs) \
   { \
      .color_format = color, \
      .zs_format = zs, \
   }

#define CONFIG(color) \
   CONFIG_ZS(color, PIPE_FORMAT_S8_UINT), \
   CONFIG_ZS(color, PIPE_FORMAT_Z24_UNORM_S8_UINT), \
   CONFIG_ZS(color, PIPE_FORMAT_Z24X8_UNORM), \
   CONFIG_ZS(color, PIPE_FORMAT_Z16_UNORM), \
   CONFIG_ZS(color, PIPE_FORMAT_NONE) \

/*
 * (copy of a comment in dri_screen.c:dri_fill_in_modes())
 *
 * The 32-bit RGBA format must not precede the 32-bit BGRA format.
 * Likewise for RGBX and BGRX.  Otherwise, the GLX client and the GLX
 * server may disagree on which format the GLXFBConfig represents,
 * resulting in swapped color channels.
 *
 * The problem, as of 2017-05-30:
 * When matching a GLXFBConfig to a __DRIconfig, GLX ignores the channel
 * order and chooses the first __DRIconfig with the expected channel
 * sizes. Specifically, GLX compares the GLXFBConfig's and __DRIconfig's
 * __DRI_ATTRIB_{CHANNEL}_SIZE but ignores __DRI_ATTRIB_{CHANNEL}_MASK.
 *
 * EGL does not suffer from this problem. It correctly compares the
 * channel masks when matching EGLConfig to __DRIconfig.
 */

static const struct gl_config drilConfigs[] = {
   CONFIG(PIPE_FORMAT_B8G8R8A8_UNORM),
   CONFIG(PIPE_FORMAT_B8G8R8X8_UNORM),
   CONFIG(PIPE_FORMAT_R8G8B8A8_UNORM),
   CONFIG(PIPE_FORMAT_R8G8B8X8_UNORM),
   CONFIG(PIPE_FORMAT_B10G10R10A2_UNORM),
   CONFIG(PIPE_FORMAT_B10G10R10X2_UNORM),
   CONFIG(PIPE_FORMAT_R10G10B10A2_UNORM),
   CONFIG(PIPE_FORMAT_R10G10B10X2_UNORM),
   CONFIG(PIPE_FORMAT_B5G6R5_UNORM),
   CONFIG(PIPE_FORMAT_B5G5R5A1_UNORM),
   CONFIG(PIPE_FORMAT_B5G5R5X1_UNORM),
   CONFIG(PIPE_FORMAT_B4G4R4A4_UNORM),
   CONFIG(PIPE_FORMAT_B4G4R4X4_UNORM),
   CONFIG(PIPE_FORMAT_R5G6B5_UNORM),
   CONFIG(PIPE_FORMAT_R5G5B5A1_UNORM),
   CONFIG(PIPE_FORMAT_R5G5B5X1_UNORM),
   CONFIG(PIPE_FORMAT_R4G4B4A4_UNORM),
   CONFIG(PIPE_FORMAT_R4G4B4X4_UNORM),
};

#define RGB UTIL_FORMAT_COLORSPACE_RGB
#define RED 0
#define GREEN 1
#define BLUE 2
#define ALPHA 3
#define ZS UTIL_FORMAT_COLORSPACE_ZS
#define DEPTH 0
#define STENCIL 1

#define CASE(ATTRIB, VALUE) \
   case __DRI_ATTRIB_ ## ATTRIB : \
      *value = VALUE; \
      break;

#define SIZE(f, cs, chan)  (f ? util_format_get_component_bits(f, cs, chan) : 0)
#define SHIFT(f, cs, chan) (f ? util_format_get_component_shift(f, cs, chan) : 0)
#define MASK(f, cs, chan) \
   (((1 << SIZE(f, cs, chan)) - 1) << SHIFT(f, cs, chan))

static int
drilIndexConfigAttrib(const __DRIconfig *_config, int index,
                      unsigned int *attrib, unsigned int *value)
{
   struct gl_config *config = (void *)_config;
   enum pipe_format color_format = config->color_format;
   enum pipe_format zs_format = config->zs_format;
   enum pipe_format accum_format = config->accum_format;

   if (index >= __DRI_ATTRIB_MAX)
      return 0;

   switch (index) {
      case __DRI_ATTRIB_SAMPLE_BUFFERS:
         *value = !!config->samples;
         break;

      case __DRI_ATTRIB_BUFFER_SIZE: {
         unsigned int red = 0, green = 0, blue = 0, alpha = 0;
         drilIndexConfigAttrib(_config, __DRI_ATTRIB_RED_SIZE, attrib, &red);
         drilIndexConfigAttrib(_config, __DRI_ATTRIB_GREEN_SIZE, attrib, &green);
         drilIndexConfigAttrib(_config, __DRI_ATTRIB_BLUE_SIZE, attrib, &blue);
         drilIndexConfigAttrib(_config, __DRI_ATTRIB_ALPHA_SIZE, attrib, &alpha);
         *value = red + green + blue + alpha;
         break;
      }

      CASE(RED_SIZE,          SIZE(color_format, RGB, 0));
      CASE(GREEN_SIZE,        SIZE(color_format, RGB, 1));
      CASE(BLUE_SIZE,         SIZE(color_format, RGB, 2));
      CASE(ALPHA_SIZE,        SIZE(color_format, RGB, 3));
      CASE(DEPTH_SIZE,        SIZE(zs_format,    ZS,  0));
      CASE(STENCIL_SIZE,      SIZE(zs_format,    ZS,  1));
      CASE(ACCUM_RED_SIZE,    SIZE(accum_format, RGB, 0));
      CASE(ACCUM_GREEN_SIZE,  SIZE(accum_format, RGB, 1));
      CASE(ACCUM_BLUE_SIZE,   SIZE(accum_format, RGB, 2));
      CASE(ACCUM_ALPHA_SIZE,  SIZE(accum_format, RGB, 3));

      CASE(RENDER_TYPE, __DRI_ATTRIB_RGBA_BIT);
      CASE(CONFORMANT, GL_TRUE);
      CASE(DOUBLE_BUFFER, config->doubleBufferMode);
      CASE(SAMPLES, config->samples);
      CASE(FRAMEBUFFER_SRGB_CAPABLE, config->sRGBCapable);

      CASE(TRANSPARENT_TYPE,        GLX_NONE);
      CASE(TRANSPARENT_INDEX_VALUE, GLX_NONE);
      CASE(TRANSPARENT_RED_VALUE,   GLX_DONT_CARE);
      CASE(TRANSPARENT_GREEN_VALUE, GLX_DONT_CARE);
      CASE(TRANSPARENT_BLUE_VALUE,  GLX_DONT_CARE);
      CASE(TRANSPARENT_ALPHA_VALUE, GLX_DONT_CARE);

      CASE(RED_MASK,   MASK(color_format, RGB, 0));
      CASE(GREEN_MASK, MASK(color_format, RGB, 1));
      CASE(BLUE_MASK,  MASK(color_format, RGB, 2));
      CASE(ALPHA_MASK, MASK(color_format, RGB, 3));

      CASE(SWAP_METHOD, __DRI_ATTRIB_SWAP_UNDEFINED);
      CASE(MAX_SWAP_INTERVAL, INT_MAX);
      CASE(BIND_TO_TEXTURE_RGB, GL_TRUE);
      CASE(BIND_TO_TEXTURE_RGBA, GL_TRUE);
      CASE(BIND_TO_TEXTURE_TARGETS,
           __DRI_ATTRIB_TEXTURE_1D_BIT |
           __DRI_ATTRIB_TEXTURE_2D_BIT |
           __DRI_ATTRIB_TEXTURE_RECTANGLE_BIT);
      CASE(YINVERTED, GL_TRUE);

      CASE(RED_SHIFT,   SHIFT(color_format, RGB, 0));
      CASE(GREEN_SHIFT, SHIFT(color_format, RGB, 1));
      CASE(BLUE_SHIFT,  SHIFT(color_format, RGB, 2));
      CASE(ALPHA_SHIFT, SHIFT(color_format, RGB, 3));

      default:
         *value = 0;
         break;
   }

   *attrib = index;
   return 1;
}

static void
drilDestroyScreen(__DRIscreen *screen)
{
   /* At the moment this is just the bounce table for the configs */
   free(screen);
}

static const __DRI2flushControlExtension dri2FlushControlExtension = {
   .base = { __DRI2_FLUSH_CONTROL, 1 }
};

static void
dril_set_tex_buffer2(__DRIcontext *pDRICtx, GLint target,
                    GLint format, __DRIdrawable *dPriv)
{
}

static void
dril_set_tex_buffer(__DRIcontext *pDRICtx, GLint target,
                   __DRIdrawable *dPriv)
{
}

const __DRItexBufferExtension driTexBufferExtension = {
   .base = { __DRI_TEX_BUFFER, 2 },

   .setTexBuffer       = dril_set_tex_buffer,
   .setTexBuffer2      = dril_set_tex_buffer2,
   .releaseTexBuffer   = NULL,
};

static const __DRIrobustnessExtension dri2Robustness = {
   .base = { __DRI2_ROBUSTNESS, 1 }
};

static const __DRIextension *dril_extensions[] = {
   &dri2FlushControlExtension.base,
   &driTexBufferExtension.base,
   &dri2Robustness.base,
   NULL
};

/* This has to return a pointer to NULL, not just NULL */
static const __DRIextension **
drilGetExtensions(__DRIscreen *screen)
{
   return (void*)&dril_extensions;
}

static __DRIcontext *
drilCreateContextAttribs(__DRIscreen *psp, int api,
                        const __DRIconfig *config,
                        __DRIcontext *shared,
                        unsigned num_attribs,
                        const uint32_t *attribs,
                        unsigned *error,
                        void *data)
{
   return NULL;
}

static __DRIcontext *
drilCreateNewContextForAPI(__DRIscreen *screen, int api,
                          const __DRIconfig *config,
                          __DRIcontext *shared, void *data)
{
   return NULL;
}

static __DRIcontext *
drilCreateNewContext(__DRIscreen *screen, const __DRIconfig *config,
                    __DRIcontext *shared, void *data)
{
   return NULL;
}

static void
drilDestroyDrawable(__DRIdrawable *pdp)
{
}

static const __DRIcoreExtension drilCoreExtension = {
   .base = { __DRI_CORE, 1 },

   .destroyScreen       = drilDestroyScreen,
   .getExtensions       = drilGetExtensions,
   .getConfigAttrib     = NULL, // XXX not actually used!
   .indexConfigAttrib   = drilIndexConfigAttrib,
   .destroyDrawable     = drilDestroyDrawable,
   .createNewContext    = drilCreateNewContext,
};

static int drilBindContext(__DRIcontext *pcp,
                          __DRIdrawable *pdp,
                          __DRIdrawable *prp)
{
   return 0; // Success
}

static int drilUnbindContext(__DRIcontext *pcp)
{
   return 0; // Success
}

static __DRIdrawable *
drilCreateNewDrawable(__DRIscreen *psp,
                     const __DRIconfig *config,
                     void *data)
{
   return NULL;
}


static enum pipe_format
fourcc_to_pipe_format(int fourcc)
{
   switch (fourcc) {
   case DRM_FORMAT_RGB565: return PIPE_FORMAT_B5G6R5_UNORM;
   case DRM_FORMAT_XRGB8888: return PIPE_FORMAT_BGRX8888_UNORM;
   case DRM_FORMAT_ARGB8888: return PIPE_FORMAT_BGRA8888_UNORM;
   case DRM_FORMAT_ABGR8888: return PIPE_FORMAT_RGBA8888_UNORM;
   case DRM_FORMAT_XBGR8888: return PIPE_FORMAT_RGBX8888_UNORM;
   case DRM_FORMAT_XRGB2101010: return PIPE_FORMAT_B10G10R10X2_UNORM;
   case DRM_FORMAT_ARGB2101010: return PIPE_FORMAT_B10G10R10A2_UNORM;
   case DRM_FORMAT_XBGR2101010: return PIPE_FORMAT_R10G10B10X2_UNORM;
   case DRM_FORMAT_ABGR2101010: return PIPE_FORMAT_R10G10B10A2_UNORM;
   case DRM_FORMAT_XBGR16161616F: return PIPE_FORMAT_R16G16B16A16_FLOAT;
   case DRM_FORMAT_ABGR16161616F: return PIPE_FORMAT_R16G16B16X16_FLOAT;
   case DRM_FORMAT_ARGB1555: return PIPE_FORMAT_B5G5R5A1_UNORM;
   case DRM_FORMAT_ABGR1555: return PIPE_FORMAT_R5G5B5A1_UNORM;
   case DRM_FORMAT_ARGB4444: return PIPE_FORMAT_B4G4R4A4_UNORM;
   case DRM_FORMAT_ABGR4444: return PIPE_FORMAT_R4G4B4A4_UNORM;
   default:                             return PIPE_FORMAT_NONE;
   }
}

static unsigned
add_srgb_config(struct gl_config **configs, unsigned c, enum pipe_format last_pformat, unsigned last_start)
{
   enum pipe_format srgb = util_format_srgb(last_pformat);
   if (!srgb)
      return c;
   unsigned end = c;
   for (unsigned j = last_start; j < end; j++) {
      configs[c] = mem_dup(configs[j], sizeof(drilConfigs[j]));

      struct gl_config *cfg = configs[c++];
      cfg->color_format = srgb;
      cfg->sRGBCapable = 1;
   }
   return c;
}

/* DRI2 awfulness */
static const __DRIconfig **
init_dri2_configs(int fd)
{
   void *egl = NULL;
   struct gl_config **configs = NULL;
   unsigned c = 0;
   enum pipe_format last_pformat = 0;
   unsigned last_start = 0;

   /* dlopen/dlsym to avoid linkage */
   egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
   if (!egl)
      return false;

   void * (*peglGetProcAddress)(const char *) = dlsym(egl, "eglGetProcAddress");
   EGLDisplay (*peglGetPlatformDisplayEXT)(EGLenum, void *, const EGLint *) = peglGetProcAddress("eglGetPlatformDisplayEXT");
   EGLBoolean (*peglInitialize)(EGLDisplay, int*, int*) = peglGetProcAddress("eglInitialize");
   EGLBoolean (*peglTerminate)(EGLDisplay) = peglGetProcAddress("eglTerminate");
   EGLBoolean (*peglGetConfigs)(EGLDisplay, EGLConfig*, EGLint, EGLint*) = peglGetProcAddress("eglGetConfigs");
   EGLBoolean (*peglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *) = peglGetProcAddress("eglGetConfigAttrib");
   const char *(*peglQueryString)(EGLDisplay, EGLint) = peglGetProcAddress("eglQueryString");

   struct gbm_device *gbm = NULL;
   if (fd != -1) {
      /* try opening GBM for hardware driver info */
      gbm = gbm_create_device(fd);
      if (!gbm)
         goto out;
   }

   EGLDisplay dpy = peglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, gbm ? gbm : EGL_DEFAULT_DISPLAY, NULL);
   if (!dpy)
      goto out_gbm;
   int maj, min;
   if (!peglInitialize(dpy, &maj, &min))
      goto out_gbm;

   const char *egl_extension_list = peglQueryString(dpy, EGL_EXTENSIONS);
   bool has_srgb = strstr(egl_extension_list, "EGL_KHR_gl_colorspace");

   int num_configs = 0;
   EGLConfig *eglconfigs = NULL;
   if (!peglGetConfigs(dpy, NULL, 0, &num_configs))
      goto out_egl;
   eglconfigs = malloc(sizeof(EGLConfig) * num_configs);
   /* overestimate: num_configs * doubleBuffer * sRGB + NULL */
   configs = calloc(num_configs * 2 * 2 + 1, sizeof(struct gl_config));
   if (!peglGetConfigs(dpy, eglconfigs, num_configs, &num_configs))
      goto out_egl;

   for (unsigned i = 0; i < num_configs; i++) {
      /* verify that this is the right format */
      EGLint format, depth, stencil, samples;

      if (!peglGetConfigAttrib(dpy, eglconfigs[i], EGL_NATIVE_VISUAL_ID, &format) ||
          !peglGetConfigAttrib(dpy, eglconfigs[i], EGL_DEPTH_SIZE, &depth) ||
          !peglGetConfigAttrib(dpy, eglconfigs[i], EGL_STENCIL_SIZE, &stencil) ||
          !peglGetConfigAttrib(dpy, eglconfigs[i], EGL_SAMPLES, &samples))
         continue;

      enum pipe_format pformat = fourcc_to_pipe_format(format);

      /* srgb configs go after base configs */
      if (last_pformat && has_srgb && pformat != last_pformat)
         c = add_srgb_config(configs, c, last_pformat, last_start);
      /* tracking for the number of srgb configs to create */
      if (pformat != last_pformat)
         last_start = c;

      for (unsigned j = 0; j < ARRAY_SIZE(drilConfigs); j++) {
         unsigned depth_size = SIZE(drilConfigs[j].zs_format, ZS, 0);
         unsigned stencil_size = SIZE(drilConfigs[j].zs_format, ZS, 1);
         /* only copy supported configs */
         if (pformat != drilConfigs[j].color_format || depth != depth_size || stencil != stencil_size)
            continue;

         /* always create single-buffered and double-buffered */
         for (unsigned k = 0; k < 2; k++) {
            configs[c] = mem_dup(&drilConfigs[j], sizeof(drilConfigs[j]));

            struct gl_config *cfg = configs[c++];
            cfg->samples = samples;
            cfg->doubleBufferMode = k;
         }
         break;
      }
      last_pformat = pformat;
   }
   /* check last format for srgbness too */
   if (c && has_srgb)
      c = add_srgb_config(configs, c, last_pformat, last_start);
out_egl:
   free(eglconfigs);
   /* don't forget cleanup */
   peglTerminate(dpy);

out_gbm:
   if (gbm)
      gbm_device_destroy(gbm);
out:
   dlclose(egl);
   if (c)
      return (void*)configs;
   free(configs);
   return NULL;
}

static __DRIscreen *
drilCreateNewScreen(int scrn, int fd,
                    const __DRIextension **loader_extensions,
                    const __DRIextension **driver_extensions,
                    const __DRIconfig ***driver_configs, void *data)
{
   const __DRIconfig **configs = init_dri2_configs(fd);
   if (!configs && fd == -1) {
      // otherwise set configs to point to our config list
      configs = calloc(ARRAY_SIZE(drilConfigs) * 2 + 1, sizeof(void *));
      int c = 0;
      for (int i = 0; i < ARRAY_SIZE(drilConfigs); i++) {
         /* create normal config */
         configs[c++] = mem_dup(&drilConfigs[i], sizeof(drilConfigs[i]));

         /* create double-buffered config */
         configs[c] = mem_dup(&drilConfigs[i], sizeof(drilConfigs[i]));
         struct gl_config *cfg = (void*)configs[c++];
         cfg->doubleBufferMode = 1;
      }
   }

   // outpointer it
   *driver_configs = configs;

   // This has to be a separate allocation from the configs.
   // If we had any additional screen state we'd need to do
   // something less hacky.
   return malloc(sizeof(int));
}

const __DRIextension *__driDriverExtensions[];

static __DRIscreen *
dril2CreateNewScreen(int scrn, int fd,
                     const __DRIextension **extensions,
                     const __DRIconfig ***driver_configs, void *data)
{
   return drilCreateNewScreen(scrn, fd,
                              extensions,
                              __driDriverExtensions,
                              driver_configs, data);
}

static __DRIscreen *
drilSWCreateNewScreen(int scrn, const __DRIextension **extensions,
                      const __DRIconfig ***driver_configs,
                      void *data)
{
   return drilCreateNewScreen(scrn, -1,
                              extensions,
                              __driDriverExtensions,
                              driver_configs, data);
}

static __DRIscreen *
drilSWCreateNewScreen2(int scrn, const __DRIextension **extensions,
                       const __DRIextension **driver_extensions,
                       const __DRIconfig ***driver_configs, void *data)
{
   return drilCreateNewScreen(scrn, -1,
                              extensions,
                              __driDriverExtensions,
                              driver_configs, data);
}

static int
drilSWQueryBufferAge(__DRIdrawable *pdp)
{
   return 0;
}


static const __DRIswrastExtension drilSWRastExtension = {
   .base = { __DRI_SWRAST, 5 },

   .createNewScreen = drilSWCreateNewScreen,
   .createNewDrawable = drilCreateNewDrawable,
   .createNewContextForAPI     = drilCreateNewContextForAPI,
   .createContextAttribs       = drilCreateContextAttribs,
   .createNewScreen2           = drilSWCreateNewScreen2,
   .queryBufferAge             = drilSWQueryBufferAge,
};

const __DRIdri2Extension drilDRI2Extension = {
    .base = { __DRI_DRI2, 5 },

    /* these are the methods used by the xserver */
    .createNewScreen            = dril2CreateNewScreen,
    .createNewDrawable          = drilCreateNewDrawable,
    .createNewContext           = drilCreateNewContext,
    .createContextAttribs       = drilCreateContextAttribs,
};

const __DRIextension *__driDriverExtensions[] = {
   &drilCoreExtension.base,
   &drilSWRastExtension.base,
   &drilDRI2Extension.base,
   NULL
};

#include "util/detect_os.h"

#include "target-helpers/drm_helper.h"
#include "target-helpers/sw_helper.h"

#define DEFINE_LOADER_DRM_ENTRYPOINT(drivername)                          \
const __DRIextension **__driDriverGetExtensions_##drivername(void);       \
PUBLIC const __DRIextension **__driDriverGetExtensions_##drivername(void) \
{                                                                         \
   return __driDriverExtensions;                                   \
}

const __DRIextension **__driDriverGetExtensions_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_swrast(void)
{
   return __driDriverExtensions;
}

const __DRIextension **__driDriverGetExtensions_kms_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_kms_swrast(void)
{
   return __driDriverExtensions;
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
