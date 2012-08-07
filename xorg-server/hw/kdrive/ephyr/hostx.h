/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@o-hand.com>
 * 
 * Copyright © 2004 Nokia 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Nokia not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Nokia makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * NOKIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL NOKIA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _XLIBS_STUFF_H_
#define _XLIBS_STUFF_H_

#include <X11/X.h>
#include <X11/Xmd.h>

#define EPHYR_WANT_DEBUG 0

#if (EPHYR_WANT_DEBUG)
#define EPHYR_DBG(x, a...) \
 fprintf(stderr, __FILE__ ":%d,%s() " x "\n", __LINE__, __func__, ##a)
#else
#define EPHYR_DBG(x, a...) do {} while (0)
#endif

typedef struct EphyrHostXVars EphyrHostXVars;
typedef struct EphyrHostXEvent EphyrHostXEvent;
typedef void *EphyrScreenInfo;
typedef enum EphyrHostXEventType {
    EPHYR_EV_MOUSE_MOTION,
    EPHYR_EV_MOUSE_PRESS,
    EPHYR_EV_MOUSE_RELEASE,
    EPHYR_EV_KEY_PRESS,
    EPHYR_EV_KEY_RELEASE,
    EPHYR_EV_EXPOSE,
    EPHYR_EV_CONFIGURE,
} EphyrHostXEventType;

/* I can't believe it's not a KeySymsRec. */
typedef struct {
    int minKeyCode;
    int maxKeyCode;
    int mapWidth;
    CARD32 *map;
} EphyrKeySyms;

struct EphyrHostXEvent {
    EphyrHostXEventType type;

    union {
        struct mouse_motion {
            int x;
            int y;
            int screen;
            int window;
        } mouse_motion;

        struct mouse_down {
            int button_num;
        } mouse_down;

        struct mouse_up {
            int button_num;
        } mouse_up;

        struct key_up {
            int scancode;
        } key_up;

        struct key_down {
            int scancode;
        } key_down;

        struct expose {
            int window;
        } expose;

        struct configure {
            int width;
            int height;
            int screen;
            int window;
        } configure;

    } data;

    int key_state;
};

typedef struct {
    VisualID visualid;
    int screen;
    int depth;
    int class;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} EphyrHostVisualInfo;

typedef struct {
    int x, y;
    int width, height;
    int visualid;
} EphyrHostWindowAttributes;

typedef struct {
    int x, y, width, height;
} EphyrBox;

typedef struct {
    short x1, y1, x2, y2;
} EphyrRect;

int
 hostx_want_screen_size(EphyrScreenInfo screen, int *width, int *height);

int
 hostx_want_host_cursor(void);

void
 hostx_use_host_cursor(void);

void
 hostx_use_fullscreen(void);

int
 hostx_want_fullscreen(void);

int
 hostx_want_preexisting_window(EphyrScreenInfo screen);

void
 hostx_use_preexisting_window(unsigned long win_id);

void
 hostx_use_resname(char *name, int fromcmd);

void
 hostx_set_title(char *name);

void
 hostx_handle_signal(int signum);

int
 hostx_init(void);

void
 hostx_add_screen(EphyrScreenInfo screen, unsigned long win_id, int screen_num);

void
 hostx_set_display_name(char *name);

void
 hostx_set_screen_number(EphyrScreenInfo screen, int number);

void
 hostx_set_win_title(EphyrScreenInfo screen, char *extra_text);

int
 hostx_get_depth(void);

int
 hostx_get_server_depth(EphyrScreenInfo screen);

void
 hostx_set_server_depth(EphyrScreenInfo screen, int depth);

int
 hostx_get_bpp(void *info);

void
 hostx_get_visual_masks(void *info, CARD32 *rmsk, CARD32 *gmsk, CARD32 *bmsk);
void

hostx_set_cmap_entry(unsigned char idx,
                     unsigned char r, unsigned char g, unsigned char b);

void *hostx_screen_init(EphyrScreenInfo screen,
                        int width, int height, int buffer_height);

void

hostx_paint_rect(EphyrScreenInfo screen,
                 int sx, int sy, int dx, int dy, int width, int height);

void
 hostx_load_keymap(void);

int
 hostx_get_event(EphyrHostXEvent * ev);

void *hostx_get_display(void);

int
 hostx_get_window(int a_screen_number);

int
 hostx_get_window_attributes(int a_window, EphyrHostWindowAttributes * a_attr);

int

hostx_get_extension_info(const char *a_ext_name,
                         int *a_major_opcode,
                         int *a_first_even, int *a_first_error);
int
 hostx_get_visuals_info(EphyrHostVisualInfo ** a_visuals, int *a_num_entries);

int hostx_create_window(int a_screen_number,
                        EphyrBox * a_geometry,
                        int a_visual_id, int *a_host_win /*out parameter */ );

int hostx_destroy_window(int a_win);

int hostx_set_window_geometry(int a_win, EphyrBox * a_geo);

int hostx_set_window_bounding_rectangles(int a_window,
                                         EphyrRect * a_rects, int a_num_rects);

int hostx_set_window_clipping_rectangles(int a_window,
                                         EphyrRect * a_rects, int a_num_rects);
int hostx_has_xshape(void);

#ifdef XF86DRI
int hostx_lookup_peer_window(void *a_local_window,
                             int *a_host_peer /*out parameter */ );
int

hostx_allocate_resource_id_peer(int a_local_resource_id,
                                int *a_remote_resource_id);
int
 hostx_get_resource_id_peer(int a_local_resource_id, int *a_remote_resource_id);
int hostx_has_dri(void);

int hostx_has_glx(void);
#endif                          /* XF86DRI */

#endif /*_XLIBS_STUFF_H_*/
