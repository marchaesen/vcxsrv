#include <X11/Xwindows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/internal/dri_interface.h>
#include <stdint.h>

#include <glx/glapi.h>
#include <glx/glapitable.h>

typedef unsigned char BYTE;
typedef int BOOL;

#ifdef _DEBUG
#define PRINTF(...) ErrorF( __VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define PUBLIC __declspec(dllexport)

BOOL colorIndexMode = FALSE;
BOOL doubleBuffered = FALSE;

struct __DRIscreenRec
{
  int ScreenNum;
  const __DRIextension **extensions;
  const __DRIswrastLoaderExtension *swrast_loader;
};

struct __DRIcontextRec
{
  struct _glapi_table *Dispatch;

  void *driverPrivate;
  void *loaderPrivate;
  __DRIdrawable *driDrawablePriv;
  __DRIdrawable *driReadablePriv;
  __DRIscreen *driScreenPriv;
};

struct __DRIdrawableRec
{
  HDC hDC;
  HDC hDCFrontBuffer;
  HGLRC hGLRC;
  HPALETTE hPalette;
  HBITMAP hBitmap;
  int winWidth;
  int winHeight;
  VOID *bits;

  void *driverPrivate;
  void *loaderPrivate;
  __DRIscreen *driScreenPriv;
  int refcount;
};

/* Struct used to manage color ramps */
struct colorIndexState
{
  GLfloat amb[3];     /* ambient color / bottom of ramp */
  GLfloat diff[3];    /* diffuse color / middle of ramp */
  GLfloat spec[3];    /* specular color / top of ramp */
  GLfloat ratio;      /* ratio of diffuse to specular in ramp */
  GLint indexes[3];   /* where ramp was placed in palette */
};

/*
** Each entry in this array corresponds to a color ramp in the
** palette.  The indexes member of each struct is updated to
** reflect the placement of the color ramp in the palette.
*/
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))
struct colorIndexState colors[] = {
    {
        { 0.0F, 0.0F, 0.0F },
        { 0.1F, 0.6F, 0.3F },
        { 1.0F, 1.0F, 1.0F },
        0.75F, { 0, 0, 0 },
    },
    {
        { 0.0F, 0.0F, 0.0F },
        { 0.0F, 0.2F, 0.5F },
        { 1.0F, 1.0F, 1.0F },

        0.75F, { 0, 0, 0 },
    },
    {
        { 0.0F, 0.05F, 0.05F },
        { 0.6F, 0.0F, 0.8F },
        { 1.0F, 1.0F, 1.0F },
        0.75F, { 0, 0, 0 },
    },
};
void setupPalette(__DRIdrawable * pdp)
{
    PIXELFORMATDESCRIPTOR pfd;
    LOGPALETTE* pPal;
    int pixelFormat = GetPixelFormat(pdp->hDC);
    int paletteSize;

    PRINTF(__FUNCTION__": pdp %x\n", pdp);

    DescribePixelFormat(pdp->hDC, pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd);

    /*
    ** Determine if a palette is needed and if so what size.
    */
    if (pfd.dwFlags & PFD_NEED_PALETTE) {
        paletteSize = 1 << pfd.cColorBits;
    } else if (pfd.iPixelType == PFD_TYPE_COLORINDEX) {
        paletteSize = 4096;
    } else {
        return;
    }

    pPal = (LOGPALETTE*)malloc(sizeof(LOGPALETTE) + paletteSize * sizeof(PALETTEENTRY));
    pPal->palVersion = 0x300;
    pPal->palNumEntries = paletteSize;

    if (pfd.iPixelType == PFD_TYPE_RGBA)
    {
        /*
        ** Fill the logical paletee with RGB color ramps
        */
        int redMask = (1 << pfd.cRedBits) - 1;
        int greenMask = (1 << pfd.cGreenBits) - 1;
        int blueMask = (1 << pfd.cBlueBits) - 1;
        int i;

        for (i=0; i<paletteSize; ++i) {
            pPal->palPalEntry[i].peRed =
                    (((i >> pfd.cRedShift) & redMask) * 255) / redMask;
            pPal->palPalEntry[i].peGreen =
                    (((i >> pfd.cGreenShift) & greenMask) * 255) / greenMask;
            pPal->palPalEntry[i].peBlue =
                    (((i >> pfd.cBlueShift) & blueMask) * 255) / blueMask;
            pPal->palPalEntry[i].peFlags = 0;
        }
    } else {
        /*
        ** Fill the logical palette with color ramps.
        **pcp
        ** Set up the logical palette so that it can be realized
        ** into the system palette as an identity palette.
        **
        ** 1) The default static entries should be present and at the right
        **    location.  The easiest way to do this is to grab them from
        **    the current system palette.
        **
        ** 2) All non-static entries should be initialized to unique values.
        **    The easiest way to do this is to ensure that all of the non-static
        **    entries have the PC_NOCOLLAPSE flag bit set.
        */
        int numRamps = NUM_COLORS;
        int rampSize = (paletteSize - 20) / numRamps;
        int extra = (paletteSize - 20) - (numRamps * rampSize);
        int i, r;

        /*
        ** Initialize static entries by copying them from the
        ** current system palette.
        */
        GetSystemPaletteEntries(pdp->hDC, 0, paletteSize, &pPal->palPalEntry[0]);

        /*
        ** Fill in non-static entries with desired colors.
        */
        for (r=0; r<numRamps; ++r) {
            int rampBase = r * rampSize + 10;
            PALETTEENTRY *pe = &pPal->palPalEntry[rampBase];
            int diffSize = (int) (rampSize * colors[r].ratio);
            int specSize = rampSize - diffSize;

            for (i=0; i<rampSize; ++i) {
                GLfloat *c0, *c1;
                GLint a;

                if (i < diffSize) {
                    c0 = colors[r].amb;
                    c1 = colors[r].diff;
                    a = (i * 255) / (diffSize - 1);
                } else {
                    c0 = colors[r].diff;
                    c1 = colors[r].spec;
                    a = ((i - diffSize) * 255) / (specSize - 1);
                }

                pe[i].peRed = (BYTE) (a * (c1[0] - c0[0]) + 255 * c0[0]);
                pe[i].peGreen = (BYTE) (a * (c1[1] - c0[1]) + 255 * c0[1]);
                pe[i].peBlue = (BYTE) (a * (c1[2] - c0[2]) + 255 * c0[2]);
                pe[i].peFlags = PC_NOCOLLAPSE;
            }

            colors[r].indexes[0] = rampBase;
            colors[r].indexes[1] = rampBase + (diffSize-1);
            colors[r].indexes[2] = rampBase + (rampSize-1);
        }

        /*
        ** Initialize any remaining non-static entries.
        */
        for (i=0; i<extra; ++i) {
            int index = numRamps*rampSize+10+i;
            PALETTEENTRY *pe = &pPal->palPalEntry[index];

            pe->peRed = (BYTE) 0;
            pe->peGreen = (BYTE) 0;
            pe->peBlue = (BYTE) 0;
            pe->peFlags = PC_NOCOLLAPSE;
        }
    }

    pdp->hPalette = CreatePalette(pPal);
    free(pPal);

    if (pdp->hPalette) {
        SelectPalette(pdp->hDC, pdp->hPalette, FALSE);
        RealizePalette(pdp->hDC);
    }
}

