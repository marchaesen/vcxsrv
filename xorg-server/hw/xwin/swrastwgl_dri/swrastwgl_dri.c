#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include <X11/Xwindows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/internal/dri_interface.h>
#include <stdint.h>

#include <glx/glheader.h>
#include <glx/glapi.h>
#include <glx/glapitable.h>

#include <stdbool.h>

typedef unsigned char BYTE;
typedef int BOOL;

#ifdef _DEBUG
#define PRINTF(...) ErrorF( __VA_ARGS__)
#else
#define PRINTF(...)
#endif

#undef PUBLIC
#define PUBLIC __declspec(dllexport)

BOOL colorIndexMode = FALSE;
BOOL doubleBuffered = FALSE;

struct __DRIscreenRec
{
  int ScreenNum;
  int max_gl_core_version;
  int max_gl_compat_version;
  int max_gl_es1_version;
  int max_gl_es2_version;

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
  int bitsPerPixel;
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
  pdp->bitsPerPixel=bitsPerPixel;

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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#endif

/**
 * Mesa texture/renderbuffer image formats.
 */
typedef enum
{
   MESA_FORMAT_NONE = 0,

   /**
    * \name Basic hardware formats
    *
    * The mesa format name specification is as follows:
    *
    *  There shall be 3 naming format base types: those for component array
    *  formats (type A); those for compressed formats (type C); and those for
    *  packed component formats (type P). With type A formats, color component
    *  order does not change with endianess. Each format name shall begin with
    *  MESA_FORMAT_, followed by a component label (from the Component Label
    *  list below) for each component in the order that the component(s) occur
    *  in the format, except for non-linear color formats where the first
    *  letter shall be 'S'. For type P formats, each component label is
    *  followed by the number of bits that represent it in the fundamental
    *  data type used by the format.
    *
    *  Following the listing of the component labels shall be an underscore; a
    *  compression type followed by an underscore for Type C formats only; a
    *  storage type from the list below; and a bit with for type A formats,
    *  which is the bit width for each array element.
    *
    *
    *  ----------    Format Base Type A: Array ----------
    *  MESA_FORMAT_[component list]_[storage type][array element bit width]
    *
    *  examples:
    *  MESA_FORMAT_A_SNORM8     - uchar[i] = A
    *  MESA_FORMAT_RGBA_16 - ushort[i * 4 + 0] = R, ushort[i * 4 + 1] = G,
    *                             ushort[i * 4 + 2] = B, ushort[i * 4 + 3] = A
    *  MESA_FORMAT_Z_UNORM32    - float[i] = Z
    *
    *
    *
    *  ----------    Format Base Type C: Compressed ----------
    *  MESA_FORMAT_[component list*][_*][compression type][storage type*]
    *  * where required
    *
    *  examples:
    *  MESA_FORMAT_RGB_ETC1
    *  MESA_FORMAT_RGBA_ETC2
    *  MESA_FORMAT_LATC1_UNORM
    *  MESA_FORMAT_RGBA_FXT1
    *
    *
    *
    *  ----------    Format Base Type P: Packed  ----------
    *  MESA_FORMAT_[[component list,bit width][storage type*][_]][_][storage type**]
    *   * when type differs between component
    *   ** when type applies to all components
    *
    *  examples:                   msb <------ TEXEL BITS -----------> lsb
    *  MESA_FORMAT_A8B8G8R8_UNORM, AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR
    *  MESA_FORMAT_R5G6B5_UNORM                        RRRR RGGG GGGB BBBB
    *  MESA_FORMAT_B4G4R4X4_UNORM                      BBBB GGGG RRRR XXXX
    *  MESA_FORMAT_Z32_FLOAT_S8X24_UINT
    *  MESA_FORMAT_R10G10B10A2_UINT
    *  MESA_FORMAT_R9G9B9E5_FLOAT
    *
    *
    *
    *  ----------    Component Labels: ----------
    *  A - Alpha
    *  B - Blue
    *  DU - Delta U
    *  DV - Delta V
    *  E - Shared Exponent
    *  G - Green
    *  I - Intensity
    *  L - Luminance
    *  R - Red
    *  S - Stencil (when not followed by RGB or RGBA)
    *  U - Chrominance
    *  V - Chrominance
    *  Y - Luma
    *  X - Packing bits
    *  Z - Depth
    *
    *
    *
    *  ----------    Type C Compression Types: ----------
    *  DXT1 - Color component labels shall be given
    *  DXT3 - Color component labels shall be given
    *  DXT5 - Color component labels shall be given
    *  ETC1 - No other information required
    *  ETC2 - No other information required
    *  FXT1 - Color component labels shall be given
    *  FXT3 - Color component labels shall be given
    *  LATC1 - Fundamental data type shall be given
    *  LATC2 - Fundamental data type shall be given
    *  RGTC1 - Color component labels and data type shall be given
    *  RGTC2 - Color component labels and data type shall be given
    *
    *
    *
    *  ----------    Storage Types: ----------
    *  FLOAT
    *  SINT
    *  UINT
    *  SNORM
    *  UNORM
    *  SRGB - RGB components, or L are UNORMs in sRGB color space.
    *         Alpha, if present is linear.
    *
    */

   /* Packed unorm formats */    /* msb <------ TEXEL BITS -----------> lsb */
                                 /* ---- ---- ---- ---- ---- ---- ---- ---- */
   MESA_FORMAT_A8B8G8R8_UNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_X8B8G8R8_UNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB xxxx xxxx */
   MESA_FORMAT_R8G8B8A8_UNORM,   /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_UNORM,   /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_B8G8R8A8_UNORM,   /* AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_B8G8R8X8_UNORM,   /* xxxx xxxx RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_A8R8G8B8_UNORM,   /* BBBB BBBB GGGG GGGG RRRR RRRR AAAA AAAA */
   MESA_FORMAT_X8R8G8B8_UNORM,   /* BBBB BBBB GGGG GGGG RRRR RRRR xxxx xxxx */
   MESA_FORMAT_L16A16_UNORM,     /* AAAA AAAA AAAA AAAA LLLL LLLL LLLL LLLL */
   MESA_FORMAT_A16L16_UNORM,     /* LLLL LLLL LLLL LLLL AAAA AAAA AAAA AAAA */
   MESA_FORMAT_B5G6R5_UNORM,                         /* RRRR RGGG GGGB BBBB */
   MESA_FORMAT_R5G6B5_UNORM,                         /* BBBB BGGG GGGR RRRR */
   MESA_FORMAT_B4G4R4A4_UNORM,                       /* AAAA RRRR GGGG BBBB */
   MESA_FORMAT_B4G4R4X4_UNORM,                       /* xxxx RRRR GGGG BBBB */
   MESA_FORMAT_A4R4G4B4_UNORM,                       /* BBBB GGGG RRRR AAAA */
   MESA_FORMAT_A1B5G5R5_UNORM,                       /* RRRR RGGG GGBB BBBA */
   MESA_FORMAT_B5G5R5A1_UNORM,                       /* ARRR RRGG GGGB BBBB */
   MESA_FORMAT_B5G5R5X1_UNORM,                       /* xRRR RRGG GGGB BBBB */
   MESA_FORMAT_A1R5G5B5_UNORM,                       /* BBBB BGGG GGRR RRRA */
   MESA_FORMAT_L8A8_UNORM,                           /* AAAA AAAA LLLL LLLL */
   MESA_FORMAT_A8L8_UNORM,                           /* LLLL LLLL AAAA AAAA */
   MESA_FORMAT_R8G8_UNORM,                           /* GGGG GGGG RRRR RRRR */
   MESA_FORMAT_G8R8_UNORM,                           /* RRRR RRRR GGGG GGGG */
   MESA_FORMAT_L4A4_UNORM,                                     /* AAAA LLLL */
   MESA_FORMAT_B2G3R3_UNORM,                                   /* RRRG GGBB */

   MESA_FORMAT_R16G16_UNORM,     /* GGGG GGGG GGGG GGGG RRRR RRRR RRRR RRRR */
   MESA_FORMAT_G16R16_UNORM,     /* RRRR RRRR RRRR RRRR GGGG GGGG GGGG GGGG */
   MESA_FORMAT_B10G10R10A2_UNORM,/* AARR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_B10G10R10X2_UNORM,/* xxRR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_R10G10B10A2_UNORM,/* AABB BBBB BBBB GGGG GGGG GGRR RRRR RRRR */

   MESA_FORMAT_S8_UINT_Z24_UNORM,/* ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ SSSS SSSS */
   MESA_FORMAT_X8_UINT_Z24_UNORM,/* ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ xxxx xxxx */
   MESA_FORMAT_Z24_UNORM_S8_UINT,/* SSSS SSSS ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ */
   MESA_FORMAT_Z24_UNORM_X8_UINT,/* xxxx xxxx ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ */

   MESA_FORMAT_YCBCR,            /*                     YYYY YYYY UorV UorV */
   MESA_FORMAT_YCBCR_REV,        /*                     UorV UorV YYYY YYYY */

   /* Array unorm formats */
   MESA_FORMAT_A_UNORM8,      /* ubyte[i] = A */
   MESA_FORMAT_A_UNORM16,     /* ushort[i] = A */
   MESA_FORMAT_L_UNORM8,      /* ubyte[i] = L */
   MESA_FORMAT_L_UNORM16,     /* ushort[i] = L */
   MESA_FORMAT_I_UNORM8,      /* ubyte[i] = I */
   MESA_FORMAT_I_UNORM16,     /* ushort[i] = I */
   MESA_FORMAT_R_UNORM8,      /* ubyte[i] = R */
   MESA_FORMAT_R_UNORM16,     /* ushort[i] = R */
   MESA_FORMAT_BGR_UNORM8,    /* ubyte[i*3] = B, [i*3+1] = G, [i*3+2] = R */
   MESA_FORMAT_RGB_UNORM8,    /* ubyte[i*3] = R, [i*3+1] = G, [i*3+2] = B */
   MESA_FORMAT_RGBA_UNORM16,  /* ushort[i] = R, [1] = G, [2] = B, [3] = A */
   MESA_FORMAT_RGBX_UNORM16,  

   MESA_FORMAT_Z_UNORM16,     /* ushort[i] = Z */
   MESA_FORMAT_Z_UNORM32,     /* uint[i] = Z */
   MESA_FORMAT_S_UINT8,       /* ubyte[i] = S */

   /* Packed signed/normalized formats */
                                 /* msb <------ TEXEL BITS -----------> lsb */
                                 /* ---- ---- ---- ---- ---- ---- ---- ---- */
   MESA_FORMAT_A8B8G8R8_SNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_X8B8G8R8_SNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB xxxx xxxx */
   MESA_FORMAT_R8G8B8A8_SNORM,   /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_SNORM,   /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R16G16_SNORM,     /* GGGG GGGG GGGG GGGG RRRR RRRR RRRR RRRR */
   MESA_FORMAT_G16R16_SNORM,     /* RRRR RRRR RRRR RRRR GGGG GGGG GGGG GGGG */
   MESA_FORMAT_R8G8_SNORM,       /*                     GGGG GGGG RRRR RRRR */
   MESA_FORMAT_G8R8_SNORM,       /*                     RRRR RRRR GGGG GGGG */
   MESA_FORMAT_L8A8_SNORM,       /*                     AAAA AAAA LLLL LLLL */

   /* Array signed/normalized formats */
   MESA_FORMAT_A_SNORM8,      /* byte[i] = A */
   MESA_FORMAT_A_SNORM16,     /* short[i] = A */
   MESA_FORMAT_L_SNORM8,      /* byte[i] = L */
   MESA_FORMAT_L_SNORM16,     /* short[i] = L */
   MESA_FORMAT_I_SNORM8,      /* byte[i] = I */
   MESA_FORMAT_I_SNORM16,     /* short[i] = I */
   MESA_FORMAT_R_SNORM8,      /* byte[i] = R */
   MESA_FORMAT_R_SNORM16,     /* short[i] = R */
   MESA_FORMAT_LA_SNORM16,    /* short[i * 2] = L, [i * 2 + 1] = A */
   MESA_FORMAT_RGB_SNORM16,   /* short[i*3] = R, [i*3+1] = G, [i*3+2] = B */
   MESA_FORMAT_RGBA_SNORM16,  /* ... */
   MESA_FORMAT_RGBX_SNORM16,  /* ... */

   /* Packed sRGB formats */
   MESA_FORMAT_A8B8G8R8_SRGB,    /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_B8G8R8A8_SRGB,    /* AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_B8G8R8X8_SRGB,    /* xxxx xxxx RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_R8G8B8A8_SRGB,    /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_SRGB,    /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_L8A8_SRGB,                            /* AAAA AAAA LLLL LLLL */

   /* Array sRGB formats */
   MESA_FORMAT_L_SRGB8,       /* ubyte[i] = L */
   MESA_FORMAT_BGR_SRGB8,     /* ubyte[i*3] = B, [i*3+1] = G, [i*3+2] = R */

   /* Packed float formats */
   MESA_FORMAT_R9G9B9E5_FLOAT,
   MESA_FORMAT_R11G11B10_FLOAT,   /* BBBB BBBB BBGG GGGG GGGG GRRR RRRR RRRR */
   MESA_FORMAT_Z32_FLOAT_S8X24_UINT, /* (float, x24s8) */

   /* Array float formats */
   MESA_FORMAT_A_FLOAT16,
   MESA_FORMAT_A_FLOAT32,
   MESA_FORMAT_L_FLOAT16,
   MESA_FORMAT_L_FLOAT32,
   MESA_FORMAT_LA_FLOAT16,
   MESA_FORMAT_LA_FLOAT32,
   MESA_FORMAT_I_FLOAT16,
   MESA_FORMAT_I_FLOAT32,
   MESA_FORMAT_R_FLOAT16,
   MESA_FORMAT_R_FLOAT32,
   MESA_FORMAT_RG_FLOAT16,
   MESA_FORMAT_RG_FLOAT32,
   MESA_FORMAT_RGB_FLOAT16,
   MESA_FORMAT_RGB_FLOAT32,
   MESA_FORMAT_RGBA_FLOAT16,
   MESA_FORMAT_RGBA_FLOAT32,  /* float[0] = R, [1] = G, [2] = B, [3] = A */
   MESA_FORMAT_RGBX_FLOAT16,
   MESA_FORMAT_RGBX_FLOAT32,
   MESA_FORMAT_Z_FLOAT32,

   /* Packed signed/unsigned non-normalized integer formats */
   MESA_FORMAT_B10G10R10A2_UINT, /* AARR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_R10G10B10A2_UINT, /* AABB BBBB BBBB GGGG GGGG GGRR RRRR RRRR */

   /* Array signed/unsigned non-normalized integer formats */
   MESA_FORMAT_A_UINT8,
   MESA_FORMAT_A_UINT16,
   MESA_FORMAT_A_UINT32,
   MESA_FORMAT_A_SINT8,
   MESA_FORMAT_A_SINT16,
   MESA_FORMAT_A_SINT32,

   MESA_FORMAT_I_UINT8,
   MESA_FORMAT_I_UINT16,
   MESA_FORMAT_I_UINT32,
   MESA_FORMAT_I_SINT8,
   MESA_FORMAT_I_SINT16,
   MESA_FORMAT_I_SINT32,

   MESA_FORMAT_L_UINT8,
   MESA_FORMAT_L_UINT16,
   MESA_FORMAT_L_UINT32,
   MESA_FORMAT_L_SINT8,
   MESA_FORMAT_L_SINT16,
   MESA_FORMAT_L_SINT32,

   MESA_FORMAT_LA_UINT8,
   MESA_FORMAT_LA_UINT16,
   MESA_FORMAT_LA_UINT32,
   MESA_FORMAT_LA_SINT8,
   MESA_FORMAT_LA_SINT16,
   MESA_FORMAT_LA_SINT32,

   MESA_FORMAT_R_UINT8,
   MESA_FORMAT_R_UINT16,
   MESA_FORMAT_R_UINT32,
   MESA_FORMAT_R_SINT8,
   MESA_FORMAT_R_SINT16,
   MESA_FORMAT_R_SINT32,

   MESA_FORMAT_RG_UINT8,
   MESA_FORMAT_RG_UINT16,
   MESA_FORMAT_RG_UINT32,
   MESA_FORMAT_RG_SINT8,
   MESA_FORMAT_RG_SINT16,
   MESA_FORMAT_RG_SINT32,

   MESA_FORMAT_RGB_UINT8,
   MESA_FORMAT_RGB_UINT16,
   MESA_FORMAT_RGB_UINT32,
   MESA_FORMAT_RGB_SINT8,
   MESA_FORMAT_RGB_SINT16,
   MESA_FORMAT_RGB_SINT32,

   MESA_FORMAT_RGBA_UINT8,
   MESA_FORMAT_RGBA_UINT16,
   MESA_FORMAT_RGBA_UINT32,
   MESA_FORMAT_RGBA_SINT8,
   MESA_FORMAT_RGBA_SINT16,
   MESA_FORMAT_RGBA_SINT32,

   MESA_FORMAT_RGBX_UINT8,
   MESA_FORMAT_RGBX_UINT16,
   MESA_FORMAT_RGBX_UINT32,
   MESA_FORMAT_RGBX_SINT8,
   MESA_FORMAT_RGBX_SINT16,
   MESA_FORMAT_RGBX_SINT32,

   /* DXT compressed formats */
   MESA_FORMAT_RGB_DXT1,
   MESA_FORMAT_RGBA_DXT1,
   MESA_FORMAT_RGBA_DXT3,
   MESA_FORMAT_RGBA_DXT5,

   /* DXT sRGB compressed formats */
   MESA_FORMAT_SRGB_DXT1,
   MESA_FORMAT_SRGBA_DXT1,
   MESA_FORMAT_SRGBA_DXT3,
   MESA_FORMAT_SRGBA_DXT5,

   /* FXT1 compressed formats */
   MESA_FORMAT_RGB_FXT1,
   MESA_FORMAT_RGBA_FXT1,

   /* RGTC compressed formats */
   MESA_FORMAT_R_RGTC1_UNORM,
   MESA_FORMAT_R_RGTC1_SNORM,
   MESA_FORMAT_RG_RGTC2_UNORM,
   MESA_FORMAT_RG_RGTC2_SNORM,

   /* LATC1/2 compressed formats */
   MESA_FORMAT_L_LATC1_UNORM,
   MESA_FORMAT_L_LATC1_SNORM,
   MESA_FORMAT_LA_LATC2_UNORM,
   MESA_FORMAT_LA_LATC2_SNORM,

   /* ETC1/2 compressed formats */
   MESA_FORMAT_ETC1_RGB8,
   MESA_FORMAT_ETC2_RGB8,
   MESA_FORMAT_ETC2_SRGB8,
   MESA_FORMAT_ETC2_RGBA8_EAC,
   MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC,
   MESA_FORMAT_ETC2_R11_EAC,
   MESA_FORMAT_ETC2_RG11_EAC,
   MESA_FORMAT_ETC2_SIGNED_R11_EAC,
   MESA_FORMAT_ETC2_SIGNED_RG11_EAC,
   MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1,
   MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1,

   MESA_FORMAT_COUNT
}
  mesa_format;

/**
 * Information about texture formats.
 */
struct gl_format_info
{
   mesa_format Name;

