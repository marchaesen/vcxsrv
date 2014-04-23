/*
 * Copyright © 2013 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *
 * some code is derived from the xf86-video-ati radeon driver, mainly
 * the calculations.
 */

/** @file glamor_xv.c
 *
 * Xv acceleration implementation
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xf86xv.h"
#define GLAMOR_FOR_XORG
#include "glamor_priv.h"

#include <X11/extensions/Xv.h>
#include "fourcc.h"
/* Reference color space transform data */
typedef struct tagREF_TRANSFORM {
    float RefLuma;
    float RefRCb;
    float RefRCr;
    float RefGCb;
    float RefGCr;
    float RefBCb;
    float RefBCr;
} REF_TRANSFORM;

#define RTFSaturation(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFBrightness(a)   (((a)*1.0)/2000.0)
#define RTFIntensity(a)   (((a)*1.0)/2000.0)
#define RTFContrast(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFHue(a)   (((a)*3.1416)/1000.0)

static const char *xv_vs = "attribute vec4 v_position;\n"
    "attribute vec4 v_texcoord0;\n"
    "varying vec2 tcs;\n"
    "void main()\n"
    "{\n"
    "     gl_Position = v_position;\n"
    "tcs = v_texcoord0.xy;\n"
    "}\n";

static const char *xv_ps = GLAMOR_DEFAULT_PRECISION
    "uniform sampler2D y_sampler;\n"
    "uniform sampler2D u_sampler;\n"
    "uniform sampler2D v_sampler;\n"
    "uniform vec4 offsetyco;\n"
    "uniform vec4 ucogamma;\n"
    "uniform vec4 vco;\n"
    "varying vec2 tcs;\n"
    "float sample;\n"
    "vec4 temp1;\n"
    "void main()\n"
    "{\n"
    "sample = texture2D(y_sampler, tcs).w;\n"
    "temp1.xyz = offsetyco.www * vec3(sample) + offsetyco.xyz;\n"
    "sample = texture2D(u_sampler, tcs).w;\n"
    "temp1.xyz = ucogamma.xyz * vec3(sample) + temp1.xyz;\n"
    "sample = texture2D(v_sampler, tcs).w;\n"
    "temp1.xyz = clamp(vco.xyz * vec3(sample) + temp1.xyz, 0.0, 1.0);\n"
    "temp1.w = 1.0;\n"
    "gl_FragColor = temp1;\n"
    "}\n";