void setupPixelFormat(__DRIdrawable *pdp)
{
  PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),    /* size of this pfd */
      1,                                /* version num */
      PFD_SUPPORT_OPENGL,               /* support OpenGL */
      0,                                /* pixel type */
      0,                                /* 8-bit color depth */
      0, 0, 0, 0, 0, 0,         /* color bits (ignored) */
      0,                                /* no alpha buffer */
      0,                                /* alpha bits (ignored) */
      0,                                /* no accumulation buffer */
      0, 0, 0, 0,                       /* accum bits (ignored) */
      16,                               /* depth buffer */
      0,                                /* no stencil buffer */
      0,                                /* no auxiliary buffers */
      PFD_MAIN_PLANE,                   /* main layer */
      0,                                /* reserved */
      0, 0, 0,                  /* no layer, visible, damage masks */
  };
  int SelectedPixelFormat;
  BOOL retVal;

  PRINTF(__FUNCTION__": pdp %x\n", pdp);

  pfd.cColorBits = GetDeviceCaps(pdp->hDC, BITSPIXEL);

  if (colorIndexMode) {
      pfd.iPixelType = PFD_TYPE_COLORINDEX;
  } else {
      pfd.iPixelType = PFD_TYPE_RGBA;
  }

  if (doubleBuffered) {
      pfd.dwFlags |= PFD_DOUBLEBUFFER;
  }

  pfd.dwFlags |= PFD_DRAW_TO_BITMAP;

  SelectedPixelFormat = ChoosePixelFormat(pdp->hDC, &pfd);
  if (SelectedPixelFormat == 0)
  {
    (void) MessageBox(WindowFromDC(pdp->hDC), "Failed to find acceptable pixel format.", "OpenGL application error", MB_ICONERROR | MB_OK);
    exit(1);
  }

  retVal = SetPixelFormat(pdp->hDC, SelectedPixelFormat, &pfd);
  if (retVal != TRUE)
  {
    MessageBox(WindowFromDC(pdp->hDC), "Failed to set pixel format.", "OpenGL application error", MB_ICONERROR | MB_OK);
    exit(1);
  }
}