   /** text name for debugging */
   const char *StrName;

   /**
    * Base format is one of GL_RED, GL_RG, GL_RGB, GL_RGBA, GL_ALPHA,
    * GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_INTENSITY, GL_YCBCR_MESA,
    * GL_DEPTH_COMPONENT, GL_STENCIL_INDEX, GL_DEPTH_STENCIL.
    */
   GLenum BaseFormat;

   /**
    * Logical data type: one of  GL_UNSIGNED_NORMALIZED, GL_SIGNED_NORMALIZED,
    * GL_UNSIGNED_INT, GL_INT, GL_FLOAT.
    */
   GLenum DataType;

   GLubyte RedBits;
   GLubyte GreenBits;
   GLubyte BlueBits;
   GLubyte AlphaBits;
   GLubyte LuminanceBits;
   GLubyte IntensityBits;
   GLubyte IndexBits;
   GLubyte DepthBits;
   GLubyte StencilBits;

   /**
    * To describe compressed formats.  If not compressed, Width=Height=1.
    */
   GLubyte BlockWidth, BlockHeight;
   GLubyte BytesPerBlock;
};

/**
 * Info about each format.
 * These must be in the same order as the MESA_FORMAT_* enums so that
 * we can do lookups without searching.
 */
static struct gl_format_info format_info[MESA_FORMAT_COUNT] =
{
   /* Packed unorm formats */
   {
      MESA_FORMAT_NONE,            /* Name */
      "MESA_FORMAT_NONE",          /* StrName */
      GL_NONE,                     /* BaseFormat */
      GL_NONE,                     /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      0, 0, 0                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A8B8G8R8_UNORM,  /* Name */
      "MESA_FORMAT_A8B8G8R8_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 8,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_X8B8G8R8_UNORM,  /* Name */
      "MESA_FORMAT_X8B8G8R8_UNORM",/* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R8G8B8A8_UNORM,  /* Name */
      "MESA_FORMAT_R8G8B8A8_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 8,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R8G8B8X8_UNORM,  /* Name */
      "MESA_FORMAT_R8G8B8X8_UNORM",/* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B8G8R8A8_UNORM,  /* Name */
      "MESA_FORMAT_B8G8R8A8_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 8,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B8G8R8X8_UNORM,  /* Name */
      "MESA_FORMAT_B8G8R8X8_UNORM",/* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A8R8G8B8_UNORM,  /* Name */
      "MESA_FORMAT_A8R8G8B8_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 8,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_X8R8G8B8_UNORM,  /* Name */
      "MESA_FORMAT_X8R8G8B8_UNORM",/* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_L16A16_UNORM,    /* Name */
      "MESA_FORMAT_L16A16_UNORM",  /* StrName */
      GL_LUMINANCE_ALPHA,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 16,                 /* Red/Green/Blue/AlphaBits */
      16, 0, 0, 0, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A16L16_UNORM,    /* Name */
      "MESA_FORMAT_A16L16_UNORM",  /* StrName */
      GL_LUMINANCE_ALPHA,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 16,                 /* Red/Green/Blue/AlphaBits */
      16, 0, 0, 0, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B5G6R5_UNORM,    /* Name */
      "MESA_FORMAT_B5G6R5_UNORM",  /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      5, 6, 5, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R5G6B5_UNORM,    /* Name */
      "MESA_FORMAT_R5G6B5_UNORM",  /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      5, 6, 5, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B4G4R4A4_UNORM,  /* Name */
      "MESA_FORMAT_B4G4R4A4_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      4, 4, 4, 4,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B4G4R4X4_UNORM,
      "MESA_FORMAT_B4G4R4X4_UNORM",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_A4R4G4B4_UNORM,  /* Name */
      "MESA_FORMAT_A4R4G4B4_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      4, 4, 4, 4,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A1B5G5R5_UNORM,  /* Name */
      "MESA_FORMAT_A1B5G5R5_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      5, 5, 5, 1,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B5G5R5A1_UNORM,  /* Name */
      "MESA_FORMAT_B5G5R5A1_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      5, 5, 5, 1,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B5G5R5X1_UNORM,
      "MESA_FORMAT_B5G5R5X1_UNORM",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      5, 5, 5, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_A1R5G5B5_UNORM,  /* Name */
      "MESA_FORMAT_A1R5G5B5_UNORM",/* StrName */
      GL_RGBA,                     /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      5, 5, 5, 1,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_L8A8_UNORM,      /* Name */
      "MESA_FORMAT_L8A8_UNORM",    /* StrName */
      GL_LUMINANCE_ALPHA,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 8,                  /* Red/Green/Blue/AlphaBits */
      8, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A8L8_UNORM,      /* Name */
      "MESA_FORMAT_A8L8_UNORM",    /* StrName */
      GL_LUMINANCE_ALPHA,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 8,                  /* Red/Green/Blue/AlphaBits */
      8, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R8G8_UNORM,
      "MESA_FORMAT_R8G8_UNORM",
      GL_RG,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_G8R8_UNORM,
      "MESA_FORMAT_G8R8_UNORM",
      GL_RG,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L4A4_UNORM,      /* Name */
      "MESA_FORMAT_L4A4_UNORM",    /* StrName */
      GL_LUMINANCE_ALPHA,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 4,                  /* Red/Green/Blue/AlphaBits */
      4, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_B2G3R3_UNORM,    /* Name */
      "MESA_FORMAT_B2G3R3_UNORM",  /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      3, 3, 2, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R16G16_UNORM,
      "MESA_FORMAT_R16G16_UNORM",
      GL_RG,
      GL_UNSIGNED_NORMALIZED,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_G16R16_UNORM,
      "MESA_FORMAT_G16R16_UNORM",
      GL_RG,
      GL_UNSIGNED_NORMALIZED,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_B10G10R10A2_UNORM,
      "MESA_FORMAT_B10G10R10A2_UNORM",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      10, 10, 10, 2,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_B10G10R10X2_UNORM,
      "MESA_FORMAT_B10G10R10X2_UNORM",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      10, 10, 10, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R10G10B10A2_UNORM,
      "MESA_FORMAT_R10G10B10A2_UNORM",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      10, 10, 10, 2,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_S8_UINT_Z24_UNORM,   /* Name */
      "MESA_FORMAT_S8_UINT_Z24_UNORM", /* StrName */
      GL_DEPTH_STENCIL,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,          /* DataType */
      0, 0, 0, 0,                      /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 24, 8,                  /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                          /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_X8_UINT_Z24_UNORM,   /* Name */
      "MESA_FORMAT_X8_UINT_Z24_UNORM", /* StrName */
      GL_DEPTH_COMPONENT,              /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,          /* DataType */
      0, 0, 0, 0,                      /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 24, 0,                  /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                          /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_Z24_UNORM_S8_UINT,   /* Name */
      "MESA_FORMAT_Z24_UNORM_S8_UINT", /* StrName */
      GL_DEPTH_STENCIL,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,          /* DataType */
      0, 0, 0, 0,                      /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 24, 8,                  /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                          /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_Z24_UNORM_X8_UINT,   /* Name */
      "MESA_FORMAT_Z24_UNORM_X8_UINT", /* StrName */
      GL_DEPTH_COMPONENT,              /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,          /* DataType */
      0, 0, 0, 0,                      /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 24, 0,                  /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                          /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_YCBCR,           /* Name */
      "MESA_FORMAT_YCBCR",         /* StrName */
      GL_YCBCR_MESA,               /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_YCBCR_REV,       /* Name */
      "MESA_FORMAT_YCBCR_REV",     /* StrName */
      GL_YCBCR_MESA,               /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },

