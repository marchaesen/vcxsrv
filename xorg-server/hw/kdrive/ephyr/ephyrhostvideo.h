/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2007 OpenedHand Ltd 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */
#ifndef __EPHYRHOSTVIDEO_H__
#define __EPHYRHOSTVIDEO_H__

typedef void *EphyrHostXVAdaptor;
typedef struct _EphyrHostXVAdaptorArray EphyrHostXVAdaptorArray;

typedef struct _EphyrHostVideoFormat {
    char depth;
    short visual_class;
} EphyrHostVideoFormat;

typedef struct _EphyrHostRational {
    int numerator;
    int denominator;
} EphyrHostRational;

typedef struct _EphyrHostEncoding {
    int id;
    char *name;
    unsigned short width, height;
    EphyrHostRational rate;
} EphyrHostEncoding;

typedef struct _EphyrHostAttribute {
    int flags;
    int min_value;
    int max_value;
    char *name;
} EphyrHostAttribute;

typedef struct _EphyrHostImageFormat {
    int id;                     /* Unique descriptor for the format */
    int type;                   /* XvRGB, XvYUV */
    int byte_order;             /* LSBFirst, MSBFirst */
    char guid[16];              /* Globally Unique IDentifier */
    int bits_per_pixel;
    int format;                 /* XvPacked, XvPlanar */
    int num_planes;

    /* for RGB formats only */
    int depth;
    unsigned int red_mask;
    unsigned int green_mask;
    unsigned int blue_mask;

    /* for YUV formats only */
    unsigned int y_sample_bits;
    unsigned int u_sample_bits;
    unsigned int v_sample_bits;
    unsigned int horz_y_period;
    unsigned int horz_u_period;
    unsigned int horz_v_period;
    unsigned int vert_y_period;
    unsigned int vert_u_period;
    unsigned int vert_v_period;
    char component_order[32];   /* eg. UYVY */
    int scanline_order;         /* XvTopToBottom, XvBottomToTop */
} EphyrHostImageFormat;

typedef struct {
    unsigned short x1, y1, x2, y2;
} EphyrHostBox;

void ephyrHostXVInit(void);

void ephyrHostFree(void *a_pointer);

/*
 * host adaptor array
 */
Bool ephyrHostXVQueryAdaptors(EphyrHostXVAdaptorArray ** a_adaptors);
void ephyrHostXVAdaptorArrayDelete(EphyrHostXVAdaptorArray * a_adaptors);
int ephyrHostXVAdaptorArrayGetSize(const EphyrHostXVAdaptorArray * a_this);
EphyrHostXVAdaptor *ephyrHostXVAdaptorArrayAt(const EphyrHostXVAdaptorArray *
                                              a_this, int a_index);

/*
 * host adaptor
 */

char ephyrHostXVAdaptorGetType(const EphyrHostXVAdaptor * a_this);
const char *ephyrHostXVAdaptorGetName(const EphyrHostXVAdaptor * a_this);
EphyrHostVideoFormat *ephyrHostXVAdaptorGetVideoFormats
    (const EphyrHostXVAdaptor * a_this, int *a_nb_formats);
int ephyrHostXVAdaptorGetNbPorts(const EphyrHostXVAdaptor * a_this);
int ephyrHostXVAdaptorGetFirstPortID(const EphyrHostXVAdaptor * a_this);

Bool ephyrHostXVAdaptorHasPutVideo(const EphyrHostXVAdaptor * a_this,
                                   Bool *a_result);
Bool ephyrHostXVAdaptorHasGetVideo(const EphyrHostXVAdaptor * a_this,
                                   Bool *a_result);
Bool ephyrHostXVAdaptorHasPutStill(const EphyrHostXVAdaptor * a_this,
                                   Bool *a_result);
Bool ephyrHostXVAdaptorHasGetStill(const EphyrHostXVAdaptor * a_this,
                                   Bool *a_result);
Bool ephyrHostXVAdaptorHasPutImage(const EphyrHostXVAdaptor * a_this,
                                   Bool *a_result);

/*
 * encoding
 */
Bool ephyrHostXVQueryEncodings(int a_port_id,
                               EphyrHostEncoding ** a_encodings,
                               unsigned int *a_num_encodings);

void ephyrHostEncodingsDelete(EphyrHostEncoding * a_encodings,
                              int a_num_encodings);

/*
 * attribute
 */
Bool ephyrHostXVQueryPortAttributes(int a_port_id,
                                    EphyrHostAttribute ** a_attributes,
                                    int *a_num_attributes);

void ephyrHostAttributesDelete(EphyrHostAttribute * a_attributes);

/*
 * image format
 */

Bool ephyrHostXVQueryImageFormats(int a_port_id,
                                  EphyrHostImageFormat ** a_formats,
                                  int *a_num_format);
/*
 * Port Attribute Get/Set
 */
Bool ephyrHostXVSetPortAttribute(int a_port_id, int a_atom, int a_attr_value);
Bool ephyrHostXVGetPortAttribute(int a_port_id, int a_atom, int *a_attr_value);

/*
 *size query
 */
Bool ephyrHostXVQueryBestSize(int a_port_id,
                              Bool a_motion,
                              unsigned int a_frame_w,
                              unsigned int a_frame_h,
                              unsigned int a_drw_w,
                              unsigned int a_drw_h,
                              unsigned int *a_actual_w,
                              unsigned int *a_actual_h);

Bool ephyrHostXVQueryImageAttributes(int a_port_id,
                                     int a_image_id /*image fourcc code */ ,
                                     unsigned short *a_width,
                                     unsigned short *a_height,
                                     int *a_image_size,
                                     int *a_pitches, int *a_offsets);
/*
 * atom
 */
Bool ephyrHostGetAtom(const char *a_name,
                      Bool a_create_if_not_exists, int *a_atom);
char *ephyrHostGetAtomName(int a_atom);

/*
 *PutImage
 * (ignore clipping for now)
 */
Bool ephyrHostXVPutImage(int a_screen_num,
                         int a_port_id,
                         int a_image_id,
                         int a_drw_x,
                         int a_drw_y,
                         int a_drw_w,
                         int a_drw_h,
                         int a_src_x,
                         int a_src_y,
                         int a_src_w,
                         int a_src_h,
                         int a_image_width,
                         int a_image_height,
                         unsigned char *a_buf,
                         EphyrHostBox * a_clip_rects, int a_clip_rect_nums);

/*
 * Putvideo/PutStill/GetVideo
 */
Bool ephyrHostXVPutVideo(int a_screen_num,
                         int a_port_id,
                         int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                         int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h);

Bool ephyrHostXVGetVideo(int a_screen_num,
                         int a_port_id,
                         int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                         int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h);

Bool ephyrHostXVPutStill(int a_screen_num,
                         int a_port_id,
                         int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                         int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h);

Bool ephyrHostXVGetStill(int a_screen_num,
                         int a_port_id,
                         int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                         int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h);

/*
 * StopVideo
 */
Bool ephyrHostXVStopVideo(int a_screen_num, int a_port_id);

#endif /*__EPHYRHOSTVIDEO_H__*/