void setupDIB(__DRIdrawable * pdp)
{
  HBITMAP hBitmap;
  BITMAPINFO *bmInfo;
  BITMAPINFOHEADER *bmHeader;
  UINT usage;
  VOID *base;
  int bmiSize;
  int bitsPerPixel;

  PRINTF(__FUNCTION__": pdp %x\n", pdp);

  bmiSize = sizeof(*bmInfo);
  bitsPerPixel = GetDeviceCaps(pdp->hDC, BITSPIXEL);

  switch (bitsPerPixel) {
  case 8:
      /* bmiColors is 256 WORD palette indices */
      bmiSize += (256 * sizeof(WORD)) - sizeof(RGBQUAD);
      break;
  case 16:
      /* bmiColors is 3 WORD component masks */
      bmiSize += (3 * sizeof(DWORD)) - sizeof(RGBQUAD);
      break;
  case 24:
  case 32:
  default:
      /* bmiColors not used */
      break;
  }

  bmInfo = (BITMAPINFO *) calloc(1, bmiSize);
  bmHeader = &bmInfo->bmiHeader;

  bmHeader->biSize = sizeof(*bmHeader);
  bmHeader->biWidth = pdp->winWidth;
  bmHeader->biHeight = pdp->winHeight;
  bmHeader->biPlanes = 1;                     /* must be 1 */
  bmHeader->biBitCount = bitsPerPixel;
  bmHeader->biXPelsPerMeter = 0;
  bmHeader->biYPelsPerMeter = 0;
  bmHeader->biClrUsed = 0;                    /* all are used */
  bmHeader->biClrImportant = 0;               /* all are important */

  switch (bitsPerPixel) {
  case 8:
      bmHeader->biCompression = BI_RGB;
      bmHeader->biSizeImage = 0;
      usage = DIB_PAL_COLORS;
      /* bmiColors is 256 WORD palette indices */
      {
          WORD *palIndex = (WORD *) &bmInfo->bmiColors[0];
          int i;

          for (i=0; i<256; i++) {
              palIndex[i] = i;
          }
      }
      break;
  case 16:
      bmHeader->biCompression = BI_RGB;
      bmHeader->biSizeImage = 0;
      usage = DIB_RGB_COLORS;
      /* bmiColors is 3 WORD component masks */
      {
          DWORD *compMask = (DWORD *) &bmInfo->bmiColors[0];

          compMask[0] = 0xF800;
          compMask[1] = 0x07E0;
          compMask[2] = 0x001F;
      }
      break;
  case 24:
  case 32:
  default:
      bmHeader->biCompression = BI_RGB;
      bmHeader->biSizeImage = 0;
      usage = DIB_RGB_COLORS;
      /* bmiColors not used */
      break;
  }

  hBitmap = CreateDIBSection(pdp->hDC, bmInfo, usage, &base, NULL, 0);
  if (hBitmap == NULL)
  {
    (void) MessageBox(WindowFromDC(pdp->hDC), "Failed to create DIBSection.", "OpenGL application error", MB_ICONERROR | MB_OK);
    exit(1);
  }

  pdp->bits=base;
  SelectObject(pdp->hDC, hBitmap);
  if (pdp->hBitmap) DeleteObject(pdp->hBitmap);
  pdp->hBitmap = hBitmap;

  free(bmInfo);
}

static void setupLoaderExtensions(__DRIscreen *psp, const __DRIextension **extensions)
{
  int i;

  for (i = 0; extensions[i]; i++)
  {
    if (strcmp(extensions[i]->name, __DRI_SWRAST_LOADER) == 0)
      psp->swrast_loader = (__DRIswrastLoaderExtension *) extensions[i];
  }
}

static const __DRItexBufferExtension swrastTexBufferExtension = {
  { __DRI_TEX_BUFFER, __DRI_TEX_BUFFER_VERSION },
  NULL,//    swrastSetTexBuffer,
  NULL //    swrastSetTexBuffer2
};

static const __DRIextension *dri_screen_extensions[] = {
  &swrastTexBufferExtension.base,
  NULL
};

struct gl_config
{
   GLboolean rgbMode;
   GLboolean floatMode;
   GLboolean colorIndexMode;  /* XXX is this used anywhere? */
   GLuint doubleBufferMode;
   GLuint stereoMode;

   GLboolean haveAccumBuffer;
   GLboolean haveDepthBuffer;
   GLboolean haveStencilBuffer;

   GLint redBits, greenBits, blueBits, alphaBits;       /* bits per comp */
   GLuint redMask, greenMask, blueMask, alphaMask;
   GLint rgbBits;               /* total bits for rgb */
   GLint indexBits;             /* total bits for colorindex */

   GLint accumRedBits, accumGreenBits, accumBlueBits, accumAlphaBits;
   GLint depthBits;
   GLint stencilBits;

   GLint numAuxBuffers;

   GLint level;

   /* EXT_visual_rating / GLX 1.2 */
   GLint visualRating;

   /* EXT_visual_info / GLX 1.2 */
   GLint transparentPixel;
   /*    colors are floats scaled to ints */
   GLint transparentRed, transparentGreen, transparentBlue, transparentAlpha;
   GLint transparentIndex;

   /* ARB_multisample / SGIS_multisample */
   GLint sampleBuffers;
   GLint samples;

   /* SGIX_pbuffer / GLX 1.3 */
   GLint maxPbufferWidth;
   GLint maxPbufferHeight;
   GLint maxPbufferPixels;
   GLint optimalPbufferWidth;   /* Only for SGIX_pbuffer. */
   GLint optimalPbufferHeight;  /* Only for SGIX_pbuffer. */

   /* OML_swap_method */
   GLint swapMethod;

   /* EXT_texture_from_pixmap */
   GLint bindToTextureRgb;
   GLint bindToTextureRgba;
   GLint bindToMipmapTexture;
   GLint bindToTextureTargets;
   GLint yInverted;

   /* EXT_framebuffer_sRGB */
   GLint sRGBCapable;
};

struct __DRIconfigRec {
    struct gl_config modes;
};

#define __ATTRIB(attrib, field) \
    { attrib, offsetof(struct gl_config, field) }