   /* Array unorm formats */
   {
      MESA_FORMAT_A_UNORM8,        /* Name */
      "MESA_FORMAT_A_UNORM8",      /* StrName */
      GL_ALPHA,                    /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 8,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_A_UNORM16,       /* Name */
      "MESA_FORMAT_A_UNORM16",     /* StrName */
      GL_ALPHA,                    /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 16,                 /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_L_UNORM8,        /* Name */
      "MESA_FORMAT_L_UNORM8",      /* StrName */
      GL_LUMINANCE,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      8, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_L_UNORM16,       /* Name */
      "MESA_FORMAT_L_UNORM16",     /* StrName */
      GL_LUMINANCE,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      16, 0, 0, 0, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_I_UNORM8,        /* Name */
      "MESA_FORMAT_I_UNORM8",      /* StrName */
      GL_INTENSITY,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 8, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_I_UNORM16,       /* Name */
      "MESA_FORMAT_I_UNORM16",     /* StrName */
      GL_INTENSITY,                /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 16, 0, 0, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R_UNORM8,
      "MESA_FORMAT_R_UNORM8",
      GL_RED,
      GL_UNSIGNED_NORMALIZED,
      8, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_R_UNORM16,
      "MESA_FORMAT_R_UNORM16",
      GL_RED,
      GL_UNSIGNED_NORMALIZED,
      16, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_BGR_UNORM8,      /* Name */
      "MESA_FORMAT_BGR_UNORM8",    /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 3                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_RGB_UNORM8,      /* Name */
      "MESA_FORMAT_RGB_UNORM8",    /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      8, 8, 8, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 3                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_RGBA_UNORM16,
      "MESA_FORMAT_RGBA_UNORM16",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      16, 16, 16, 16,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBX_UNORM16,
      "MESA_FORMAT_RGBX_UNORM16",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_Z_UNORM16,       /* Name */
      "MESA_FORMAT_Z_UNORM16",     /* StrName */
      GL_DEPTH_COMPONENT,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 16, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 2                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_Z_UNORM32,       /* Name */
      "MESA_FORMAT_Z_UNORM32",     /* StrName */
      GL_DEPTH_COMPONENT,          /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 32, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_S_UINT8,         /* Name */
      "MESA_FORMAT_S_UINT8",       /* StrName */
      GL_STENCIL_INDEX,            /* BaseFormat */
      GL_UNSIGNED_INT,             /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 8,               /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                      /* BlockWidth/Height,Bytes */
   },