void
glamor_init_xv_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    GLint fs_prog, vs_prog;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glamor_priv->xv_prog = glCreateProgram();

    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, xv_vs);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, xv_ps);
    glAttachShader(glamor_priv->xv_prog, vs_prog);
    glAttachShader(glamor_priv->xv_prog, fs_prog);

    glBindAttribLocation(glamor_priv->xv_prog,
                         GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(glamor_priv->xv_prog,
                         GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glamor_link_glsl_prog(screen, glamor_priv->xv_prog, "xv");
}

#define ClipValue(v,min,max) ((v) < (min) ? (min) : (v) > (max) ? (max) : (v))
#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvContrast, xvSaturation, xvHue, xvColorspace,
    xvGamma;

#define NUM_ATTRIBUTES 5
static XF86AttributeRec Attributes_glamor[NUM_ATTRIBUTES + 1] = {
    {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
    {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
    {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
    {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
    {XvSettable | XvGettable, 0, 1, "XV_COLORSPACE"},
    {0, 0, 0, NULL}
};

#define NUM_FORMATS 3

static XF86VideoFormatRec Formats[NUM_FORMATS] = {
    {15, TrueColor}, {16, TrueColor}, {24, TrueColor}
};

#define NUM_IMAGES 2

static XF86ImageRec Images[NUM_IMAGES] = {
    XVIMAGE_YV12,
    XVIMAGE_I420,
};

static void
glamor_xv_stop_video(ScrnInfoPtr pScrn, void *data, Bool cleanup)
{
    glamor_port_private *port_priv = (glamor_port_private *) data;
    int i;

    if (!cleanup)
        return;

    for (i = 0; i < 3; i++) {
        if (port_priv->src_pix[i]) {
            glamor_destroy_pixmap(port_priv->src_pix[i]);
            port_priv->src_pix[i] = NULL;
        }
    }
}

static int
glamor_xv_set_port_attribute(ScrnInfoPtr pScrn,
                             Atom attribute, INT32 value, void *data)
{
    glamor_port_private *port_priv = (glamor_port_private *) data;

    if (attribute == xvBrightness)
        port_priv->brightness = ClipValue(value, -1000, 1000);
    else if (attribute == xvHue)
        port_priv->hue = ClipValue(value, -1000, 1000);
    else if (attribute == xvContrast)
        port_priv->contrast = ClipValue(value, -1000, 1000);
    else if (attribute == xvSaturation)
        port_priv->saturation = ClipValue(value, -1000, 1000);
    else if (attribute == xvGamma)
        port_priv->gamma = ClipValue(value, 100, 10000);
    else if (attribute == xvColorspace)
        port_priv->transform_index = ClipValue(value, 0, 1);
    else
        return BadMatch;
    return Success;
}

static int
glamor_xv_get_port_attribute(ScrnInfoPtr pScrn,
                             Atom attribute, INT32 *value, void *data)
{
    glamor_port_private *port_priv = (glamor_port_private *) data;

    if (attribute == xvBrightness)
        *value = port_priv->brightness;
    else if (attribute == xvHue)
        *value = port_priv->hue;
    else if (attribute == xvContrast)
        *value = port_priv->contrast;
    else if (attribute == xvSaturation)
        *value = port_priv->saturation;
    else if (attribute == xvGamma)
        *value = port_priv->gamma;
    else if (attribute == xvColorspace)
        *value = port_priv->transform_index;
    else
        return BadMatch;

    return Success;
}

static void
glamor_xv_query_best_size(ScrnInfoPtr pScrn,
                          Bool motion,
                          short vid_w, short vid_h,
                          short drw_w, short drw_h,
                          unsigned int *p_w, unsigned int *p_h, void *data)
{
    *p_w = drw_w;
    *p_h = drw_h;
}

static int
glamor_xv_query_image_attributes(ScrnInfoPtr pScrn,
                                 int id,
                                 unsigned short *w, unsigned short *h,
                                 int *pitches, int *offsets)
{
    int size = 0, tmp;

    if (offsets)
        offsets[0] = 0;
    switch (id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        *h = *h;
        *w = *w;
        size = *w;
        if (pitches)
            pitches[0] = size;
        size *= *h;
        if (offsets)
            offsets[1] = size;
        tmp = *w >> 1;
        if (pitches)
            pitches[1] = pitches[2] = tmp;
        tmp *= (*h >> 1);
        size += tmp;
        if (offsets)
            offsets[2] = size;
        size += tmp;
        break;
    }
    return size;
}

/* Parameters for ITU-R BT.601 and ITU-R BT.709 colour spaces
   note the difference to the parameters used in overlay are due
   to 10bit vs. float calcs */
static REF_TRANSFORM trans[2] = {
    {1.1643, 0.0, 1.5960, -0.3918, -0.8129, 2.0172, 0.0},       /* BT.601 */
    {1.1643, 0.0, 1.7927, -0.2132, -0.5329, 2.1124, 0.0}        /* BT.709 */
};

static void
glamor_display_textured_video(glamor_port_private *port_priv)
{
    ScreenPtr screen = port_priv->pPixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(port_priv->pPixmap);
    glamor_pixmap_private *src_pixmap_priv[3];
    float vertices[32], texcoords[8];
    BoxPtr box = REGION_RECTS(&port_priv->clip);
    int nBox = REGION_NUM_RECTS(&port_priv->clip);
    int dst_x_off, dst_y_off;
    GLfloat dst_xscale, dst_yscale;
    GLfloat src_xscale[3], src_yscale[3];
    int i;
    const float Loff = -0.0627;
    const float Coff = -0.502;
    float uvcosf, uvsinf;
    float yco;
    float uco[3], vco[3], off[3];
    float bright, cont, gamma;
    int ref = port_priv->transform_index;
    GLint uloc, sampler_loc;

    cont = RTFContrast(port_priv->contrast);
    bright = RTFBrightness(port_priv->brightness);
    gamma = (float) port_priv->gamma / 1000.0;
    uvcosf = RTFSaturation(port_priv->saturation) * cos(RTFHue(port_priv->hue));
    uvsinf = RTFSaturation(port_priv->saturation) * sin(RTFHue(port_priv->hue));
/* overlay video also does pre-gamma contrast/sat adjust, should we? */

    yco = trans[ref].RefLuma * cont;
    uco[0] = -trans[ref].RefRCr * uvsinf;
    uco[1] = trans[ref].RefGCb * uvcosf - trans[ref].RefGCr * uvsinf;
    uco[2] = trans[ref].RefBCb * uvcosf;
    vco[0] = trans[ref].RefRCr * uvcosf;
    vco[1] = trans[ref].RefGCb * uvsinf + trans[ref].RefGCr * uvcosf;
    vco[2] = trans[ref].RefBCb * uvsinf;
    off[0] = Loff * yco + Coff * (uco[0] + vco[0]) + bright;
    off[1] = Loff * yco + Coff * (uco[1] + vco[1]) + bright;
    off[2] = Loff * yco + Coff * (uco[2] + vco[2]) + bright;
    gamma = 1.0;

    pixmap_priv_get_dest_scale(pixmap_priv, &dst_xscale, &dst_yscale);
    glamor_get_drawable_deltas(port_priv->pDraw, port_priv->pPixmap, &dst_x_off,
                               &dst_y_off);
    glamor_set_destination_pixmap_priv_nc(pixmap_priv);

    for (i = 0; i < 3; i++) {
        if (port_priv->src_pix[i]) {
            src_pixmap_priv[i] =
                glamor_get_pixmap_private(port_priv->src_pix[i]);
            pixmap_priv_get_scale(src_pixmap_priv[i], &src_xscale[i],
                                  &src_yscale[i]);
        }
    }
    glamor_make_current(glamor_priv);
    glUseProgram(glamor_priv->xv_prog);

    uloc = glGetUniformLocation(glamor_priv->xv_prog, "offsetyco");
    glUniform4f(uloc, off[0], off[1], off[2], yco);
    uloc = glGetUniformLocation(glamor_priv->xv_prog, "ucogamma");
    glUniform4f(uloc, uco[0], uco[1], uco[2], gamma);
    uloc = glGetUniformLocation(glamor_priv->xv_prog, "vco");
    glUniform4f(uloc, vco[0], vco[1], vco[2], 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_pixmap_priv[0]->base.fbo->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, src_pixmap_priv[1]->base.fbo->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, src_pixmap_priv[2]->base.fbo->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    sampler_loc = glGetUniformLocation(glamor_priv->xv_prog, "y_sampler");
    glUniform1i(sampler_loc, 0);
    sampler_loc = glGetUniformLocation(glamor_priv->xv_prog, "u_sampler");
    glUniform1i(sampler_loc, 1);
    sampler_loc = glGetUniformLocation(glamor_priv->xv_prog, "v_sampler");
    glUniform1i(sampler_loc, 2);

    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2,
                          GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), texcoords);
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                          GL_FALSE, 2 * sizeof(float), vertices);

    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
    for (i = 0; i < nBox; i++) {
        float off_x = box[i].x1 - port_priv->drw_x;
        float off_y = box[i].y1 - port_priv->drw_y;
        float diff_x = (float) port_priv->src_w / (float) port_priv->dst_w;
        float diff_y = (float) port_priv->src_h / (float) port_priv->dst_h;
        float srcx, srcy, srcw, srch;
        int dstx, dsty, dstw, dsth;

        dstx = box[i].x1 + dst_x_off;
        dsty = box[i].y1 + dst_y_off;
        dstw = box[i].x2 - box[i].x1;
        dsth = box[i].y2 - box[i].y1;

        srcx = port_priv->src_x + off_x * diff_x;
        srcy = port_priv->src_y + off_y * diff_y;
        srcw = (port_priv->src_w * dstw) / (float) port_priv->dst_w;
        srch = (port_priv->src_h * dsth) / (float) port_priv->dst_h;

        glamor_set_normalize_vcoords(pixmap_priv,
                                     dst_xscale, dst_yscale,
                                     dstx,
                                     dsty,
                                     dstx + dstw,
                                     dsty + dsth,
                                     glamor_priv->yInverted, vertices);

        glamor_set_normalize_tcoords(src_pixmap_priv[0],
                                     src_xscale[0],
                                     src_yscale[0],
                                     srcx,
                                     srcy,
                                     srcx + srcw,
                                     srcy + srch,
                                     glamor_priv->yInverted, texcoords);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

    DamageDamageRegion(port_priv->pDraw, &port_priv->clip);
}

static int
glamor_xv_put_image(ScrnInfoPtr pScrn,
                    short src_x, short src_y,
                    short drw_x, short drw_y,
                    short src_w, short src_h,
                    short drw_w, short drw_h,
                    int id,
                    unsigned char *buf,
                    short width,
                    short height,
                    Bool sync,
                    RegionPtr clipBoxes, void *data, DrawablePtr pDrawable)
{
    ScreenPtr screen = pDrawable->pScreen;
    glamor_port_private *port_priv = (glamor_port_private *) data;
    INT32 x1, x2, y1, y2;
    int srcPitch, srcPitch2;
    BoxRec dstBox;
    int top, nlines;
    int s2offset, s3offset, tmp;

    s2offset = s3offset = srcPitch2 = 0;

    /* Clip */
    x1 = src_x;
    x2 = src_x + src_w;
    y1 = src_y;
    y2 = src_y + src_h;

    dstBox.x1 = drw_x;
    dstBox.x2 = drw_x + drw_w;
    dstBox.y1 = drw_y;
    dstBox.y2 = drw_y + drw_h;
    if (!xf86XVClipVideoHelper
        (&dstBox, &x1, &x2, &y1, &y2, clipBoxes, width, height))
        return Success;

    if ((x1 >= x2) || (y1 >= y2))
        return Success;

    srcPitch = width;
    srcPitch2 = width >> 1;

    if (!port_priv->src_pix[0] ||
        (width != port_priv->src_pix_w || height != port_priv->src_pix_h)) {
        int i;

        for (i = 0; i < 3; i++)
            if (port_priv->src_pix[i])
                glamor_destroy_pixmap(port_priv->src_pix[i]);

        port_priv->src_pix[0] =
            glamor_create_pixmap(screen, width, height, 8, 0);
        port_priv->src_pix[1] =
            glamor_create_pixmap(screen, width >> 1, height >> 1, 8, 0);
        port_priv->src_pix[2] =
            glamor_create_pixmap(screen, width >> 1, height >> 1, 8, 0);
        port_priv->src_pix_w = width;
        port_priv->src_pix_h = height;

        if (!port_priv->src_pix[0] || !port_priv->src_pix[1] ||
            !port_priv->src_pix[2])
            return BadAlloc;
    }

    top = (y1 >> 16) & ~1;
    nlines = ((y2 + 0xffff) >> 16) - top;

    switch (id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        s2offset = srcPitch * height;
        s3offset = s2offset + (srcPitch2 * ((height + 1) >> 1));
        s2offset += ((top >> 1) * srcPitch2);
        s3offset += ((top >> 1) * srcPitch2);
        if (id == FOURCC_YV12) {
            tmp = s2offset;
            s2offset = s3offset;
            s3offset = tmp;
        }
        glamor_upload_sub_pixmap_to_texture(port_priv->src_pix[0],
                                            0, 0, srcPitch, nlines,
                                            port_priv->src_pix[0]->devKind,
                                            buf + (top * srcPitch), 0);

        glamor_upload_sub_pixmap_to_texture(port_priv->src_pix[1],
                                            0, 0, srcPitch2, (nlines + 1) >> 1,
                                            port_priv->src_pix[1]->devKind,
                                            buf + s2offset, 0);

        glamor_upload_sub_pixmap_to_texture(port_priv->src_pix[2],
                                            0, 0, srcPitch2, (nlines + 1) >> 1,
                                            port_priv->src_pix[2]->devKind,
                                            buf + s3offset, 0);
        break;
    default:
        return BadMatch;
    }

    if (pDrawable->type == DRAWABLE_WINDOW)
        port_priv->pPixmap = (*screen->GetWindowPixmap) ((WindowPtr) pDrawable);
    else
        port_priv->pPixmap = (PixmapPtr) pDrawable;

    if (!RegionEqual(&port_priv->clip, clipBoxes)) {
        RegionCopy(&port_priv->clip, clipBoxes);
    }

    port_priv->src_x = src_x;
    port_priv->src_y = src_y;
    port_priv->src_w = src_w;
    port_priv->src_h = src_h;
    port_priv->dst_w = drw_w;
    port_priv->dst_h = drw_h;
    port_priv->drw_x = drw_x;
    port_priv->drw_y = drw_y;
    port_priv->w = width;
    port_priv->h = height;
    port_priv->pDraw = pDrawable;
    glamor_display_textured_video(port_priv);
    return Success;
}

static XF86VideoEncodingRec DummyEncodingGLAMOR[1] = {
    {
     0,
     "XV_IMAGE",
     8192, 8192,
     {1, 1}
     }
};

XF86VideoAdaptorPtr
glamor_xv_init(ScreenPtr screen, int num_texture_ports)
{
    glamor_port_private *port_priv;
    XF86VideoAdaptorPtr adapt;
    int i;

    glamor_init_xv_shader(screen);

    adapt = calloc(1, sizeof(XF86VideoAdaptorRec) + num_texture_ports *
                   (sizeof(glamor_port_private) + sizeof(DevUnion)));
    if (adapt == NULL)
        return NULL;

    xvBrightness = MAKE_ATOM("XV_BRIGHTNESS");
    xvContrast = MAKE_ATOM("XV_CONTRAST");
    xvSaturation = MAKE_ATOM("XV_SATURATION");
    xvHue = MAKE_ATOM("XV_HUE");
    xvGamma = MAKE_ATOM("XV_GAMMA");
    xvColorspace = MAKE_ATOM("XV_COLORSPACE");

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = 0;
    adapt->name = "GLAMOR Textured Video";
    adapt->nEncodings = 1;
    adapt->pEncodings = DummyEncodingGLAMOR;

    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = Formats;
    adapt->nPorts = num_texture_ports;
    adapt->pPortPrivates = (DevUnion *) (&adapt[1]);

    adapt->pAttributes = Attributes_glamor;
    adapt->nAttributes = NUM_ATTRIBUTES;

    port_priv =
        (glamor_port_private *) (&adapt->pPortPrivates[num_texture_ports]);
    adapt->pImages = Images;
    adapt->nImages = NUM_IMAGES;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = glamor_xv_stop_video;
    adapt->SetPortAttribute = glamor_xv_set_port_attribute;
    adapt->GetPortAttribute = glamor_xv_get_port_attribute;
    adapt->QueryBestSize = glamor_xv_query_best_size;
    adapt->PutImage = glamor_xv_put_image;
    adapt->ReputImage = NULL;
    adapt->QueryImageAttributes = glamor_xv_query_image_attributes;

    for (i = 0; i < num_texture_ports; i++) {
        glamor_port_private *pPriv = &port_priv[i];

        pPriv->brightness = 0;
        pPriv->contrast = 0;
        pPriv->saturation = 0;
        pPriv->hue = 0;
        pPriv->gamma = 1000;
        pPriv->transform_index = 0;

        REGION_NULL(pScreen, &pPriv->clip);

        adapt->pPortPrivates[i].ptr = (void *) (pPriv);
    }
    return adapt;
}