static const struct { unsigned int attrib, offset; } attribMap[] = {
    __ATTRIB(__DRI_ATTRIB_BUFFER_SIZE,                  rgbBits),
    __ATTRIB(__DRI_ATTRIB_LEVEL,                        level),
    __ATTRIB(__DRI_ATTRIB_RED_SIZE,                     redBits),
    __ATTRIB(__DRI_ATTRIB_GREEN_SIZE,                   greenBits),
    __ATTRIB(__DRI_ATTRIB_BLUE_SIZE,                    blueBits),
    __ATTRIB(__DRI_ATTRIB_ALPHA_SIZE,                   alphaBits),
    __ATTRIB(__DRI_ATTRIB_DEPTH_SIZE,                   depthBits),
    __ATTRIB(__DRI_ATTRIB_STENCIL_SIZE,                 stencilBits),
    __ATTRIB(__DRI_ATTRIB_ACCUM_RED_SIZE,               accumRedBits),
    __ATTRIB(__DRI_ATTRIB_ACCUM_GREEN_SIZE,             accumGreenBits),
    __ATTRIB(__DRI_ATTRIB_ACCUM_BLUE_SIZE,              accumBlueBits),
    __ATTRIB(__DRI_ATTRIB_ACCUM_ALPHA_SIZE,             accumAlphaBits),
    __ATTRIB(__DRI_ATTRIB_SAMPLE_BUFFERS,               sampleBuffers),
    __ATTRIB(__DRI_ATTRIB_SAMPLES,                      samples),
    __ATTRIB(__DRI_ATTRIB_DOUBLE_BUFFER,                doubleBufferMode),
    __ATTRIB(__DRI_ATTRIB_STEREO,                       stereoMode),
    __ATTRIB(__DRI_ATTRIB_AUX_BUFFERS,                  numAuxBuffers),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_TYPE,             transparentPixel),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_INDEX_VALUE,      transparentPixel),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_RED_VALUE,        transparentRed),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_GREEN_VALUE,      transparentGreen),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_BLUE_VALUE,       transparentBlue),
    __ATTRIB(__DRI_ATTRIB_TRANSPARENT_ALPHA_VALUE,      transparentAlpha),
    __ATTRIB(__DRI_ATTRIB_FLOAT_MODE,                   floatMode),
    __ATTRIB(__DRI_ATTRIB_RED_MASK,                     redMask),
    __ATTRIB(__DRI_ATTRIB_GREEN_MASK,                   greenMask),
    __ATTRIB(__DRI_ATTRIB_BLUE_MASK,                    blueMask),
    __ATTRIB(__DRI_ATTRIB_ALPHA_MASK,                   alphaMask),
    __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_WIDTH,            maxPbufferWidth),
    __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_HEIGHT,           maxPbufferHeight),
    __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_PIXELS,           maxPbufferPixels),
    __ATTRIB(__DRI_ATTRIB_OPTIMAL_PBUFFER_WIDTH,        optimalPbufferWidth),
    __ATTRIB(__DRI_ATTRIB_OPTIMAL_PBUFFER_HEIGHT,       optimalPbufferHeight),
    __ATTRIB(__DRI_ATTRIB_SWAP_METHOD,                  swapMethod),
    __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGB,          bindToTextureRgb),
    __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGBA,         bindToTextureRgba),
    __ATTRIB(__DRI_ATTRIB_BIND_TO_MIPMAP_TEXTURE,       bindToMipmapTexture),
    __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_TARGETS,      bindToTextureTargets),
    __ATTRIB(__DRI_ATTRIB_YINVERTED,                    yInverted),
    __ATTRIB(__DRI_ATTRIB_FRAMEBUFFER_SRGB_CAPABLE,     sRGBCapable),

    /* The struct field doesn't matter here, these are handled by the
     * switch in driGetConfigAttribIndex.  We need them in the array
     * so the iterator includes them though.*/
    __ATTRIB(__DRI_ATTRIB_RENDER_TYPE,                  level),
    __ATTRIB(__DRI_ATTRIB_CONFIG_CAVEAT,                level),
    __ATTRIB(__DRI_ATTRIB_SWAP_METHOD,                  level)
};

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