   /* Packed signed/normalized formats */
   {
      MESA_FORMAT_A8B8G8R8_SNORM,
      "MESA_FORMAT_A8B8G8R8_SNORM",
      GL_RGBA,
      GL_SIGNED_NORMALIZED,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_X8B8G8R8_SNORM,
      "MESA_FORMAT_X8B8G8R8_SNORM",
      GL_RGB,
      GL_SIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4                       /* 4 bpp, but no alpha */
   },
   {
      MESA_FORMAT_R8G8B8A8_SNORM,
      "MESA_FORMAT_R8G8B8A8_SNORM",
      GL_RGBA,
      GL_SIGNED_NORMALIZED,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R8G8B8X8_SNORM,
      "MESA_FORMAT_R8G8B8X8_SNORM",
      GL_RGB,
      GL_SIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R16G16_SNORM,
      "MESA_FORMAT_R16G16_SNORM",
      GL_RG,
      GL_SIGNED_NORMALIZED,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_G16R16_SNORM,
      "MESA_FORMAT_G16R16_SNORM",
      GL_RG,
      GL_SIGNED_NORMALIZED,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R8G8_SNORM,
      "MESA_FORMAT_R8G8_SNORM",
      GL_RG,
      GL_SIGNED_NORMALIZED,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_G8R8_SNORM,
      "MESA_FORMAT_G8R8_SNORM",
      GL_RG,
      GL_SIGNED_NORMALIZED,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L8A8_SNORM,
      "MESA_FORMAT_L8A8_SNORM",
      GL_LUMINANCE_ALPHA,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 8,
      8, 0, 0, 0, 0,
      1, 1, 2
   },

   /* Array signed/normalized formats */
   {
      MESA_FORMAT_A_SNORM8,
      "MESA_FORMAT_A_SNORM8",
      GL_ALPHA,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 8,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_A_SNORM16,
      "MESA_FORMAT_A_SNORM16",
      GL_ALPHA,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 16,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L_SNORM8,
      "MESA_FORMAT_L_SNORM8",
      GL_LUMINANCE,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 0,
      8, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_L_SNORM16,
      "MESA_FORMAT_L_SNORM16",
      GL_LUMINANCE,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 0,
      16, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_I_SNORM8,
      "MESA_FORMAT_I_SNORM8",
      GL_INTENSITY,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 0,
      0, 8, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_I_SNORM16,
      "MESA_FORMAT_I_SNORM16",
      GL_INTENSITY,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 0,
      0, 16, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_R_SNORM8,         /* Name */
      "MESA_FORMAT_R_SNORM8",       /* StrName */
      GL_RED,                       /* BaseFormat */
      GL_SIGNED_NORMALIZED,         /* DataType */
      8, 0, 0, 0,                   /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,                /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 1                       /* BlockWidth/Height,Bytes */
   },
   {
      MESA_FORMAT_R_SNORM16,
      "MESA_FORMAT_R_SNORM16",
      GL_RED,
      GL_SIGNED_NORMALIZED,
      16, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_LA_SNORM16,
      "MESA_FORMAT_LA_SNORM16",
      GL_LUMINANCE_ALPHA,
      GL_SIGNED_NORMALIZED,
      0, 0, 0, 16,
      16, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RGB_SNORM16,
      "MESA_FORMAT_RGB_SNORM16",
      GL_RGB,
      GL_SIGNED_NORMALIZED,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 6
   },
   {
      MESA_FORMAT_RGBA_SNORM16,
      "MESA_FORMAT_RGBA_SNORM16",
      GL_RGBA,
      GL_SIGNED_NORMALIZED,
      16, 16, 16, 16,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBX_SNORM16,
      "MESA_FORMAT_RGBX_SNORM16",
      GL_RGB,
      GL_SIGNED_NORMALIZED,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },

   /* Packed sRGB formats */
   {
      MESA_FORMAT_A8B8G8R8_SRGB,
      "MESA_FORMAT_A8B8G8R8_SRGB",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_B8G8R8A8_SRGB,
      "MESA_FORMAT_B8G8R8A8_SRGB",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_B8G8R8X8_SRGB,
      "MESA_FORMAT_B8G8R8X8_SRGB",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R8G8B8A8_SRGB,
      "MESA_FORMAT_R8G8B8A8_SRGB",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R8G8B8X8_SRGB,
      "MESA_FORMAT_R8G8B8X8_SRGB",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_L8A8_SRGB,
      "MESA_FORMAT_L8A8_SRGB",
      GL_LUMINANCE_ALPHA,
      GL_UNSIGNED_NORMALIZED,    
      0, 0, 0, 8,
      8, 0, 0, 0, 0,
      1, 1, 2
   },

   /* Array sRGB formats */
   {
      MESA_FORMAT_L_SRGB8,
      "MESA_FORMAT_L_SRGB8",
      GL_LUMINANCE,
      GL_UNSIGNED_NORMALIZED,    
      0, 0, 0, 0,
      8, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_BGR_SRGB8,
      "MESA_FORMAT_BGR_SRGB8",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 3
   },

   /* Packed float formats */
   {
      MESA_FORMAT_R9G9B9E5_FLOAT,
      "MESA_FORMAT_RGB9_E5",
      GL_RGB,
      GL_FLOAT,
      9, 9, 9, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R11G11B10_FLOAT,
      "MESA_FORMAT_R11G11B10_FLOAT",
      GL_RGB,
      GL_FLOAT,
      11, 11, 10, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_Z32_FLOAT_S8X24_UINT,   /* Name */
      "MESA_FORMAT_Z32_FLOAT_S8X24_UINT", /* StrName */
      GL_DEPTH_STENCIL,                   /* BaseFormat */
      /* DataType here is used to answer GL_TEXTURE_DEPTH_TYPE queries, and is
       * never used for stencil because stencil is always GL_UNSIGNED_INT.
       */
      GL_FLOAT,                    /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 32, 8,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 8                      /* BlockWidth/Height,Bytes */
   },

   /* Array float formats */
   {
      MESA_FORMAT_A_FLOAT16,
      "MESA_FORMAT_A_FLOAT16",
      GL_ALPHA,
      GL_FLOAT,
      0, 0, 0, 16,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_A_FLOAT32,
      "MESA_FORMAT_A_FLOAT32",
      GL_ALPHA,
      GL_FLOAT,
      0, 0, 0, 32,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_L_FLOAT16,
      "MESA_FORMAT_L_FLOAT16",
      GL_LUMINANCE,
      GL_FLOAT,
      0, 0, 0, 0,
      16, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L_FLOAT32,
      "MESA_FORMAT_L_FLOAT32",
      GL_LUMINANCE,
      GL_FLOAT,
      0, 0, 0, 0,
      32, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_LA_FLOAT16,
      "MESA_FORMAT_LA_FLOAT16",
      GL_LUMINANCE_ALPHA,
      GL_FLOAT,
      0, 0, 0, 16,
      16, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_LA_FLOAT32,
      "MESA_FORMAT_LA_FLOAT32",
      GL_LUMINANCE_ALPHA,
      GL_FLOAT,
      0, 0, 0, 32,
      32, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_I_FLOAT16,
      "MESA_FORMAT_I_FLOAT16",
      GL_INTENSITY,
      GL_FLOAT,
      0, 0, 0, 0,
      0, 16, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_I_FLOAT32,
      "MESA_FORMAT_I_FLOAT32",
      GL_INTENSITY,
      GL_FLOAT,
      0, 0, 0, 0,
      0, 32, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R_FLOAT16,
      "MESA_FORMAT_R_FLOAT16",
      GL_RED,
      GL_FLOAT,
      16, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_R_FLOAT32,
      "MESA_FORMAT_R_FLOAT32",
      GL_RED,
      GL_FLOAT,
      32, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RG_FLOAT16,
      "MESA_FORMAT_RG_FLOAT16",
      GL_RG,
      GL_FLOAT,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RG_FLOAT32,
      "MESA_FORMAT_RG_FLOAT32",
      GL_RG,
      GL_FLOAT,
      32, 32, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGB_FLOAT16,
      "MESA_FORMAT_RGB_FLOAT16",
      GL_RGB,
      GL_FLOAT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 6
   },
   {
      MESA_FORMAT_RGB_FLOAT32,
      "MESA_FORMAT_RGB_FLOAT32",
      GL_RGB,
      GL_FLOAT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 12
   },
   {
      MESA_FORMAT_RGBA_FLOAT16,
      "MESA_FORMAT_RGBA_FLOAT16",
      GL_RGBA,
      GL_FLOAT,
      16, 16, 16, 16,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBA_FLOAT32,
      "MESA_FORMAT_RGBA_FLOAT32",
      GL_RGBA,
      GL_FLOAT,
      32, 32, 32, 32,
      0, 0, 0, 0, 0,
      1, 1, 16
   },
   {
      MESA_FORMAT_RGBX_FLOAT16,
      "MESA_FORMAT_RGBX_FLOAT16",
      GL_RGB,
      GL_FLOAT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBX_FLOAT32,
      "MESA_FORMAT_RGBX_FLOAT32",
      GL_RGB,
      GL_FLOAT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 16
   },
   {
      MESA_FORMAT_Z_FLOAT32,       /* Name */
      "MESA_FORMAT_Z_FLOAT32",     /* StrName */
      GL_DEPTH_COMPONENT,          /* BaseFormat */
      GL_FLOAT,                    /* DataType */
      0, 0, 0, 0,                  /* Red/Green/Blue/AlphaBits */
      0, 0, 0, 32, 0,              /* Lum/Int/Index/Depth/StencilBits */
      1, 1, 4                      /* BlockWidth/Height,Bytes */
   },

   /* Packed signed/unsigned non-normalized integer formats */
   {
      MESA_FORMAT_B10G10R10A2_UINT,
      "MESA_FORMAT_B10G10R10A2_UINT",
      GL_RGBA,
      GL_UNSIGNED_INT,
      10, 10, 10, 2,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R10G10B10A2_UINT,
      "MESA_FORMAT_R10G10B10A2_UINT",
      GL_RGBA,
      GL_UNSIGNED_INT,
      10, 10, 10, 2,
      0, 0, 0, 0, 0,
      1, 1, 4
   },

   /* Array signed/unsigned non-normalized integer formats */
   {
      MESA_FORMAT_A_UINT8,
      "MESA_FORMAT_A_UINT8",
      GL_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 8,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_A_UINT16,
      "MESA_FORMAT_A_UINT16",
      GL_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 16,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_A_UINT32,
      "MESA_FORMAT_A_UINT32",
      GL_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 32,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_A_SINT8,
      "MESA_FORMAT_A_SINT8",
      GL_ALPHA,
      GL_INT,
      0, 0, 0, 8,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_A_SINT16,
      "MESA_FORMAT_A_SINT16",
      GL_ALPHA,
      GL_INT,
      0, 0, 0, 16,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_A_SINT32,
      "MESA_FORMAT_A_SINT32",
      GL_ALPHA,
      GL_INT,
      0, 0, 0, 32,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_I_UINT8,
      "MESA_FORMAT_I_UINT8",
      GL_INTENSITY,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      0, 8, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_I_UINT16,
      "MESA_FORMAT_I_UINT16",
      GL_INTENSITY,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      0, 16, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_I_UINT32,
      "MESA_FORMAT_I_UINT32",
      GL_INTENSITY,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      0, 32, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_I_SINT8,
      "MESA_FORMAT_I_SINT8",
      GL_INTENSITY,
      GL_INT,
      0, 0, 0, 0,
      0, 8, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_I_SINT16,
      "MESA_FORMAT_I_SINT16",
      GL_INTENSITY,
      GL_INT,
      0, 0, 0, 0,
      0, 16, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_I_SINT32,
      "MESA_FORMAT_I_SINT32",
      GL_INTENSITY,
      GL_INT,
      0, 0, 0, 0,
      0, 32, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_L_UINT8,
      "MESA_FORMAT_L_UINT8",
      GL_LUMINANCE,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      8, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_L_UINT16,
      "MESA_FORMAT_L_UINT16",
      GL_LUMINANCE,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      16, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L_UINT32,
      "MESA_FORMAT_L_UINT32",
      GL_LUMINANCE,
      GL_UNSIGNED_INT,
      0, 0, 0, 0,
      32, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_L_SINT8,
      "MESA_FORMAT_L_SINT8",
      GL_LUMINANCE,
      GL_INT,
      0, 0, 0, 0,
      8, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_L_SINT16,
      "MESA_FORMAT_L_SINT16",
      GL_LUMINANCE,
      GL_INT,
      0, 0, 0, 0,
      16, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_L_SINT32,
      "MESA_FORMAT_L_SINT32",
      GL_LUMINANCE,
      GL_INT,
      0, 0, 0, 0,
      32, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_LA_UINT8,
      "MESA_FORMAT_LA_UINT8",
      GL_LUMINANCE_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 8,
      8, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_LA_UINT16,
      "MESA_FORMAT_LA_UINT16",
      GL_LUMINANCE_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 16,
      16, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_LA_UINT32,
      "MESA_FORMAT_LA_UINT32",
      GL_LUMINANCE_ALPHA,
      GL_UNSIGNED_INT,
      0, 0, 0, 32,
      32, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_LA_SINT8,
      "MESA_FORMAT_LA_SINT8",
      GL_LUMINANCE_ALPHA,
      GL_INT,
      0, 0, 0, 8,
      8, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_LA_SINT16,
      "MESA_FORMAT_LA_SINT16",
      GL_LUMINANCE_ALPHA,
      GL_INT,
      0, 0, 0, 16,
      16, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_LA_SINT32,
      "MESA_FORMAT_LA_SINT32",
      GL_LUMINANCE_ALPHA,
      GL_INT,
      0, 0, 0, 32,
      32, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_R_UINT8,
      "MESA_FORMAT_R_UINT8",
      GL_RED,
      GL_UNSIGNED_INT,
      8, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_R_UINT16,
      "MESA_FORMAT_R_UINT16",
      GL_RED,
      GL_UNSIGNED_INT,
      16, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_R_UINT32,
      "MESA_FORMAT_R_UINT32",
      GL_RED,
      GL_UNSIGNED_INT,
      32, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_R_SINT8,
      "MESA_FORMAT_R_SINT8",
      GL_RED,
      GL_INT,
      8, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 1
   },
   {
      MESA_FORMAT_R_SINT16,
      "MESA_FORMAT_R_SINT16",
      GL_RED,
      GL_INT,
      16, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_R_SINT32,
      "MESA_FORMAT_R_SINT32",
      GL_RED,
      GL_INT,
      32, 0, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RG_UINT8,
      "MESA_FORMAT_RG_UINT8",
      GL_RG,
      GL_UNSIGNED_INT,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_RG_UINT16,
      "MESA_FORMAT_RG_UINT16",
      GL_RG,
      GL_UNSIGNED_INT,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RG_UINT32,
      "MESA_FORMAT_RG_UINT32",
      GL_RG,
      GL_UNSIGNED_INT,
      32, 32, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RG_SINT8,
      "MESA_FORMAT_RG_SINT8",
      GL_RG,
      GL_INT,
      8, 8, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 2
   },
   {
      MESA_FORMAT_RG_SINT16,
      "MESA_FORMAT_RG_SINT16",
      GL_RG,
      GL_INT,
      16, 16, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RG_SINT32,
      "MESA_FORMAT_RG_SINT32",
      GL_RG,
      GL_INT,
      32, 32, 0, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGB_UINT8,
      "MESA_FORMAT_RGB_UINT8",
      GL_RGB,
      GL_UNSIGNED_INT,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 3
   },
   {
      MESA_FORMAT_RGB_UINT16,
      "MESA_FORMAT_RGB_UINT16",
      GL_RGB,
      GL_UNSIGNED_INT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 6
   },
   {
      MESA_FORMAT_RGB_UINT32,
      "MESA_FORMAT_RGB_UINT32",
      GL_RGB,
      GL_UNSIGNED_INT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 12
   },
   {
      MESA_FORMAT_RGB_SINT8,
      "MESA_FORMAT_RGB_SINT8",
      GL_RGB,
      GL_INT,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 3
   },
   {
      MESA_FORMAT_RGB_SINT16,
      "MESA_FORMAT_RGB_SINT16",
      GL_RGB,
      GL_INT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 6
   },
   {
      MESA_FORMAT_RGB_SINT32,
      "MESA_FORMAT_RGB_SINT32",
      GL_RGB,
      GL_INT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 12
   },
   {
      MESA_FORMAT_RGBA_UINT8,
      "MESA_FORMAT_RGBA_UINT8",
      GL_RGBA,
      GL_UNSIGNED_INT,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RGBA_UINT16,
      "MESA_FORMAT_RGBA_UINT16",
      GL_RGBA,
      GL_UNSIGNED_INT,
      16, 16, 16, 16,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBA_UINT32,
      "MESA_FORMAT_RGBA_UINT32",
      GL_RGBA,
      GL_UNSIGNED_INT,
      32, 32, 32, 32,
      0, 0, 0, 0, 0,
      1, 1, 16
   },
   {
      MESA_FORMAT_RGBA_SINT8,
      "MESA_FORMAT_RGBA_SINT8",
      GL_RGBA,
      GL_INT,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RGBA_SINT16,
      "MESA_FORMAT_RGBA_SINT16",
      GL_RGBA,
      GL_INT,
      16, 16, 16, 16,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBA_SINT32,
      "MESA_FORMAT_RGBA_SINT32",
      GL_RGBA,
      GL_INT,
      32, 32, 32, 32,
      0, 0, 0, 0, 0,
      1, 1, 16
   },
   {
      MESA_FORMAT_RGBX_UINT8,
      "MESA_FORMAT_RGBX_UINT8",
      GL_RGB,
      GL_UNSIGNED_INT,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RGBX_UINT16,
      "MESA_FORMAT_RGBX_UINT16",
      GL_RGB,
      GL_UNSIGNED_INT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBX_UINT32,
      "MESA_FORMAT_RGBX_UINT32",
      GL_RGB,
      GL_UNSIGNED_INT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 16
   },
   {
      MESA_FORMAT_RGBX_SINT8,
      "MESA_FORMAT_RGBX_SINT8",
      GL_RGB,
      GL_INT,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      1, 1, 4
   },
   {
      MESA_FORMAT_RGBX_SINT16,
      "MESA_FORMAT_RGBX_SINT16",
      GL_RGB,
      GL_INT,
      16, 16, 16, 0,
      0, 0, 0, 0, 0,
      1, 1, 8
   },
   {
      MESA_FORMAT_RGBX_SINT32,
      "MESA_FORMAT_RGBX_SINT32",
      GL_RGB,
      GL_INT,
      32, 32, 32, 0,
      0, 0, 0, 0, 0,
      1, 1, 16
   },