__DRIconfig **
driCreateConfigs(GLenum fb_format, GLenum fb_type,
                 const uint8_t * depth_bits, const uint8_t * stencil_bits,
                 unsigned num_depth_stencil_bits,
                 const GLenum * db_modes, unsigned num_db_modes,
                 const uint8_t * msaa_samples, unsigned num_msaa_modes,
                 GLboolean enable_accum)
{
  static const uint8_t bits_table[4][4] = {
    /* R  G  B  A */
     { 3, 3, 2, 0 }, /* Any GL_UNSIGNED_BYTE_3_3_2 */
     { 5, 6, 5, 0 }, /* Any GL_UNSIGNED_SHORT_5_6_5 */
     { 8, 8, 8, 0 }, /* Any RGB with any GL_UNSIGNED_INT_8_8_8_8 */
     { 8, 8, 8, 8 }  /* Any RGBA with any GL_UNSIGNED_INT_8_8_8_8 */
  };

  static const uint32_t masks_table_rgb[6][4] = {
     { 0x000000E0, 0x0000001C, 0x00000003, 0x00000000 }, /* 3_3_2       */
     { 0x00000007, 0x00000038, 0x000000C0, 0x00000000 }, /* 2_3_3_REV   */
     { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 }, /* 5_6_5       */
     { 0x0000001F, 0x000007E0, 0x0000F800, 0x00000000 }, /* 5_6_5_REV   */
     { 0xFF000000, 0x00FF0000, 0x0000FF00, 0x00000000 }, /* 8_8_8_8     */
     { 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000 }  /* 8_8_8_8_REV */
  };

  static const uint32_t masks_table_rgba[6][4] = {
     { 0x000000E0, 0x0000001C, 0x00000003, 0x00000000 }, /* 3_3_2       */
     { 0x00000007, 0x00000038, 0x000000C0, 0x00000000 }, /* 2_3_3_REV   */
     { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 }, /* 5_6_5       */
     { 0x0000001F, 0x000007E0, 0x0000F800, 0x00000000 }, /* 5_6_5_REV   */
     { 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF }, /* 8_8_8_8     */
     { 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000 }, /* 8_8_8_8_REV */
  };

  static const uint32_t masks_table_bgr[6][4] = {
     { 0x00000007, 0x00000038, 0x000000C0, 0x00000000 }, /* 3_3_2       */
     { 0x000000E0, 0x0000001C, 0x00000003, 0x00000000 }, /* 2_3_3_REV   */
     { 0x0000001F, 0x000007E0, 0x0000F800, 0x00000000 }, /* 5_6_5       */
     { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 }, /* 5_6_5_REV   */
     { 0x0000FF00, 0x00FF0000, 0xFF000000, 0x00000000 }, /* 8_8_8_8     */
     { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 }, /* 8_8_8_8_REV */
  };

  static const uint32_t masks_table_bgra[6][4] = {
     { 0x00000007, 0x00000038, 0x000000C0, 0x00000000 }, /* 3_3_2       */
     { 0x000000E0, 0x0000001C, 0x00000003, 0x00000000 }, /* 2_3_3_REV   */
     { 0x0000001F, 0x000007E0, 0x0000F800, 0x00000000 }, /* 5_6_5       */
     { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 }, /* 5_6_5_REV   */
     { 0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF }, /* 8_8_8_8     */
     { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 }, /* 8_8_8_8_REV */
  };

  static const uint8_t bytes_per_pixel[6] = {
     1, /* 3_3_2       */
     1, /* 2_3_3_REV   */
     2, /* 5_6_5       */
     2, /* 5_6_5_REV   */
     4, /* 8_8_8_8     */
     4  /* 8_8_8_8_REV */

  };

  const uint8_t  * bits;
  const uint32_t * masks;
  int index;
  __DRIconfig **configs, **c;

  struct gl_config *modes;
  unsigned i, j, k, h;
  unsigned num_modes;
  unsigned num_accum_bits = (enable_accum) ? 2 : 1;

  PRINTF(__FUNCTION__"\n");
  switch ( fb_type ) {
     case GL_UNSIGNED_BYTE_3_3_2:
        index = 0;
        break;
     case GL_UNSIGNED_BYTE_2_3_3_REV:
        index = 1;
        break;
     case GL_UNSIGNED_SHORT_5_6_5:
        index = 2;
        break;
     case GL_UNSIGNED_SHORT_5_6_5_REV:
        index = 3;
        break;
     case GL_UNSIGNED_INT_8_8_8_8:
        index = 4;
        break;
     case GL_UNSIGNED_INT_8_8_8_8_REV:
        index = 5;
        break;
     default:
        fprintf( stderr, "[%s:%u] Unknown framebuffer type 0x%04x.\n",
              __FUNCTION__, __LINE__, fb_type );
        return NULL;
  }


  /* Valid types are GL_UNSIGNED_SHORT_5_6_5 and GL_UNSIGNED_INT_8_8_8_8 and
   * the _REV versions.
   *
   * Valid formats are GL_RGBA, GL_RGB, and GL_BGRA.
   */

  switch ( fb_format ) {
     case GL_RGB:
        masks = masks_table_rgb[ index ];
        break;

     case GL_RGBA:
        masks = masks_table_rgba[ index ];
        break;

     case GL_BGR:
        masks = masks_table_bgr[ index ];
        break;

     case GL_BGRA:
        masks = masks_table_bgra[ index ];
        break;

     default:
        fprintf( stderr, "[%s:%u] Unknown framebuffer format 0x%04x.\n",
              __FUNCTION__, __LINE__, fb_format );
        return NULL;
  }

  switch ( bytes_per_pixel[ index ] ) {
     case 1:
        bits = bits_table[0];
        break;
     case 2:
        bits = bits_table[1];
        break;
     default:
        bits = ((fb_format == GL_RGB) || (fb_format == GL_BGR))
           ? bits_table[2]
           : bits_table[3];
        break;
  }

  num_modes = num_depth_stencil_bits * num_db_modes * num_accum_bits * num_msaa_modes;
  configs = calloc(1, (num_modes + 1) * sizeof *configs);
  if (configs == NULL)
      return NULL;

  c = configs;
  for ( k = 0 ; k < num_depth_stencil_bits ; k++ ) {
      for ( i = 0 ; i < num_db_modes ; i++ ) {
          for ( h = 0 ; h < num_msaa_modes; h++ ) {
              for ( j = 0 ; j < num_accum_bits ; j++ ) {
                  *c = malloc (sizeof **c);
                  modes = &(*c)->modes;
                  c++;

                  memset(modes, 0, sizeof *modes);
                  modes->redBits   = bits[0];
                  modes->greenBits = bits[1];
                  modes->blueBits  = bits[2];
                  modes->alphaBits = bits[3];
                  modes->redMask   = masks[0];
                  modes->greenMask = masks[1];
                  modes->blueMask  = masks[2];
                  modes->alphaMask = masks[3];
                  modes->rgbBits   = modes->redBits + modes->greenBits
                      + modes->blueBits + modes->alphaBits;

                  modes->accumRedBits   = 16 * j;
                  modes->accumGreenBits = 16 * j;
                  modes->accumBlueBits  = 16 * j;
                  modes->accumAlphaBits = (masks[3] != 0) ? 16 * j : 0;
                  modes->visualRating = (j == 0) ? GLX_NONE : GLX_SLOW_CONFIG;

                  modes->stencilBits = stencil_bits[k];
                  modes->depthBits = depth_bits[k];

                  modes->transparentPixel = GLX_NONE;
                  modes->transparentRed = GLX_DONT_CARE;
                  modes->transparentGreen = GLX_DONT_CARE;
                  modes->transparentBlue = GLX_DONT_CARE;
                  modes->transparentAlpha = GLX_DONT_CARE;
                  modes->transparentIndex = GLX_DONT_CARE;
                  modes->rgbMode = GL_TRUE;

                  if ( db_modes[i] == GLX_NONE ) {
                      modes->doubleBufferMode = GL_FALSE;
                  }
                  else {
                      modes->doubleBufferMode = GL_TRUE;
                      modes->swapMethod = db_modes[i];
                  }

                  modes->samples = msaa_samples[h];
                  modes->sampleBuffers = modes->samples ? 1 : 0;


                  modes->haveAccumBuffer = ((modes->accumRedBits +
                                         modes->accumGreenBits +
                                         modes->accumBlueBits +
                                         modes->accumAlphaBits) > 0);
                  modes->haveDepthBuffer = (modes->depthBits > 0);
                  modes->haveStencilBuffer = (modes->stencilBits > 0);

                  modes->bindToTextureRgb = GL_TRUE;
                  modes->bindToTextureRgba = GL_TRUE;
                  modes->bindToMipmapTexture = GL_FALSE;
                  modes->bindToTextureTargets =
                      __DRI_ATTRIB_TEXTURE_1D_BIT |
                      __DRI_ATTRIB_TEXTURE_2D_BIT |
                      __DRI_ATTRIB_TEXTURE_RECTANGLE_BIT;

                  modes->sRGBCapable = GL_FALSE;
              }
          }
      }
  }
  *c = NULL;

  return configs;
}

static __DRIconfig **
swrastFillInModes(__DRIscreen *psp,
                  unsigned pixel_bits, unsigned depth_bits,
                  unsigned stencil_bits, GLboolean have_back_buffer)
{
  __DRIconfig **configs;
  unsigned depth_buffer_factor;
  unsigned back_buffer_factor;
  GLenum fb_format;
  GLenum fb_type;

  /* GLX_SWAP_COPY_OML is only supported because the Intel driver doesn't
   * support pageflipping at all.
   */
  static const GLenum back_buffer_modes[] = {
      GLX_NONE, GLX_SWAP_UNDEFINED_OML
  };

  uint8_t depth_bits_array[4];
  uint8_t stencil_bits_array[4];
  uint8_t msaa_samples_array[1];

  (void) psp;
  (void) have_back_buffer;

  PRINTF(__FUNCTION__"\n");

  depth_bits_array[0] = 0;
  depth_bits_array[1] = 0;
  depth_bits_array[2] = depth_bits;
  depth_bits_array[3] = depth_bits;

  /* Just like with the accumulation buffer, always provide some modes
   * with a stencil buffer.
   */
  stencil_bits_array[0] = 0;
  stencil_bits_array[1] = (stencil_bits == 0) ? 8 : stencil_bits;
  stencil_bits_array[2] = 0;
  stencil_bits_array[3] = (stencil_bits == 0) ? 8 : stencil_bits;

  msaa_samples_array[0] = 0;

  depth_buffer_factor = 4;
  back_buffer_factor = 2;

  switch (pixel_bits) {
  case 8:
      fb_format = GL_RGB;
      fb_type = GL_UNSIGNED_BYTE_2_3_3_REV;
      break;
  case 16:
      fb_format = GL_RGB;
      fb_type = GL_UNSIGNED_SHORT_5_6_5;
      break;
  case 24:
      fb_format = GL_BGR;
      fb_type = GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
  case 32:
      fb_format = GL_BGRA;
      fb_type = GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
  default:
      fprintf(stderr, "[%s:%u] bad depth %d\n", __FUNCTION__, __LINE__,
              pixel_bits);
      return NULL;
  }

  configs = driCreateConfigs(fb_format, fb_type,
                             depth_bits_array, stencil_bits_array,
                             depth_buffer_factor, back_buffer_modes,
                             back_buffer_factor, msaa_samples_array, 1,
                             GL_TRUE);
  if (configs == NULL) {
      fprintf(stderr, "[%s:%u] Error creating FBConfig!\n", __FUNCTION__,
              __LINE__);
      return NULL;
  }

  return configs;
}

__DRIconfig **driConcatConfigs(__DRIconfig **a, __DRIconfig **b)
{
  __DRIconfig **all;
  int i, j, index;

  PRINTF(__FUNCTION__"\n");

  i = 0;
  while (a[i] != NULL)
      i++;
  j = 0;
  while (b[j] != NULL)
      j++;

  all = malloc((i + j + 1) * sizeof *all);
  index = 0;
  for (i = 0; a[i] != NULL; i++)
      all[index++] = a[i];
  for (j = 0; b[j] != NULL; j++)
      all[index++] = b[j];
  all[index++] = NULL;

  free(a);
  free(b);

  return all;
}