   /* DXT compressed formats */
   {
      MESA_FORMAT_RGB_DXT1,        /* Name */
      "MESA_FORMAT_RGB_DXT1",      /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      4, 4, 4, 0,                  /* approx Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      4, 4, 8                      /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_RGBA_DXT1,
      "MESA_FORMAT_RGBA_DXT1",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 8                      /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_RGBA_DXT3,
      "MESA_FORMAT_RGBA_DXT3",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 16                     /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_RGBA_DXT5,
      "MESA_FORMAT_RGBA_DXT5",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,    
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 16                     /* 16 bytes per 4x4 block */
   },

   /* DXT sRGB compressed formats */
   {
      MESA_FORMAT_SRGB_DXT1,       /* Name */
      "MESA_FORMAT_SRGB_DXT1",     /* StrName */
      GL_RGB,                      /* BaseFormat */
      GL_UNSIGNED_NORMALIZED,      /* DataType */
      4, 4, 4, 0,                  /* approx Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,               /* Lum/Int/Index/Depth/StencilBits */
      4, 4, 8                      /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_SRGBA_DXT1,
      "MESA_FORMAT_SRGBA_DXT1",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 8                      /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_SRGBA_DXT3,
      "MESA_FORMAT_SRGBA_DXT3",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 16                     /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_SRGBA_DXT5,
      "MESA_FORMAT_SRGBA_DXT5",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 4,
      0, 0, 0, 0, 0,
      4, 4, 16                     /* 16 bytes per 4x4 block */
   },

   /* FXT1 compressed formats */
   {
      MESA_FORMAT_RGB_FXT1,
      "MESA_FORMAT_RGB_FXT1",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 0,                  /* approx Red/Green/BlueBits */
      0, 0, 0, 0, 0,
      8, 4, 16                     /* 16 bytes per 8x4 block */
   },
   {
      MESA_FORMAT_RGBA_FXT1,
      "MESA_FORMAT_RGBA_FXT1",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      4, 4, 4, 1,                  /* approx Red/Green/Blue/AlphaBits */
      0, 0, 0, 0, 0,
      8, 4, 16                     /* 16 bytes per 8x4 block */
   },

   /* RGTC compressed formats */
   {
     MESA_FORMAT_R_RGTC1_UNORM,
     "MESA_FORMAT_R_RGTC1_UNORM",
     GL_RED,
     GL_UNSIGNED_NORMALIZED,
     8, 0, 0, 0,
     0, 0, 0, 0, 0,
     4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_R_RGTC1_SNORM,
     "MESA_FORMAT_R_RGTC1_SNORM",
     GL_RED,
     GL_SIGNED_NORMALIZED,
     8, 0, 0, 0,
     0, 0, 0, 0, 0,
     4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_RG_RGTC2_UNORM,
     "MESA_FORMAT_RG_RGTC2_UNORM",
     GL_RG,
     GL_UNSIGNED_NORMALIZED,
     8, 8, 0, 0,
     0, 0, 0, 0, 0,
     4, 4, 16                     /* 16 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_RG_RGTC2_SNORM,
     "MESA_FORMAT_RG_RGTC2_SNORM",
     GL_RG,
     GL_SIGNED_NORMALIZED,
     8, 8, 0, 0,
     0, 0, 0, 0, 0,
     4, 4, 16                     /* 16 bytes per 4x4 block */
   },

   /* LATC1/2 compressed formats */
   {
     MESA_FORMAT_L_LATC1_UNORM,
     "MESA_FORMAT_L_LATC1_UNORM",
     GL_LUMINANCE,
     GL_UNSIGNED_NORMALIZED,
     0, 0, 0, 0,
     4, 0, 0, 0, 0,
     4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_L_LATC1_SNORM,
     "MESA_FORMAT_L_LATC1_SNORM",
     GL_LUMINANCE,
     GL_SIGNED_NORMALIZED,
     0, 0, 0, 0,
     4, 0, 0, 0, 0,
     4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_LA_LATC2_UNORM,
     "MESA_FORMAT_LA_LATC2_UNORM",
     GL_LUMINANCE_ALPHA,
     GL_UNSIGNED_NORMALIZED,
     0, 0, 0, 4,
     4, 0, 0, 0, 0,
     4, 4, 16                     /* 16 bytes per 4x4 block */
   },
   {
     MESA_FORMAT_LA_LATC2_SNORM,
     "MESA_FORMAT_LA_LATC2_SNORM",
     GL_LUMINANCE_ALPHA,
     GL_SIGNED_NORMALIZED,
     0, 0, 0, 4,
     4, 0, 0, 0, 0,
     4, 4, 16                     /* 16 bytes per 4x4 block */
   },