static int driGetConfigAttribIndex(const __DRIconfig *config, unsigned int index, unsigned int *value)
{
  switch (attribMap[index].attrib) {
  case __DRI_ATTRIB_RENDER_TYPE:
      /* no support for color index mode */
      *value = __DRI_ATTRIB_RGBA_BIT;
      break;
  case __DRI_ATTRIB_CONFIG_CAVEAT:
      if (config->modes.visualRating == GLX_NON_CONFORMANT_CONFIG)
          *value = __DRI_ATTRIB_NON_CONFORMANT_CONFIG;
      else if (config->modes.visualRating == GLX_SLOW_CONFIG)
          *value = __DRI_ATTRIB_SLOW_BIT;
      else
          *value = 0;
      break;
  case __DRI_ATTRIB_SWAP_METHOD:
      /* XXX no return value??? */
      break;

  case __DRI_ATTRIB_FLOAT_MODE:
      /* this field is not int-sized */
      *value = config->modes.floatMode;
      break;

  default:
      /* any other int-sized field */
      *value = *(unsigned int *)
          ((char *) &config->modes + attribMap[index].offset);
      break;
  }

    return GL_TRUE;
}

int driIndexConfigAttrib(const __DRIconfig *config, int index, unsigned int *attrib, unsigned int *value)
{
  if (index >= 0 && index < ARRAY_SIZE(attribMap))
  {
    *attrib = attribMap[index].attrib;
    return driGetConfigAttribIndex(config, index, value);
  }

  return GL_FALSE;
}

static const __DRIconfig **dri_init_screen(__DRIscreen * psp)
{
  __DRIconfig **configs8, **configs16, **configs24, **configs32;

  PRINTF(__FUNCTION__": psp %x\n", psp);

  psp->extensions = dri_screen_extensions;

  configs8  = swrastFillInModes(psp,  8,  8, 0, 1);
  configs16 = swrastFillInModes(psp, 16, 16, 0, 1);
  configs24 = swrastFillInModes(psp, 24, 24, 8, 1);
  configs32 = swrastFillInModes(psp, 32, 24, 8, 1);

  configs16 = driConcatConfigs(configs8, configs16);
  configs24 = driConcatConfigs(configs16, configs24);
  configs32 = driConcatConfigs(configs24, configs32);

  return (const __DRIconfig **)configs32;
}

static __DRIscreen *driCreateNewScreen(int scrn, const __DRIextension **extensions, const __DRIconfig ***driver_configs, void *data)
{
  static const __DRIextension *emptyExtensionList[] = { NULL };
  __DRIscreen *psp;

  psp = calloc(sizeof(struct __DRIscreenRec),1);
  if (!psp)
    return NULL;

  PRINTF(__FUNCTION__": psp %x\n", psp);

  setupLoaderExtensions(psp, extensions);

  psp->extensions = emptyExtensionList;
  psp->ScreenNum = scrn;

  *driver_configs = dri_init_screen(psp);

  if (*driver_configs == NULL)
  {
    free(psp);
    return NULL;
  }

  return psp;
}

static __DRIcontext *driCreateNewContext(__DRIscreen *psp, const __DRIconfig *config,  __DRIcontext *shared, void *data)
{
  __DRIcontext *pcp;
  void * const shareCtx = (shared != NULL) ? shared->driverPrivate : NULL;

  pcp = calloc(sizeof(struct __DRIcontextRec),1);
  if (!pcp)
      return NULL;

  PRINTF(__FUNCTION__": psp %x, shared %x, data %x, pcp %x\n", psp, shared, data, pcp);

  pcp->loaderPrivate = data;

  pcp->driScreenPriv = psp;
  pcp->driDrawablePriv = NULL;
  pcp->driReadablePriv = NULL;

  pcp->Dispatch=calloc(sizeof(void*), (sizeof(struct _glapi_table) / sizeof(void *) + MAX_EXTENSION_FUNCS));
  _glapi_set_dispatch(pcp->Dispatch);

  glWinSetupDispatchTable();

  return pcp;
}

static const __DRIextension **driGetExtensions(__DRIscreen *psp)
{
  return psp->extensions;
}

static void driDrawableCheckSize(__DRIdrawable *pdp)
{
  int x;
  int y;
  int w;
  int h;
  pdp->driScreenPriv->swrast_loader->getDrawableInfo(pdp, &x, &y, &w, &h, pdp->loaderPrivate);
  if (pdp->winWidth==w && pdp->winHeight==h)
    return;
  pdp->winWidth=w;
  pdp->winHeight=h;
  setupDIB(pdp);
}
static __DRIdrawable *driCreateNewDrawable(__DRIscreen *psp, const __DRIconfig *config, void *data)
{
  __DRIdrawable *pdp = calloc(sizeof(struct __DRIdrawableRec),1);
  if (!pdp)
      return NULL;

  PRINTF(__FUNCTION__": pdp %x, data %x\n", pdp, data);

  pdp->loaderPrivate = data;

  pdp->driScreenPriv = psp;

  pdp->refcount++;

  pdp->hDCFrontBuffer = GetDC(NULL);
  pdp->hDC = CreateCompatibleDC(pdp->hDCFrontBuffer);

  driDrawableCheckSize(pdp);

  setupPixelFormat(pdp);
  setupPalette(pdp);
  pdp->hGLRC = wglCreateContext(pdp->hDC);

  return pdp;
}

static __DRIcontext *current_pcp;