   /* ETC1/2 compressed formats */
   {
      MESA_FORMAT_ETC1_RGB8,
      "MESA_FORMAT_ETC1_RGB8",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_RGB8,
      "MESA_FORMAT_ETC2_RGB8",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_SRGB8,
      "MESA_FORMAT_ETC2_SRGB8",
      GL_RGB,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 0,
      0, 0, 0, 0, 0,
      4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_RGBA8_EAC,
      "MESA_FORMAT_ETC2_RGBA8_EAC",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      4, 4, 16                    /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC,
      "MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 8,
      0, 0, 0, 0, 0,
      4, 4, 16                    /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_R11_EAC,
      "MESA_FORMAT_ETC2_R11_EAC",
      GL_RED,
      GL_UNSIGNED_NORMALIZED,
      11, 0, 0, 0,
      0, 0, 0, 0, 0,
      4, 4, 8                    /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_RG11_EAC,
      "MESA_FORMAT_ETC2_RG11_EAC",
      GL_RG,
      GL_UNSIGNED_NORMALIZED,
      11, 11, 0, 0,
      0, 0, 0, 0, 0,
      4, 4, 16                    /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_SIGNED_R11_EAC,
      "MESA_FORMAT_ETC2_SIGNED_R11_EAC",
      GL_RED,
      GL_SIGNED_NORMALIZED,
      11, 0, 0, 0,
      0, 0, 0, 0, 0,
      4, 4, 8                    /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_SIGNED_RG11_EAC,
      "MESA_FORMAT_ETC2_SIGNED_RG11_EAC",
      GL_RG,
      GL_SIGNED_NORMALIZED,
      11, 11, 0, 0,
      0, 0, 0, 0, 0,
      4, 4, 16                    /* 16 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1,
      "MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 1,
      0, 0, 0, 0, 0,
      4, 4, 8                     /* 8 bytes per 4x4 block */
   },
   {
      MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1,
      "MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1",
      GL_RGBA,
      GL_UNSIGNED_NORMALIZED,
      8, 8, 8, 1,
      0, 0, 0, 0, 0,
      4, 4, 8                     /* 8 bytes per 4x4 block */
   },
};

static const struct gl_format_info *
_mesa_get_format_info(mesa_format format)
{
   const struct gl_format_info *info = &format_info[format];
   assert(info->Name == format);
   return info;
}


/** Return string name of format (for debugging) */
const char *
_mesa_get_format_name(mesa_format format)
{
   const struct gl_format_info *info = _mesa_get_format_info(format);
   return info->StrName;
}

/**
 * Return bits per component for the given format.
 * \param format  one of MESA_FORMAT_x
 * \param pname  the component, such as GL_RED_BITS, GL_TEXTURE_BLUE_BITS, etc.
 */
GLint
_mesa_get_format_bits(mesa_format format, GLenum pname)
{
   const struct gl_format_info *info = _mesa_get_format_info(format);

   switch (pname) {
   case GL_RED_BITS:
   case GL_TEXTURE_RED_SIZE:
   case GL_RENDERBUFFER_RED_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
      return info->RedBits;
   case GL_GREEN_BITS:
   case GL_TEXTURE_GREEN_SIZE:
   case GL_RENDERBUFFER_GREEN_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
      return info->GreenBits;
   case GL_BLUE_BITS:
   case GL_TEXTURE_BLUE_SIZE:
   case GL_RENDERBUFFER_BLUE_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
      return info->BlueBits;
   case GL_ALPHA_BITS:
   case GL_TEXTURE_ALPHA_SIZE:
   case GL_RENDERBUFFER_ALPHA_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
      return info->AlphaBits;
   case GL_TEXTURE_INTENSITY_SIZE:
      return info->IntensityBits;
   case GL_TEXTURE_LUMINANCE_SIZE:
      return info->LuminanceBits;
   case GL_INDEX_BITS:
      return info->IndexBits;
   case GL_DEPTH_BITS:
   case GL_TEXTURE_DEPTH_SIZE_ARB:
   case GL_RENDERBUFFER_DEPTH_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      return info->DepthBits;
   case GL_STENCIL_BITS:
   case GL_TEXTURE_STENCIL_SIZE_EXT:
   case GL_RENDERBUFFER_STENCIL_SIZE_EXT:
   case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      return info->StencilBits;
   default:
      ErrorF("bad pname in _mesa_get_format_bits(): %d\n",pname);
      return 0;
   }
}

/**
 * Return color encoding for given format.
 * \return GL_LINEAR or GL_SRGB
 */
GLenum
_mesa_get_format_color_encoding(mesa_format format)
{
   /* XXX this info should be encoded in gl_format_info */
   switch (format) {
   case MESA_FORMAT_BGR_SRGB8:
   case MESA_FORMAT_A8B8G8R8_SRGB:
   case MESA_FORMAT_B8G8R8A8_SRGB:
   case MESA_FORMAT_R8G8B8A8_SRGB:
   case MESA_FORMAT_L_SRGB8:
   case MESA_FORMAT_L8A8_SRGB:
   case MESA_FORMAT_SRGB_DXT1:
   case MESA_FORMAT_SRGBA_DXT1:
   case MESA_FORMAT_SRGBA_DXT3:
   case MESA_FORMAT_SRGBA_DXT5:
   case MESA_FORMAT_R8G8B8X8_SRGB:
   case MESA_FORMAT_ETC2_SRGB8:
   case MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC:
   case MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1:
   case MESA_FORMAT_B8G8R8X8_SRGB:
      return GL_SRGB;
   default:
      return GL_LINEAR;
   }
}

__DRIconfig **
driCreateConfigs(mesa_format format,
     const uint8_t * depth_bits, const uint8_t * stencil_bits,
     unsigned num_depth_stencil_bits,
     const GLenum * db_modes, unsigned num_db_modes,
     const uint8_t * msaa_samples, unsigned num_msaa_modes,
     GLboolean enable_accum)
{
   static const uint32_t masks_table[][4] = {
      /* MESA_FORMAT_B5G6R5_UNORM */
      { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 },
      /* MESA_FORMAT_B8G8R8X8_UNORM */
      { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 },
      /* MESA_FORMAT_B8G8R8A8_UNORM */
      { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 },
      /* MESA_FORMAT_B10G10R10X2_UNORM */
      { 0x3FF00000, 0x000FFC00, 0x000003FF, 0x00000000 },
      /* MESA_FORMAT_B10G10R10A2_UNORM */
      { 0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000 },
   };

   const uint32_t * masks;
   __DRIconfig **configs, **c;
   struct gl_config *modes;
   unsigned i, j, k, h;
   unsigned num_modes;
   unsigned num_accum_bits = (enable_accum) ? 2 : 1;
   int red_bits;
   int green_bits;
   int blue_bits;
   int alpha_bits;
   bool is_srgb;

   switch (format) {
   case MESA_FORMAT_B5G6R5_UNORM:
      masks = masks_table[0];
      break;
   case MESA_FORMAT_B8G8R8X8_UNORM:
      masks = masks_table[1];
      break;
   case MESA_FORMAT_B8G8R8A8_UNORM:
   case MESA_FORMAT_B8G8R8A8_SRGB:
      masks = masks_table[2];
      break;
   case MESA_FORMAT_B10G10R10X2_UNORM:
      masks = masks_table[3];
      break;
   case MESA_FORMAT_B10G10R10A2_UNORM:
      masks = masks_table[4];
      break;
   default:
      fprintf(stderr, "[%s:%u] Unknown framebuffer type %s (%d).\n",
              __FUNCTION__, __LINE__,
              _mesa_get_format_name(format), format);
      return NULL;
   }

   red_bits = _mesa_get_format_bits(format, GL_RED_BITS);
   green_bits = _mesa_get_format_bits(format, GL_GREEN_BITS);
   blue_bits = _mesa_get_format_bits(format, GL_BLUE_BITS);
   alpha_bits = _mesa_get_format_bits(format, GL_ALPHA_BITS);
   is_srgb = _mesa_get_format_color_encoding(format) == GL_SRGB;

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
                   modes->redBits   = red_bits;
                   modes->greenBits = green_bits;
                   modes->blueBits  = blue_bits;
                   modes->alphaBits = alpha_bits;
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

                   modes->yInverted = GL_TRUE;
                   modes->sRGBCapable = is_srgb;
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
    mesa_format format;

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
        case 16:
            format = MESA_FORMAT_B5G6R5_UNORM;
            break;
        case 24:
            format = MESA_FORMAT_B8G8R8X8_UNORM;
            break;
        case 32:
            format = MESA_FORMAT_B8G8R8A8_UNORM;
            break;
        default:
            fprintf(stderr, "[%s:%u] bad depth %d\n", __FUNCTION__, __LINE__,
                    pixel_bits);
            return NULL;
    }

    configs = driCreateConfigs(format,
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

__DRIconfig **driConcatConfigs(__DRIconfig **a,
                               __DRIconfig **b)
{
    __DRIconfig **all;
    int i, j, index;

    if (a == NULL || a[0] == NULL)
       return b;
    else if (b == NULL || b[0] == NULL)
       return a;

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

/**
 * Return the value of a configuration attribute.  The attribute is
 * indicated by the index.
 */
static int
driGetConfigAttribIndex(const __DRIconfig *config,
      unsigned int index, unsigned int *value)
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

    default:
        /* any other int-sized field */
  *value = *(unsigned int *)
      ((char *) &config->modes + attribMap[index].offset);

  break;
    }

    return GL_TRUE;
}


/**
 * Get the value of a configuration attribute.
 * \param attrib  the attribute (one of the _DRI_ATTRIB_x tokens)
 * \param value  returns the attribute's value
 * \return 1 for success, 0 for failure
 */
int
driGetConfigAttrib(const __DRIconfig *config, unsigned int attrib, unsigned int *value)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(attribMap); i++)
    if (attribMap[i].attrib == attrib)
      return driGetConfigAttribIndex(config, i, value);

  return GL_FALSE;
}

/**
 * Get a configuration attribute name and value, given an index.
 * \param index  which field of the __DRIconfig to query
 * \param attrib  returns the attribute name (one of the _DRI_ATTRIB_x tokens)
 * \param value  returns the attribute's value
 * \return 1 for success, 0 for failure
 */
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

    psp->max_gl_compat_version = 21;
    psp->max_gl_es1_version = 11;
    psp->max_gl_es2_version = 20;

    psp->extensions = dri_screen_extensions;

    configs16 = swrastFillInModes(psp, 16, 16, 0, 1);
    configs24 = swrastFillInModes(psp, 24, 24, 8, 1);
    configs32 = swrastFillInModes(psp, 32, 24, 8, 1);

    configs24 = driConcatConfigs(configs16, configs24);
    configs32 = driConcatConfigs(configs24, configs32);

    return (const __DRIconfig **)configs32;
}

static __DRIscreen *driCreateNewScreen(int scrn, const __DRIextension **extensions, const __DRIconfig ***driver_configs, void *data)
{
  static const __DRIextension *emptyExtensionList[] = { NULL };
  __DRIscreen *psp;

  HDC hDc = GetDC(NULL);
  int bitsPerPixel= GetDeviceCaps(hDc, BITSPIXEL);
  ReleaseDC(NULL, hDc);
  if (bitsPerPixel<24)
  {
    PRINTF(__FUNCTION__": bitsPerPixel not supported %d\n", bitsPerPixel);
    return NULL;
  }

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
  driGetConfigAttrib,
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