static int driBindContext(__DRIcontext *pcp, __DRIdrawable *pdp, __DRIdrawable *prp)
{
  PRINTF(__FUNCTION__": pcp %x, pdp %x, prp %x\n",pcp, pdp, prp);
  
  if (current_pcp)
  {
    driUnbindContext(current_pcp);
    current_pcp=NULL;
  }

  /* Bind the drawable to the context */
  if (pcp)
  {
    static int Created=0;

    if (!pdp || !prp)
    {
      return GL_TRUE;
    }

    pcp->driDrawablePriv = pdp;
    pcp->driReadablePriv = prp;
    if (pdp)
    {
      pdp->refcount++;
    }
    if ( prp && pdp != prp )
    {
      prp->refcount++;
    }

    if (wglGetCurrentContext()!=pdp->hGLRC)
    {
      if (!wglMakeCurrent(pdp->hDC, pdp->hGLRC))
        ErrorF("Error making context current: pdp %x\n",pdp);
      PRINTF("pdp->hDC %x to pdp->hGLRC %x\n",pdp->hDC, pdp->hGLRC);
    }
    if (Created)
    {
      // initialize wgl extension proc pointers (don't call them before here...)
      // (but we need to have a current context for them to be resolvable)
      Created=1;
      wglResolveExtensionProcs();
    }
    current_pcp=pcp;
    _glapi_set_dispatch(pcp->Dispatch);
  }

  return GL_TRUE;
}

static void driDestroyDrawable(__DRIdrawable *pdp)
{
  if (pdp)
  {
    pdp->refcount--;
    if (pdp->refcount) return;

    PRINTF(__FUNCTION__": pdp %x\n", pdp);

    wglDeleteContext (pdp->hGLRC);
    DeleteDC(pdp->hDC);
    ReleaseDC(NULL, pdp->hDCFrontBuffer);
    DeleteObject(pdp->hBitmap);
    if (pdp->hPalette) DeleteObject(pdp->hPalette);

    free(pdp);
  }
}

static int driUnbindContext(__DRIcontext *pcp)
{
  __DRIdrawable *pdp;
  __DRIdrawable *prp;

  PRINTF(__FUNCTION__": pcp %x\n", pcp);

  if (pcp == NULL)
      return GL_FALSE;

  pdp = pcp->driDrawablePriv;
  prp = pcp->driReadablePriv;

  /* already unbound */
  if (!pdp && !prp)
  {
    PRINTF(__FUNCTION__": pcp %x already unbound\n", pcp);
    return GL_TRUE;
  }

  if (wglGetCurrentContext()==pdp->hGLRC)
  {
    current_pcp=NULL;
    wglMakeCurrent(pdp->hDC, NULL);
    PRINTF("pdp->hDC %x to NULL\n", pdp->hDC);
  }

  driDestroyDrawable(pdp);
  if (prp!=pdp)
    driDestroyDrawable(prp);

  pcp->driDrawablePriv = NULL;
  pcp->driReadablePriv = NULL;
  _glapi_set_dispatch(NULL);

  return GL_TRUE;
}

static void driDestroyContext(__DRIcontext *pcp)
{
  PRINTF(__FUNCTION__": pcp %x\n", pcp);
  if (pcp)
  {
    driUnbindContext(pcp);
    free(pcp);
  }
}

static void driDestroyScreen(__DRIscreen *psp)
{
  PRINTF(__FUNCTION__": psp %x\n", psp);
  if (psp)
  {
    free(psp);
  }
}

static void driSwapBuffers(__DRIdrawable *pdp)
{
//    GET_CURRENT_CONTEXT(ctx);
  /* Revert image */
  int Row;
  int UpRow;
  int ColCount;
  int HalfRowCount;
  int *pBits;

  //GdiFlush();

  driDrawableCheckSize(pdp);

  ColCount=pdp->winWidth;
  HalfRowCount=pdp->winHeight/2;
  pBits=(int*)pdp->bits;

  for (Row=0,UpRow=2*HalfRowCount-1; Row<HalfRowCount; Row++,UpRow--)
  {
    int j;
    for (j=0; j<ColCount; j++)
    {
      int Temp=pBits[Row*ColCount+j];
      pBits[Row*ColCount+j]=pBits[UpRow*ColCount+j];
      pBits[UpRow*ColCount+j]=Temp;
    }
  }

  pdp->driScreenPriv->swrast_loader->putImage(pdp, __DRI_SWRAST_IMAGE_OP_SWAP, 0, 0, pdp->winWidth, pdp->winHeight, pdp->bits, pdp->loaderPrivate);
}

const __DRIcoreExtension driCoreExtension = {
  { __DRI_CORE, __DRI_CORE_VERSION },
  NULL, /* driCreateNewScreen */
  driDestroyScreen,
  driGetExtensions,
  NULL,//    driGetConfigAttrib,
  driIndexConfigAttrib,
  NULL, /* driCreateNewDrawable */
  driDestroyDrawable,
  driSwapBuffers,
  driCreateNewContext,
  NULL,//    driCopyContext,
  driDestroyContext,
  driBindContext,
  driUnbindContext
};

const __DRIswrastExtension driWGLExtension = {
  { __DRI_SWRAST, __DRI_SWRAST_VERSION },
  driCreateNewScreen,
  driCreateNewDrawable,
  NULL //    driCreateNewContextForAPI
};

/* This is the table of extensions that the loader will dlsym() for. */
PUBLIC const __DRIextension *__driDriverExtensions[] =
{
  &driCoreExtension.base,
  &driWGLExtension.base,
  NULL
};
