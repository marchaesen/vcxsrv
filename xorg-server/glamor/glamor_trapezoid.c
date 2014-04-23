/*
 * Copyright © 2009 Intel Corporation
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
 *    Junyan He <junyan.he@linux.intel.com>
 *
 */

/** @file glamor_trapezoid.c
 *
 * Trapezoid acceleration implementation
 */

#include "glamor_priv.h"

#ifdef RENDER
#include "mipict.h"
#include "fbpict.h"

static xFixed
_glamor_linefixedX(xLineFixed *l, xFixed y, Bool ceil)
{
    xFixed dx = l->p2.x - l->p1.x;
    xFixed_32_32 ex = (xFixed_32_32) (y - l->p1.y) * dx;
    xFixed dy = l->p2.y - l->p1.y;

    if (ceil)
        ex += (dy - 1);
    return l->p1.x + (xFixed) (ex / dy);
}

static xFixed
_glamor_linefixedY(xLineFixed *l, xFixed x, Bool ceil)
{
    xFixed dy = l->p2.y - l->p1.y;
    xFixed_32_32 ey = (xFixed_32_32) (x - l->p1.x) * dy;
    xFixed dx = l->p2.x - l->p1.x;

    if (ceil)
        ey += (dx - 1);
    return l->p1.y + (xFixed) (ey / dx);
}

#ifdef GLAMOR_TRAPEZOID_SHADER

#define GLAMOR_VERTEX_TOP_BOTTOM  (GLAMOR_VERTEX_SOURCE + 1)
#define GLAMOR_VERTEX_LEFT_PARAM  (GLAMOR_VERTEX_SOURCE + 2)
#define GLAMOR_VERTEX_RIGHT_PARAM (GLAMOR_VERTEX_SOURCE + 3)

#define DEBUG_CLIP_VTX 0

#define POINT_INSIDE_CLIP_RECT(point, rect)	\
    (point[0] >= IntToxFixed(rect->x1)		\
     && point[0] <= IntToxFixed(rect->x2) 	\
     && point[1] >= IntToxFixed(rect->y1)	\
     && point[1] <= IntToxFixed(rect->y2))

static xFixed
_glamor_lines_crossfixedY(xLineFixed *l, xLineFixed *r)
{
    xFixed dx1 = l->p2.x - l->p1.x;
    xFixed dx2 = r->p2.x - r->p1.x;
    xFixed dy1 = l->p2.y - l->p1.y;
    xFixed dy2 = r->p2.y - r->p1.y;
    xFixed_32_32 tmp = (xFixed_32_32) dy2 * dy1;
    xFixed_32_32 dividend1 = (tmp >> 32) * (l->p1.x - r->p1.x);
    xFixed_32_32 dividend2;
    xFixed_32_32 dividend3;
    xFixed_32_32 divisor;

    tmp = (xFixed_32_32) dx1 *dy2;

    dividend2 = (tmp >> 32) * l->p1.y;
    tmp = (xFixed_32_32) dy1 *dx2;

    dividend3 = (tmp >> 32) * r->p1.y;
    divisor = ((xFixed_32_32) dx1 * (xFixed_32_32) dy2
               - (xFixed_32_32) dy1 * (xFixed_32_32) dx2) >> 32;

    if (divisor)
        return (xFixed) ((dividend2 - dividend1 - dividend3) / divisor);

    return 0xFFFFFFFF;
}

static Bool
point_inside_trapezoid(int point[2], xTrapezoid *trap, xFixed cut_y)
{
    int ret = TRUE;
    int tmp;

    if (point[1] > trap->bottom) {
        ret = FALSE;
        if (DEBUG_CLIP_VTX) {
            ErrorF("Out of Trap bottom, point[1] = %d(0x%x)), "
                   "bottom = %d(0x%x)\n",
                   (unsigned int) xFixedToInt(point[1]), point[1],
                   (unsigned int) xFixedToInt(trap->bottom),
                   (unsigned int) trap->bottom);
        }

        return ret;
    }

    if (point[1] < trap->top) {
        ret = FALSE;
        if (DEBUG_CLIP_VTX) {
            ErrorF("Out of Trap top, point[1] = %d(0x%x)), "
                   "top = %d(0x%x)\n",
                   (unsigned int) xFixedToInt(point[1]), point[1],
                   (unsigned int) xFixedToInt(trap->top),
                   (unsigned int) trap->top);
        }

        return ret;
    }

    tmp = _glamor_linefixedX(&trap->left, point[1], FALSE);
    if (point[0] < tmp) {
        ret = FALSE;

        if (abs(cut_y - trap->top) < pixman_fixed_1_minus_e &&
            abs(point[1] - trap->top) < pixman_fixed_1_minus_e &&
            tmp - point[0] < pixman_fixed_1_minus_e) {
            ret = TRUE;
        }
        else if (abs(cut_y - trap->bottom) < pixman_fixed_1_minus_e &&
                 point[1] - trap->bottom < pixman_fixed_1_minus_e &&
                 tmp - point[0] < pixman_fixed_1_minus_e) {
            ret = TRUE;
        }

        if (DEBUG_CLIP_VTX && !ret) {
            ErrorF("Out of Trap left, point[0] = %d(0x%x)), "
                   "left = %d(0x%x)\n",
                   (unsigned int) xFixedToInt(point[0]), point[0],
                   (unsigned int) xFixedToInt(tmp), (unsigned int) tmp);
        }

        if (!ret)
            return ret;
    }

    tmp = _glamor_linefixedX(&trap->right, point[1], TRUE);
    if (point[0] > tmp) {
        ret = FALSE;

        if (abs(cut_y - trap->top) < pixman_fixed_1_minus_e &&
            abs(point[1] - trap->top) < pixman_fixed_1_minus_e &&
            point[0] - tmp < pixman_fixed_1_minus_e) {
            ret = TRUE;
        }
        else if (abs(cut_y - trap->bottom) < pixman_fixed_1_minus_e &&
                 abs(point[1] - trap->bottom) < pixman_fixed_1_minus_e &&
                 point[0] - tmp < pixman_fixed_1_minus_e) {
            ret = TRUE;
        }

        if (DEBUG_CLIP_VTX && !ret) {
            ErrorF("Out of Trap right, point[0] = %d(0x%x)), "
                   "right = %d(0x%x)\n",
                   (unsigned int) xFixedToInt(point[0]), point[0],
                   (unsigned int) xFixedToInt(tmp), (unsigned int) tmp);
        }

        if (!ret)
            return ret;
    }

    return ret;
}

static void
glamor_emit_composite_vert(ScreenPtr screen,
                           float *vb,
                           const float *src_coords,
                           const float *mask_coords,
                           const float *dst_coords, int i)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int j = 0;

    vb += i * glamor_priv->vb_stride / sizeof(float);

    vb[j++] = dst_coords[i * 2 + 0];
    vb[j++] = dst_coords[i * 2 + 1];
    if (glamor_priv->has_source_coords) {
        vb[j++] = src_coords[i * 2 + 0];
        vb[j++] = src_coords[i * 2 + 1];
    }
    if (glamor_priv->has_mask_coords) {
        vb[j++] = mask_coords[i * 2 + 0];
        vb[j++] = mask_coords[i * 2 + 1];
    }

    glamor_priv->render_nr_verts++;
}

static void
glamor_emit_composite_triangle(ScreenPtr screen,
                               float *vb,
                               const float *src_coords,
                               const float *mask_coords,
                               const float *dst_coords)
{
    glamor_emit_composite_vert(screen, vb,
                               src_coords, mask_coords, dst_coords, 0);
    glamor_emit_composite_vert(screen, vb,
                               src_coords, mask_coords, dst_coords, 1);
    glamor_emit_composite_vert(screen, vb,
                               src_coords, mask_coords, dst_coords, 2);
}

static void
glamor_flush_composite_triangles(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);
    glamor_put_vbo_space(screen);

    if (!glamor_priv->render_nr_verts)
        return;

    glDrawArrays(GL_TRIANGLES, 0, glamor_priv->render_nr_verts);
}

static Bool
_glamor_clip_trapezoid_vertex(xTrapezoid *trap, BoxPtr pbox,
                              int vertex[6], int *num)
{
    xFixed edge_cross_y = 0xFFFFFFFF;
    int tl[2];
    int bl[2];
    int tr[2];
    int br[2];
    int left_cut_top[2];
    int left_cut_left[2];
    int left_cut_right[2];
    int left_cut_bottom[2];
    int right_cut_top[2];
    int right_cut_left[2];
    int right_cut_right[2];
    int right_cut_bottom[2];
    int tmp[2];
    int tmp_vtx[20 * 2];
    float tmp_vtx_slope[20];
    BoxRec trap_bound;
    int i = 0;
    int vertex_num = 0;

    if (DEBUG_CLIP_VTX) {
        ErrorF
            ("The parameter of xTrapezoid is:\ntop: %d  0x%x\tbottom: %d  0x%x\n"
             "left:  p1 (%d   0x%x, %d   0x%x)\tp2 (%d   0x%x, %d   0x%x)\n"
             "right: p1 (%d   0x%x, %d   0x%x)\tp2 (%d   0x%x, %d   0x%x)\n",
             xFixedToInt(trap->top), (unsigned int) trap->top,
             xFixedToInt(trap->bottom), (unsigned int) trap->bottom,
             xFixedToInt(trap->left.p1.x), (unsigned int) trap->left.p1.x,
             xFixedToInt(trap->left.p1.y), (unsigned int) trap->left.p1.y,
             xFixedToInt(trap->left.p2.x), (unsigned int) trap->left.p2.x,
             xFixedToInt(trap->left.p2.y), (unsigned int) trap->left.p2.y,
             xFixedToInt(trap->right.p1.x), (unsigned int) trap->right.p1.x,
             xFixedToInt(trap->right.p1.y), (unsigned int) trap->right.p1.y,
             xFixedToInt(trap->right.p2.x), (unsigned int) trap->right.p2.x,
             xFixedToInt(trap->right.p2.y), (unsigned int) trap->right.p2.y);
    }

    miTrapezoidBounds(1, trap, &trap_bound);
    if (DEBUG_CLIP_VTX)
        ErrorF("The bounds for this traps is: bounds.x1 = %d, bounds.x2 = %d, "
               "bounds.y1 = %d, bounds.y2 = %d\n", trap_bound.x1, trap_bound.x2,
               trap_bound.y1, trap_bound.y2);

    if (trap_bound.x1 > pbox->x2 || trap_bound.x2 < pbox->x1)
        return FALSE;
    if (trap_bound.y1 > pbox->y2 || trap_bound.y2 < pbox->y1)
        return FALSE;

#define IS_TRAP_EDGE_VERTICAL(edge)		\
	(edge->p1.x == edge->p2.x)

#define CACULATE_CUT_VERTEX(vtx, cal_x, ceil, vh_edge, edge)		\
	do {								\
	    if(cal_x) {							\
		vtx[1] = (vh_edge);					\
		vtx[0] = (_glamor_linefixedX(				\
			      edge, vh_edge, ceil));			\
		if(DEBUG_CLIP_VTX)					\
		    ErrorF("The intersection point of line y=%d and "	\
			   "line of p1:(%d,%d) -- p2 (%d,%d) "		\
			   "is (%d, %d)\n",				\
			   xFixedToInt(vh_edge),			\
			   xFixedToInt(edge->p1.x),			\
			   xFixedToInt(edge->p1.y),			\
			   xFixedToInt(edge->p2.x),			\
			   xFixedToInt(edge->p2.y),			\
			   xFixedToInt(vtx[0]),				\
			   xFixedToInt(vtx[1]));			\
	    } else {							\
		vtx[0] = (vh_edge);					\
		vtx[1] = (_glamor_linefixedY(				\
			      edge, vh_edge, ceil));			\
		if(DEBUG_CLIP_VTX)					\
		    ErrorF("The intersection point of line x=%d and "	\
			   "line of p1:(%d,%d) -- p2 (%d,%d) "		\
			   "is (%d, %d)\n",				\
			   xFixedToInt(vh_edge),			\
			   xFixedToInt(edge->p1.x),			\
			   xFixedToInt(edge->p1.y),			\
			   xFixedToInt(edge->p2.x),			\
			   xFixedToInt(edge->p2.y),			\
			   xFixedToInt(vtx[0]),				\
			   xFixedToInt(vtx[1]));			\
	    }								\
	} while(0)

#define ADD_VERTEX_IF_INSIDE(vtx)				\
	if(POINT_INSIDE_CLIP_RECT(vtx, pbox)			\
	   && point_inside_trapezoid(vtx, trap, edge_cross_y)){	\
	    tmp_vtx[vertex_num] = xFixedToInt(vtx[0]);		\
	    tmp_vtx[vertex_num + 1] = xFixedToInt(vtx[1]);	\
	    vertex_num += 2;					\
	    if(DEBUG_CLIP_VTX)					\
		ErrorF("@ Point: (%d, %d) is inside "		\
		       "the Rect and Trapezoid\n",		\
		       xFixedToInt(vtx[0]),			\
		       xFixedToInt(vtx[1]));			\
	} else if(DEBUG_CLIP_VTX){				\
	    ErrorF("X Point: (%d, %d) is outside "		\
		   "the Rect and Trapezoid\t",			\
		   xFixedToInt(vtx[0]),				\
		   xFixedToInt(vtx[1]));			\
	    if(POINT_INSIDE_CLIP_RECT(vtx, pbox))		\
		ErrorF("The Point is outside "			\
		       "the Trapezoid\n");			\
	    else						\
		ErrorF("The Point is outside "			\
		       "the Rect\n");				\
	}

    /*Trap's right edge cut right edge. */
    if ((!IS_TRAP_EDGE_VERTICAL((&trap->left))) ||
        (!IS_TRAP_EDGE_VERTICAL((&trap->right)))) {
        edge_cross_y = _glamor_lines_crossfixedY((&trap->left), (&trap->right));
        if (DEBUG_CLIP_VTX) {
            ErrorF("Trap's left edge cut right edge at %d(0x%x), "
                   "trap_top = %x, trap_bottom = %x\n",
                   xFixedToInt(edge_cross_y), edge_cross_y,
                   (unsigned int) trap->top, (unsigned int) trap->bottom);
        }
    }

    /*Trap's TopLeft, BottomLeft, TopRight and BottomRight. */
    CACULATE_CUT_VERTEX(tl, 1, FALSE, trap->top, (&trap->left));
    CACULATE_CUT_VERTEX(bl, 1, FALSE, trap->bottom, (&trap->left));
    CACULATE_CUT_VERTEX(tr, 1, TRUE, trap->top, (&trap->right));
    CACULATE_CUT_VERTEX(br, 1, TRUE, trap->bottom, (&trap->right));

    if (DEBUG_CLIP_VTX)
        ErrorF("Trap's TopLeft, BottomLeft, TopRight and BottomRight\n");
    if (DEBUG_CLIP_VTX)
        ErrorF("Caculate the vertex of trapezoid:\n"
               "      (%3d, %3d)-------------------------(%3d, %3d)\n"
               "              /                           \\       \n"
               "             /                             \\      \n"
               "            /                               \\     \n"
               "  (%3d, %3d)---------------------------------(%3d, %3d)\n"
               "Clip with rect:\n"
               "  (%3d, %3d)------------------------(%3d, %3d)    \n"
               "           |                        |             \n"
               "           |                        |             \n"
               "           |                        |             \n"
               "  (%3d, %3d)------------------------(%3d, %3d)    \n",
               xFixedToInt(tl[0]), xFixedToInt(tl[1]), xFixedToInt(tr[0]),
               xFixedToInt(tr[1]), xFixedToInt(bl[0]), xFixedToInt(bl[1]),
               xFixedToInt(br[0]), xFixedToInt(br[1]),
               pbox->x1, pbox->y1, pbox->x2, pbox->y1, pbox->x1, pbox->y2,
               pbox->x2, pbox->y2);

    ADD_VERTEX_IF_INSIDE(tl);
    ADD_VERTEX_IF_INSIDE(bl);
    ADD_VERTEX_IF_INSIDE(tr);
    ADD_VERTEX_IF_INSIDE(br);

    /*Trap's left edge cut Rect. */
    if (DEBUG_CLIP_VTX)
        ErrorF("Trap's left edge cut Rect\n");
    CACULATE_CUT_VERTEX(left_cut_top, 1, FALSE, IntToxFixed(pbox->y1),
                        (&trap->left));
    ADD_VERTEX_IF_INSIDE(left_cut_top);
    if (!IS_TRAP_EDGE_VERTICAL((&trap->left))) {
        CACULATE_CUT_VERTEX(left_cut_left, 0, FALSE, IntToxFixed(pbox->x1),
                            (&trap->left));
        ADD_VERTEX_IF_INSIDE(left_cut_left);
    }
    CACULATE_CUT_VERTEX(left_cut_bottom, 1, FALSE, IntToxFixed(pbox->y2),
                        (&trap->left));
    ADD_VERTEX_IF_INSIDE(left_cut_bottom);
    if (!IS_TRAP_EDGE_VERTICAL((&trap->left))) {
        CACULATE_CUT_VERTEX(left_cut_right, 0, FALSE, IntToxFixed(pbox->x2),
                            (&trap->left));
        ADD_VERTEX_IF_INSIDE(left_cut_right);
    }

    /*Trap's right edge cut Rect. */
    if (DEBUG_CLIP_VTX)
        ErrorF("Trap's right edge cut Rect\n");
    CACULATE_CUT_VERTEX(right_cut_top, 1, TRUE, IntToxFixed(pbox->y1),
                        (&trap->right));
    ADD_VERTEX_IF_INSIDE(right_cut_top);
    if (!IS_TRAP_EDGE_VERTICAL((&trap->right))) {
        CACULATE_CUT_VERTEX(right_cut_left, 0, TRUE, IntToxFixed(pbox->x1),
                            (&trap->right));
        ADD_VERTEX_IF_INSIDE(right_cut_left);
    }
    CACULATE_CUT_VERTEX(right_cut_bottom, 1, TRUE, IntToxFixed(pbox->y2),
                        (&trap->right));
    ADD_VERTEX_IF_INSIDE(right_cut_bottom);
    if (!IS_TRAP_EDGE_VERTICAL((&trap->right))) {
        CACULATE_CUT_VERTEX(right_cut_right, 0, TRUE, IntToxFixed(pbox->x2),
                            (&trap->right));
        ADD_VERTEX_IF_INSIDE(right_cut_right);
    }

    /* Trap's top cut Left and Right of rect. */
    if (DEBUG_CLIP_VTX)
        ErrorF("Trap's top cut Left and Right of rect\n");
    tmp[0] = IntToxFixed(pbox->x1);
    tmp[1] = trap->top;
    ADD_VERTEX_IF_INSIDE(tmp);
    tmp[0] = IntToxFixed(pbox->x2);
    tmp[1] = trap->top;
    ADD_VERTEX_IF_INSIDE(tmp);

    /* Trap's bottom cut Left and Right of rect. */
    if (DEBUG_CLIP_VTX)
        ErrorF("Trap's bottom cut Left and Right of rect\n");
    tmp[0] = IntToxFixed(pbox->x1);
    tmp[1] = trap->bottom;
    ADD_VERTEX_IF_INSIDE(tmp);
    tmp[0] = IntToxFixed(pbox->x2);
    tmp[1] = trap->bottom;
    ADD_VERTEX_IF_INSIDE(tmp);

    /* The orginal 4 vertex of rect. */
    if (DEBUG_CLIP_VTX)
        ErrorF("The orginal 4 vertex of rect\n");
    tmp[0] = IntToxFixed(pbox->x1);
    tmp[1] = IntToxFixed(pbox->y1);
    ADD_VERTEX_IF_INSIDE(tmp);
    tmp[0] = IntToxFixed(pbox->x1);
    tmp[1] = IntToxFixed(pbox->y2);
    ADD_VERTEX_IF_INSIDE(tmp);
    tmp[0] = IntToxFixed(pbox->x2);
    tmp[1] = IntToxFixed(pbox->y2);
    ADD_VERTEX_IF_INSIDE(tmp);
    tmp[0] = IntToxFixed(pbox->x2);
    tmp[1] = IntToxFixed(pbox->y1);
    ADD_VERTEX_IF_INSIDE(tmp);

    if (DEBUG_CLIP_VTX) {
        ErrorF("\nThe candidate vertex number is %d\n", vertex_num / 2);
        for (i = 0; i < vertex_num / 2; i++) {
            ErrorF("(%d, %d) ", tmp_vtx[2 * i], tmp_vtx[2 * i + 1]);
        }
        ErrorF("\n");
    }

    /* Sort the vertex by X and then Y. */
    for (i = 0; i < vertex_num / 2; i++) {
        int j;

        for (j = 0; j < vertex_num / 2 - i - 1; j++) {
            if (tmp_vtx[2 * j] > tmp_vtx[2 * (j + 1)]
                || (tmp_vtx[2 * j] == tmp_vtx[2 * (j + 1)]
                    && tmp_vtx[2 * j + 1] > tmp_vtx[2 * (j + 1) + 1])) {
                tmp[0] = tmp_vtx[2 * j];
                tmp[1] = tmp_vtx[2 * j + 1];
                tmp_vtx[2 * j] = tmp_vtx[2 * (j + 1)];
                tmp_vtx[2 * j + 1] = tmp_vtx[2 * (j + 1) + 1];
                tmp_vtx[2 * (j + 1)] = tmp[0];
                tmp_vtx[2 * (j + 1) + 1] = tmp[1];
            }
        }

    }

    if (DEBUG_CLIP_VTX) {
        ErrorF("\nAfter sort vertex number is:\n");
        for (i = 0; i < vertex_num / 2; i++) {
            ErrorF("(%d, %d) ", tmp_vtx[2 * i], tmp_vtx[2 * i + 1]);
        }
        ErrorF("\n");
    }

    memset(vertex, -1, 2 * 6);
    *num = 0;

    for (i = 0; i < vertex_num / 2; i++) {
        if (*num > 0 && vertex[2 * (*num - 1)] == tmp_vtx[2 * i]
            && vertex[2 * (*num - 1) + 1] == tmp_vtx[2 * i + 1]) {
            /*same vertex. */
            if (DEBUG_CLIP_VTX)
                ErrorF("X Point:(%d, %d) discard\n",
                       tmp_vtx[2 * i], tmp_vtx[2 * i + 1]);
            continue;
        }

        (*num)++;
        if (*num > 6) {
            if (DEBUG_CLIP_VTX)
                FatalError("Trapezoid clip with Rect can never have vtx"
                           "number bigger than 6\n");
            else {
                ErrorF("Trapezoid clip with Rect can never have vtx"
                       "number bigger than 6\n");
                *num = 6;
                break;
            }
        }

        vertex[2 * (*num - 1)] = tmp_vtx[2 * i];
        vertex[2 * (*num - 1) + 1] = tmp_vtx[2 * i + 1];
        if (DEBUG_CLIP_VTX)
            ErrorF("@ Point:(%d, %d) select, num now is %d\n",
                   tmp_vtx[2 * i], tmp_vtx[2 * i + 1], *num);
    }

    /* Now we need to arrange the vtx in the polygon's counter-clockwise
       order. We first select the left and top point as the start point and
       sort every vtx by the slope from vtx to the start vtx. */
    for (i = 1; i < *num; i++) {
        tmp_vtx_slope[i] = (vertex[2 * i] != vertex[0] ?
                            (float) (vertex[2 * i + 1] -
                                     vertex[1]) / (float) (vertex[2 * i] -
                                                           vertex[0])
                            : (float) INT_MAX);
    }

    if (DEBUG_CLIP_VTX) {
        ErrorF("\nvtx number: %d, VTX and slope:\n", *num);
        for (i = 0; i < *num; i++) {
            ErrorF("(%d, %d):%f ",
                   vertex[2 * i], vertex[2 * i + 1], tmp_vtx_slope[i]);
        }
        ErrorF("\n");
    }

    /* Sort the vertex by slope. */
    for (i = 0; i < *num - 1; i++) {
        int j;
        float tmp_slope;

        for (j = 1; j < *num - i - 1; j++) {
            if (tmp_vtx_slope[j] < tmp_vtx_slope[j + 1]) {
                tmp_slope = tmp_vtx_slope[j];
                tmp_vtx_slope[j] = tmp_vtx_slope[j + 1];
                tmp_vtx_slope[j + 1] = tmp_slope;
                tmp[0] = vertex[2 * j];
                tmp[1] = vertex[2 * j + 1];
                vertex[2 * j] = vertex[2 * (j + 1)];
                vertex[2 * j + 1] = vertex[2 * (j + 1) + 1];
                vertex[2 * (j + 1)] = tmp[0];
                vertex[2 * (j + 1) + 1] = tmp[1];
            }
        }
    }

    if (DEBUG_CLIP_VTX) {
        ErrorF("\nBefore return, vtx number: %d, VTX and slope:\n", *num);
        for (i = 0; i < *num; i++) {
            ErrorF("(%d, %d):%f ",
                   vertex[2 * i], vertex[2 * i + 1], tmp_vtx_slope[i]);
        }
        ErrorF("\n");
    }

    return TRUE;
}

static void *
glamor_setup_composite_vbo_for_trapezoid(ScreenPtr screen, int n_verts)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int stride;
    int vert_size;
    char *vbo_offset;
    void *vb;

    glamor_priv->render_nr_verts = 0;

    /* For GLAMOR_VERTEX_POS */
    glamor_priv->vb_stride = 2 * sizeof(float);

    /* For GLAMOR_GLAMOR_VERTEX_SOURCE */
    glamor_priv->vb_stride += 2 * sizeof(float);

    /* For GLAMOR_VERTEX_TOP_BOTTOM */
    glamor_priv->vb_stride += 2 * sizeof(float);

    /* For GLAMOR_VERTEX_LEFT_PARAM */
    glamor_priv->vb_stride += 4 * sizeof(float);

    /* For GLAMOR_VERTEX_RIGHT_PARAM */
    glamor_priv->vb_stride += 4 * sizeof(float);

    vert_size = n_verts * glamor_priv->vb_stride;

    glamor_make_current(glamor_priv);

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_TOP_BOTTOM);
    glDisableVertexAttribArray(GLAMOR_VERTEX_LEFT_PARAM);
    glDisableVertexAttribArray(GLAMOR_VERTEX_RIGHT_PARAM);

    vb = glamor_get_vbo_space(screen, vert_size, &vbo_offset);

    /* Set the vertex pointer. */
    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                          GL_FALSE, glamor_priv->vb_stride,
                          vbo_offset);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
    stride = 2;

    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT,
                          GL_FALSE, glamor_priv->vb_stride,
                          vbo_offset + stride * sizeof(float));
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    stride += 2;

    glVertexAttribPointer(GLAMOR_VERTEX_TOP_BOTTOM, 2, GL_FLOAT,
                          GL_FALSE, glamor_priv->vb_stride,
                          vbo_offset + stride * sizeof(float));
    glEnableVertexAttribArray(GLAMOR_VERTEX_TOP_BOTTOM);
    stride += 2;

    glVertexAttribPointer(GLAMOR_VERTEX_LEFT_PARAM, 4, GL_FLOAT,
                          GL_FALSE, glamor_priv->vb_stride,
                          vbo_offset + stride * sizeof(float));
    glEnableVertexAttribArray(GLAMOR_VERTEX_LEFT_PARAM);
    stride += 4;

    glVertexAttribPointer(GLAMOR_VERTEX_RIGHT_PARAM, 4, GL_FLOAT,
                          GL_FALSE, glamor_priv->vb_stride,
                          vbo_offset + stride * sizeof(float));
    glEnableVertexAttribArray(GLAMOR_VERTEX_RIGHT_PARAM);

    return vb;
}

static Bool
_glamor_trapezoids_with_shader(CARD8 op,
                               PicturePtr src, PicturePtr dst,
                               PictFormatPtr mask_format, INT16 x_src,
                               INT16 y_src, int ntrap, xTrapezoid * traps)
{
    ScreenPtr screen = dst->pDrawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    struct shader_key key;
    glamor_composite_shader *shader = NULL;
    struct blendinfo op_info;
    PictFormatShort saved_source_format = 0;
    PixmapPtr source_pixmap = NULL;
    PixmapPtr dest_pixmap = NULL;
    glamor_pixmap_private *source_pixmap_priv = NULL;
    glamor_pixmap_private *dest_pixmap_priv = NULL;
    glamor_pixmap_private *temp_src_priv = NULL;
    int x_temp_src, y_temp_src;
    int src_width, src_height;
    int source_x_off, source_y_off;
    GLfloat src_xscale = 1, src_yscale = 1;
    int x_dst, y_dst;
    int dest_x_off, dest_y_off;
    GLfloat dst_xscale, dst_yscale;
    BoxRec bounds;
    PicturePtr temp_src = src;
    int vert_stride = 3;
    int ntriangle_per_loop;
    int nclip_rect;
    int mclip_rect;
    int clip_processed;
    int clipped_vtx[6 * 2];
    RegionRec region;
    BoxPtr box = NULL;
    BoxPtr pbox = NULL;
    int traps_count = 0;
    int traps_not_completed = 0;
    xTrapezoid *ptrap = NULL;
    int nbox;
    float src_matrix[9];
    Bool ret = FALSE;

    /* If a mask format wasn't provided, we get to choose, but behavior should
     * be as if there was no temporary mask the traps were accumulated into.
     */
    if (!mask_format) {
        if (dst->polyEdge == PolyEdgeSharp)
            mask_format = PictureMatchFormat(screen, 1, PICT_a1);
        else
            mask_format = PictureMatchFormat(screen, 8, PICT_a8);
        for (; ntrap; ntrap--, traps++)
            glamor_trapezoids(op, src, dst, mask_format, x_src,
                              y_src, 1, traps);
        return TRUE;
    }

    miTrapezoidBounds(ntrap, traps, &bounds);
    DEBUGF("The bounds for all traps is: bounds.x1 = %d, bounds.x2 = %d, "
           "bounds.y1 = %d, bounds.y2 = %d\n", bounds.x1, bounds.x2,
           bounds.y1, bounds.y2);

    /* No area need to render. */
    if (bounds.y1 >= bounds.y2 || bounds.x1 >= bounds.x2)
        return TRUE;

    dest_pixmap = glamor_get_drawable_pixmap(dst->pDrawable);
    dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dest_pixmap_priv)
        || dest_pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        /* Currently. Always fallback to cpu if destination is in CPU memory. */
        ret = FALSE;
        DEBUGF("dst pixmap has no FBO.\n");
        goto TRAPEZOID_OUT;
    }

    if (src->pDrawable) {
        source_pixmap = glamor_get_drawable_pixmap(src->pDrawable);
        source_pixmap_priv = glamor_get_pixmap_private(source_pixmap);
        temp_src_priv = source_pixmap_priv;
        if (source_pixmap_priv
            && (source_pixmap_priv->type == GLAMOR_DRM_ONLY
                || source_pixmap_priv->type == GLAMOR_TEXTURE_LARGE)) {
            ret = FALSE;
            goto TRAPEZOID_OUT;
        }
    }

    x_dst = bounds.x1;
    y_dst = bounds.y1;

    src_width = bounds.x2 - bounds.x1;
    src_height = bounds.y2 - bounds.y1;

    x_temp_src = x_src + bounds.x1 - (traps[0].left.p1.x >> 16);
    y_temp_src = y_src + bounds.y1 - (traps[0].left.p1.y >> 16);

    if ((!src->pDrawable && (src->pSourcePict->type != SourcePictTypeSolidFill))        //1. The Gradient case.
        /* 2. Has no fbo but can upload. */
        || (src->pDrawable && !GLAMOR_PIXMAP_PRIV_HAS_FBO(source_pixmap_priv)
            && ((src_width * src_height * 4 <
                 source_pixmap->drawable.width * source_pixmap->drawable.height)
                || !glamor_check_fbo_size(glamor_priv,
                                          source_pixmap->drawable.width,
                                          source_pixmap->drawable.height)))) {

        if (!glamor_check_fbo_size(glamor_priv, src_width, src_height)) {
            ret = FALSE;
            goto TRAPEZOID_OUT;
        }
        temp_src = glamor_convert_gradient_picture(screen, src,
                                                   x_src, y_src,
                                                   src_width, src_height);
        if (!temp_src) {
            temp_src = src;
            ret = FALSE;
            DEBUGF("Convert gradient picture failed\n");
            goto TRAPEZOID_OUT;
        }
        temp_src_priv =
            glamor_get_pixmap_private((PixmapPtr) temp_src->pDrawable);
        x_temp_src = y_temp_src = 0;
    }

    x_dst += dst->pDrawable->x;
    y_dst += dst->pDrawable->y;
    if (temp_src->pDrawable) {
        x_temp_src += temp_src->pDrawable->x;
        y_temp_src += temp_src->pDrawable->y;
    }

    if (!miComputeCompositeRegion(&region,
                                  temp_src, NULL, dst,
                                  x_temp_src, y_temp_src,
                                  0, 0, x_dst, y_dst, src_width, src_height)) {
        DEBUGF("All the regions are clipped out, do nothing\n");
        goto TRAPEZOID_OUT;
    }

    glamor_make_current(glamor_priv);

    box = REGION_RECTS(&region);
    nbox = REGION_NUM_RECTS(&region);
    pbox = box;

    ret = glamor_composite_choose_shader(op, temp_src, NULL, dst,
                                         temp_src_priv, NULL, dest_pixmap_priv,
                                         &key, &shader, &op_info,
                                         &saved_source_format);
    if (ret == FALSE) {
        DEBUGF("can not set the shader program for composite\n");
        goto TRAPEZOID_RESET_GL;
    }
    glamor_set_destination_pixmap_priv_nc(dest_pixmap_priv);
    glamor_composite_set_shader_blend(dest_pixmap_priv, &key, shader, &op_info);
    glamor_priv->has_source_coords = key.source != SHADER_SOURCE_SOLID;
    glamor_priv->has_mask_coords = (key.mask != SHADER_MASK_NONE &&
                                    key.mask != SHADER_MASK_SOLID);

    glamor_get_drawable_deltas(dst->pDrawable, dest_pixmap,
                               &dest_x_off, &dest_y_off);

    pixmap_priv_get_dest_scale(dest_pixmap_priv, &dst_xscale, &dst_yscale);

    if (glamor_priv->has_source_coords) {
        source_pixmap = glamor_get_drawable_pixmap(temp_src->pDrawable);
        source_pixmap_priv = glamor_get_pixmap_private(source_pixmap);
        glamor_get_drawable_deltas(temp_src->pDrawable,
                                   source_pixmap, &source_x_off, &source_y_off);
        pixmap_priv_get_scale(source_pixmap_priv, &src_xscale, &src_yscale);
        glamor_picture_get_matrixf(temp_src, src_matrix);
        vert_stride += 3;
    }

    if (glamor_priv->has_mask_coords) {
        DEBUGF("Should never have mask coords here!\n");
        ret = FALSE;
        goto TRAPEZOID_RESET_GL;
    }

    /* A trapezoid clip with a rectangle will at most generate a hexagon,
       which can be devided into 4 triangles to render. */
    ntriangle_per_loop =
        (vert_stride * nbox * ntrap * 4) >
        GLAMOR_COMPOSITE_VBO_VERT_CNT ? (GLAMOR_COMPOSITE_VBO_VERT_CNT /
                                         vert_stride) : nbox * ntrap * 4;
    ntriangle_per_loop = (ntriangle_per_loop / 4) * 4;

    nclip_rect = nbox;
    while (nclip_rect) {
        float *vb;

        mclip_rect = (nclip_rect * ntrap * 4) > ntriangle_per_loop ?
            (ntriangle_per_loop / (4 * ntrap)) : nclip_rect;

        if (!mclip_rect) {      /* Maybe too many traps. */
            mclip_rect = 1;
            ptrap = traps;
            traps_count = ntriangle_per_loop / 4;
            traps_not_completed = ntrap - traps_count;
        }
        else {
            traps_count = ntrap;
            ptrap = traps;
            traps_not_completed = 0;
        }

 NTRAPS_LOOP_AGAIN:

        vb = glamor_setup_composite_vbo(screen,
                                        (mclip_rect * traps_count *
                                         4 * vert_stride));
        clip_processed = mclip_rect;

        while (mclip_rect--) {
            while (traps_count--) {
                int vtx_num;
                int i;
                float vertices[3 * 2], source_texcoords[3 * 2];

                DEBUGF
                    ("In loop of render trapezoid, nclip_rect = %d, mclip_rect = %d, "
                     "clip_processed = %d, traps_count = %d, traps_not_completed = %d\n",
                     nclip_rect, mclip_rect, clip_processed, traps_count,
                     traps_not_completed);

                if (_glamor_clip_trapezoid_vertex
                    (ptrap, pbox, clipped_vtx, &vtx_num)) {
                    for (i = 0; i < vtx_num - 2; i++) {
                        int clipped_vtx_tmp[3 * 2];

                        clipped_vtx_tmp[0] = clipped_vtx[0];
                        clipped_vtx_tmp[1] = clipped_vtx[1];
                        clipped_vtx_tmp[2] = clipped_vtx[(i + 1) * 2];
                        clipped_vtx_tmp[3] = clipped_vtx[(i + 1) * 2 + 1];
                        clipped_vtx_tmp[4] = clipped_vtx[(i + 2) * 2];
                        clipped_vtx_tmp[5] = clipped_vtx[(i + 2) * 2 + 1];
                        glamor_set_normalize_tri_vcoords(dst_xscale, dst_yscale,
                                                         clipped_vtx_tmp,
                                                         glamor_priv->yInverted,
                                                         vertices);
                        DEBUGF("vertices of triangle: (%f X %f), (%f X %f), "
                               "(%f X %f)\n", vertices[0], vertices[1],
                               vertices[2], vertices[3], vertices[4],
                               vertices[5]);

                        if (key.source != SHADER_SOURCE_SOLID) {
                            if (src->transform) {
                                glamor_set_transformed_normalize_tri_tcoords
                                    (source_pixmap_priv, src_matrix, src_xscale,
                                     src_yscale, clipped_vtx_tmp,
                                     glamor_priv->yInverted, source_texcoords);
                            }
                            else {
                                glamor_set_normalize_tri_tcoords(src_xscale,
                                                                 src_yscale,
                                                                 clipped_vtx_tmp,
                                                                 glamor_priv->
                                                                 yInverted,
                                                                 source_texcoords);
                            }

                            DEBUGF("source_texcoords of triangle: (%f X %f), "
                                   "(%f X %f), (%f X %f)\n",
                                   source_texcoords[0], source_texcoords[1],
                                   source_texcoords[2], source_texcoords[3],
                                   source_texcoords[4], source_texcoords[5]);
                        }

                        glamor_emit_composite_triangle(screen, vb,
                                                       source_texcoords,
                                                       NULL, vertices);
                        vb += 3 * glamor_priv->vb_stride / sizeof(float);
                    }
                }

                ptrap++;
            }

            if (traps_not_completed) {  /* one loop of ntraps not completed */
                mclip_rect = 1;
                traps_count = traps_not_completed > (ntriangle_per_loop / 4) ?
                    (ntriangle_per_loop / 4) : traps_not_completed;
                traps_not_completed -= traps_count;
                glamor_flush_composite_triangles(screen);
                goto NTRAPS_LOOP_AGAIN;
            }
            else {
                ptrap = traps;
                traps_count = ntrap;
            }

            pbox++;
        }

        glamor_flush_composite_triangles(screen);

        nclip_rect -= clip_processed;
    }

    ret = TRUE;

 TRAPEZOID_RESET_GL:
    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_MASK);
    glDisable(GL_BLEND);

 TRAPEZOID_OUT:
    if (box) {
        REGION_UNINIT(dst->pDrawable->pScreen, &region);
    }

    if (temp_src != src) {
        FreePicture(temp_src, 0);
    }
    else {
        if (saved_source_format) {
            src->format = saved_source_format;
        }
    }

    return ret;
}

void
glamor_init_trapezoid_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    GLint fs_prog, vs_prog;

    const char *trapezoid_vs =
        GLAMOR_DEFAULT_PRECISION
        "attribute vec4 v_position;\n"
        "attribute vec2 v_texcoord;\n"
        /* v_top_bottom, v_left_param and v_right_param contain the
           constant value for all the vertex of one rect. Using uniform
           is more suitable but we need to reset the uniform variables
           for every rect rendering and can not use the vbo, which causes
           performance loss. So we set these attributes to same value
           for every vertex of one rect and so it is also a constant in FS */
        "attribute vec2 v_top_bottom;\n"
        "attribute vec4 v_left_param;\n"
        "attribute vec4 v_right_param;\n"
        "\n"
        "varying vec2 source_texture;\n"
        "varying float trap_top;\n"
        "varying float trap_bottom;\n"
        "varying float trap_left_x;\n"
        "varying float trap_left_y;\n"
        "varying float trap_left_slope;\n"
        "varying float trap_left_vertical_f;\n"
        "varying float trap_right_x;\n"
        "varying float trap_right_y;\n"
        "varying float trap_right_slope;\n"
        "varying float trap_right_vertical_f;\n"
        "\n"
        "void main()\n"
        "{\n"
        "    gl_Position = v_position;\n"
        "    source_texture = v_texcoord.xy;\n"
        "    trap_top = v_top_bottom.x;\n"
        "    trap_bottom = v_top_bottom.y;\n"
        "    \n"
        "    trap_left_x = v_left_param.x;\n"
        "    trap_left_y = v_left_param.y;\n"
        "    trap_left_slope = v_left_param.z;\n"
        "    trap_left_vertical_f = v_left_param.w;\n"
        "    \n"
        "    trap_right_x = v_right_param.x;\n"
        "    trap_right_y = v_right_param.y;\n"
        "    trap_right_slope = v_right_param.z;\n"
        "    trap_right_vertical_f = v_right_param.w;\n"
        "}\n";

    /*
     * Because some GL fill function do not support the MultSample
     * anti-alias, we need to do the MSAA here. This manner like
     * pixman, will caculate the value of area in trapezoid dividing
     * the totol area for each pixel, as follow:
     |
     ----+------------------------------------------------------>
     |
     |              -------------
     |             /             \
     |            /               \
     |           /                 \
     |          /              +----------------+
     |         /               |.....\          |
     |        /                |......\         |
     |       /                 |.......\        |
     |      /                  |........\       |
     |     /-------------------+---------\      |
     |                         |                |
     |                         |                |
     |                         +----------------+
     |
     \|/

     */
    const char *trapezoid_fs =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 source_texture;  \n"
        "varying float trap_top;  \n"
        "varying float trap_bottom;  \n"
        "varying float trap_left_x;  \n"
        "varying float trap_left_y;  \n"
        "varying float trap_left_slope;  \n"
        "varying float trap_left_vertical_f;  \n"
        "varying float trap_right_x;  \n"
        "varying float trap_right_y;  \n"
        "varying float trap_right_slope;  \n"
        "varying float trap_right_vertical_f;  \n"
        "float x_per_pix = 1.0;"
        "float y_per_pix = 1.0;"
        "\n"
        "float get_alpha_val() \n"
        "{  \n"
        "    float x_up_cut_left;  \n"
        "    float x_bottom_cut_left;  \n"
        "    float x_up_cut_right;  \n"
        "    float x_bottom_cut_right;  \n"
        "    bool trap_left_vertical;\n"
        "    bool trap_right_vertical;\n"
        "	 if (abs(trap_left_vertical_f - 1.0) <= 0.0001)\n"
        "		trap_left_vertical = true;\n"
        "	 else\n"
        "		trap_left_vertical = false;\n"
        "	 if (abs(trap_right_vertical_f - 1.0) <= 0.0001)\n"
        "		trap_right_vertical = true;\n"
        "	 else\n"
        "		trap_right_vertical = false;\n"
        "    \n"
        "    if(trap_left_vertical == true) {  \n"
        "        x_up_cut_left = trap_left_x;  \n"
        "        x_bottom_cut_left = trap_left_x;  \n"
        "    } else {  \n"
        "        x_up_cut_left = trap_left_x  \n"
        "            + (source_texture.y - y_per_pix/2.0 - trap_left_y)  \n"
        "              / trap_left_slope;  \n"
        "        x_bottom_cut_left = trap_left_x  \n"
        "            + (source_texture.y + y_per_pix/2.0 - trap_left_y)  \n"
        "              / trap_left_slope;  \n"
        "    }  \n"
        "    \n"
        "    if(trap_right_vertical == true) {  \n"
        "        x_up_cut_right = trap_right_x;  \n"
        "        x_bottom_cut_right = trap_right_x;  \n"
        "    } else {  \n"
        "        x_up_cut_right = trap_right_x  \n"
        "            + (source_texture.y - y_per_pix/2.0 - trap_right_y)  \n"
        "              / trap_right_slope;  \n"
        "        x_bottom_cut_right = trap_right_x  \n"
        "            + (source_texture.y + y_per_pix/2.0 - trap_right_y)  \n"
        "              / trap_right_slope;  \n"
        "    }  \n"
        "    \n"
        "    if((x_up_cut_left <= source_texture.x - x_per_pix/2.0) &&  \n"
        "          (x_bottom_cut_left <= source_texture.x - x_per_pix/2.0) &&  \n"
        "          (x_up_cut_right >= source_texture.x + x_per_pix/2.0) &&  \n"
        "          (x_bottom_cut_right >= source_texture.x + x_per_pix/2.0) &&  \n"
        "          (trap_top <= source_texture.y - y_per_pix/2.0) &&  \n"
        "          (trap_bottom >= source_texture.y + y_per_pix/2.0)) {  \n"
        //       The complete inside case.
        "        return 1.0;  \n"
        "    } else if((trap_top > source_texture.y + y_per_pix/2.0) ||  \n"
        "                (trap_bottom < source_texture.y - y_per_pix/2.0)) {  \n"
        //       The complete outside. Above the top or Below the bottom.
        "        return 0.0;  \n"
        "    } else {  \n"
        "        if((x_up_cut_right < source_texture.x - x_per_pix/2.0 &&  \n"
        "                   x_bottom_cut_right < source_texture.x - x_per_pix/2.0)  \n"
        "            || (x_up_cut_left > source_texture.x + x_per_pix/2.0  &&  \n"
        "                   x_bottom_cut_left > source_texture.x + x_per_pix/2.0)) {  \n"
        //            The complete outside. At Left or Right of the trapezoide.
        "             return 0.0;  \n"
        "        }  \n"
        "    }  \n"
        //   Get here, the pix is partly inside the trapezoid.
        "    {  \n"
        "        float percent = 0.0;  \n"
        "        float up = (source_texture.y - y_per_pix/2.0) >= trap_top ?  \n"
        "                       (source_texture.y - y_per_pix/2.0) : trap_top;  \n"
        "        float bottom = (source_texture.y + y_per_pix/2.0) <= trap_bottom ?  \n"
        "                       (source_texture.y + y_per_pix/2.0) : trap_bottom;  \n"
        "        float left = source_texture.x - x_per_pix/2.0;  \n"
        "        float right = source_texture.x + x_per_pix/2.0;  \n"
        "        \n"
        "        percent = (bottom - up) / y_per_pix;  \n"
        "        \n"
        "	     if(trap_left_vertical == true) {  \n"
        "            if(trap_left_x > source_texture.x - x_per_pix/2.0 &&  \n"
        "                     trap_left_x < source_texture.x + x_per_pix/2.0)  \n"
        "                left = trap_left_x;  \n"
        "        }  \n"
        "	     if(trap_right_vertical == true) {  \n"
        "            if(trap_right_x > source_texture.x - x_per_pix/2.0 &&  \n"
        "                     trap_right_x < source_texture.x + x_per_pix/2.0)  \n"
        "                right = trap_right_x;  \n"
        "        }  \n"
        "        if((up >= bottom) || (left >= right))  \n"
        "            return 0.0;  \n"
        "        \n"
        "        percent = percent * ((right - left)/x_per_pix);  \n"
        "        if(trap_left_vertical == true && trap_right_vertical == true)  \n"
        "            return percent;  \n"
        "        \n"
        "        if(trap_left_vertical != true) {  \n"
        "            float area;  \n"
        //           the slope should never be 0.0 here
        "            float up_x = trap_left_x + (up - trap_left_y)/trap_left_slope;  \n"
        "            float bottom_x = trap_left_x + (bottom - trap_left_y)/trap_left_slope;  \n"
        "            if(trap_left_slope < 0.0 && up_x > left) {  \n"
        /*   case 1
           |
           ----+------------------------------------->
           |                 /
           |                /
           |           +---/--------+
           |           |  /.........|
           |           | /..........|
           |           |/...........|
           |           /............|
           |          /|............|
           |           +------------+
           |
           \|/
         */
        "                float left_y = trap_left_y + trap_left_slope*(left - trap_left_x);  \n"
        "                if((up_x > left) && (left_y > up)) {  \n"
        "                    area = 0.5 * (up_x - left) * (left_y - up);  \n"
        "                    if(up_x > right) {  \n"
        "                        float right_y = trap_left_y  \n"
        "                                  + trap_left_slope*(right - trap_left_x);  \n"
        "                        area = area - 0.5 * (up_x - right) * (right_y - up);  \n"
        "                    }  \n"
        "                    if(left_y > bottom) {  \n"
        "                        area = area - 0.5 * (bottom_x - left) * (left_y - bottom);  \n"
        "                    }  \n"
        "                } else {  \n"
        "                    area = 0.0;  \n"
        "                }  \n"
        "                percent = percent * (1.0 - (area/((right-left)*(bottom-up))));  \n"
        "            } else if(trap_left_slope > 0.0 && bottom_x > left) {  \n"
        /*   case 2
           |
           ----+------------------------------------->
           |          \
           |           \
           |           +\-----------+
           |           | \..........|
           |           |  \.........|
           |           |   \........|
           |           |    \.......|
           |           |     \......|
           |           +------\-----+
           |                   \
           |                    \
           \|/
         */
        "                float right_y = trap_left_y + trap_left_slope*(right - trap_left_x);  \n"
        "                if((up_x < right) && (right_y > up)) {  \n"
        "                    area = 0.5 * (right - up_x) * (right_y - up);  \n"
        "                    if(up_x < left) {  \n"
        "                        float left_y = trap_left_y  \n"
        "                                  + trap_left_slope*(left - trap_left_x);  \n"
        "                        area = area - 0.5 * (left - up_x) * (left_y - up);  \n"
        "                    }  \n"
        "                    if(right_y > bottom) {  \n"
        "                        area = area - 0.5 * (right - bottom_x) * (right_y - bottom);  \n"
        "                    }  \n"
        "                } else {  \n"
        "                    area = 0.0;  \n"
        "                }  \n"
        "                percent = percent * (area/((right-left)*(bottom-up)));  \n"
        "            }  \n"
        "        }  \n"
        "        \n"
        "        if(trap_right_vertical != true) {  \n"
        "            float area;  \n"
        //           the slope should never be 0.0 here
        "            float up_x = trap_right_x + (up - trap_right_y)/trap_right_slope;  \n"
        "            float bottom_x = trap_right_x + (bottom - trap_right_y)/trap_right_slope;  \n"
        "            if(trap_right_slope < 0.0 && bottom_x < right) {  \n"
        /*   case 3
           |
           ----+------------------------------------->
           |                     /
           |           +--------/---+
           |           |......./    |
           |           |....../     |
           |           |...../      |
           |           |..../       |
           |           |.../        |
           |           +--/---------+
           |             /
           |
           \|/
         */
        "                float left_y = trap_right_y + trap_right_slope*(left - trap_right_x);  \n"
        "                if((up_x > left) && (left_y > up)) {  \n"
        "                    area = 0.5 * (up_x - left) * (left_y - up);  \n"
        "                    if(up_x > right) {  \n"
        "                        float right_y = trap_right_y  \n"
        "                                  + trap_right_slope*(right - trap_right_x);  \n"
        "                        area = area - 0.5 * (up_x - right) * (right_y - up);  \n"
        "                    }  \n"
        "                    if(left_y > bottom) {  \n"
        "                        area = area - 0.5 * (bottom_x - left) * (left_y - bottom);  \n"
        "                    }  \n"
        "                } else {  \n"
        "                    area = 0.0;  \n"
        "                }  \n"
        "                percent = percent * (area/((right-left)*(bottom-up)));  \n"
        "            } else if(trap_right_slope > 0.0 && up_x < right) {  \n"
        /*   case 4
           |
           ----+------------------------------------->
           |                   \
           |           +--------\---+
           |           |.........\  |
           |           |..........\ |
           |           |...........\|
           |           |............\
           |           |............|\
           |           +------------+ \
           |                           \
           |
           \|/
         */
        "                float right_y = trap_right_y + trap_right_slope*(right - trap_right_x);  \n"
        "                if((up_x < right) && (right_y > up)) {  \n"
        "                    area = 0.5 * (right - up_x) * (right_y - up);  \n"
        "                    if(up_x < left) {  \n"
        "                        float left_y = trap_right_y  \n"
        "                                  + trap_right_slope*(left - trap_right_x);  \n"
        "                        area = area - 0.5 * (left - up_x) * (left_y - up);  \n"
        "                    }  \n"
        "                    if(right_y > bottom) {  \n"
        "                        area = area - 0.5 * (right - bottom_x) * (right_y - bottom);  \n"
        "                    }  \n"
        "                } else {  \n"
        "                    area = 0.0;  \n"
        "                }  \n"
        "                percent = percent * (1.0 - (area/((right-left)*(bottom-up))));  \n"
        "            }  \n"
        "        }  \n"
        "        \n"
        "        return percent;  \n"
        "    }  \n"
        "}  \n"
        "\n"
        "void main()  \n"
        "{  \n"
        "    float alpha_val = get_alpha_val();  \n"
        "    gl_FragColor = vec4(0.0, 0.0, 0.0, alpha_val);  \n"
        "}\n";

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);

    glamor_priv->trapezoid_prog = glCreateProgram();

    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, trapezoid_vs);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, trapezoid_fs);

    glAttachShader(glamor_priv->trapezoid_prog, vs_prog);
    glAttachShader(glamor_priv->trapezoid_prog, fs_prog);

    glBindAttribLocation(glamor_priv->trapezoid_prog,
                         GLAMOR_VERTEX_POS, "v_positionsition");
    glBindAttribLocation(glamor_priv->trapezoid_prog,
                         GLAMOR_VERTEX_SOURCE, "v_texcoord");
    glBindAttribLocation(glamor_priv->trapezoid_prog,
                         GLAMOR_VERTEX_TOP_BOTTOM, "v_top_bottom");
    glBindAttribLocation(glamor_priv->trapezoid_prog,
                         GLAMOR_VERTEX_LEFT_PARAM, "v_left_param");
    glBindAttribLocation(glamor_priv->trapezoid_prog,
                         GLAMOR_VERTEX_RIGHT_PARAM, "v_right_param");

    glamor_link_glsl_prog(screen, glamor_priv->trapezoid_prog, "trapezoid");
}

void
glamor_fini_trapezoid_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glDeleteProgram(glamor_priv->trapezoid_prog);
}

static Bool
_glamor_generate_trapezoid_with_shader(ScreenPtr screen, PicturePtr picture,
                                       xTrapezoid *traps, int ntrap,
                                       BoxRec *bounds)
{
    glamor_screen_private *glamor_priv;
    glamor_pixmap_private *pixmap_priv;
    PixmapPtr pixmap = NULL;
    GLint trapezoid_prog;
    GLfloat xscale, yscale;
    float left_slope, right_slope;
    xTrapezoid *ptrap;
    BoxRec one_trap_bound;
    int nrect_max;
    int i, j;
    float params[4];

    glamor_priv = glamor_get_screen_private(screen);
    trapezoid_prog = glamor_priv->trapezoid_prog;

    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);
    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv)
        || pixmap_priv->type == GLAMOR_TEXTURE_LARGE) { /* should always have here. */
        DEBUGF("GLAMOR_PIXMAP_PRIV_HAS_FBO check failed, fallback\n");
        return FALSE;
    }

    /* First, clear all to zero */
    if (!glamor_solid(pixmap, 0, 0, pixmap_priv->base.pixmap->drawable.width,
                      pixmap_priv->base.pixmap->drawable.height,
                      GXclear, 0xFFFFFFFF, 0)) {
        DEBUGF("glamor_solid failed, fallback\n");
        return FALSE;
    }

    glamor_make_current(glamor_priv);

    glamor_set_destination_pixmap_priv_nc(pixmap_priv);

    pixmap_priv_get_dest_scale(pixmap_priv, (&xscale), (&yscale));

    /* Now draw the Trapezoid mask. */
    glUseProgram(trapezoid_prog);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    nrect_max = GLAMOR_COMPOSITE_VBO_VERT_CNT / (4 * GLAMOR_VERTEX_RIGHT_PARAM);

    for (i = 0; i < ntrap;) {
        float *vertices;
        int mrect;
        int stride;

        mrect = (ntrap - i) > nrect_max ? nrect_max : (ntrap - i);
        vertices = glamor_setup_composite_vbo_for_trapezoid(screen, 4 * mrect);
        stride = glamor_priv->vb_stride / sizeof(float);

        for (j = 0; j < mrect; j++) {
            ptrap = traps + i + j;

            DEBUGF
                ("--- The parameter of xTrapezoid is:\ntop: %d  0x%x\tbottom: %d  0x%x\n"
                 "left:  p1 (%d   0x%x, %d   0x%x)\tp2 (%d   0x%x, %d   0x%x)\n"
                 "right: p1 (%d   0x%x, %d   0x%x)\tp2 (%d   0x%x, %d   0x%x)\n",
                 xFixedToInt(ptrap->top), ptrap->top,
                 xFixedToInt(ptrap->bottom), ptrap->bottom,
                 xFixedToInt(ptrap->left.p1.x), ptrap->left.p1.x,
                 xFixedToInt(ptrap->left.p1.y), ptrap->left.p1.y,
                 xFixedToInt(ptrap->left.p2.x), ptrap->left.p2.x,
                 xFixedToInt(ptrap->left.p2.y), ptrap->left.p2.y,
                 xFixedToInt(ptrap->right.p1.x), ptrap->right.p1.x,
                 xFixedToInt(ptrap->right.p1.y), ptrap->right.p1.y,
                 xFixedToInt(ptrap->right.p2.x), ptrap->right.p2.x,
                 xFixedToInt(ptrap->right.p2.y), ptrap->right.p2.y);

            miTrapezoidBounds(1, ptrap, &one_trap_bound);

            vertices += 2;
            glamor_set_tcoords_ext((pixmap_priv->base.pixmap->drawable.width),
                                   (pixmap_priv->base.pixmap->drawable.height),
                                   (one_trap_bound.x1), (one_trap_bound.y1),
                                   (one_trap_bound.x2), (one_trap_bound.y2),
                                   glamor_priv->yInverted, vertices, stride);
            DEBUGF("tex_vertices --> leftup : %f X %f, rightup: %f X %f,"
                   "rightbottom: %f X %f, leftbottom : %f X %f\n", vertices[0],
                   vertices[1], vertices[1 * stride], vertices[1 * stride + 1],
                   vertices[2 * stride], vertices[2 * stride + 1],
                   vertices[3 * stride], vertices[3 * stride + 1]);

            /* Need to rebase. */
            one_trap_bound.x1 -= bounds->x1;
            one_trap_bound.x2 -= bounds->x1;
            one_trap_bound.y1 -= bounds->y1;
            one_trap_bound.y2 -= bounds->y1;

            vertices -= 2;

            glamor_set_normalize_vcoords_ext(pixmap_priv, xscale, yscale,
                                             one_trap_bound.x1,
                                             one_trap_bound.y1,
                                             one_trap_bound.x2,
                                             one_trap_bound.y2,
                                             glamor_priv->yInverted, vertices,
                                             stride);
            DEBUGF("vertices --> leftup : %f X %f, rightup: %f X %f,"
                   "rightbottom: %f X %f, leftbottom : %f X %f\n", vertices[0],
                   vertices[1], vertices[1 * stride], vertices[1 * stride + 1],
                   vertices[2 * stride], vertices[2 * stride + 1],
                   vertices[3 * stride], vertices[3 * stride + 1]);
            vertices += 4;

            /* Set the top and bottom. */
            params[0] = ((float) ptrap->top) / 65536;
            params[1] = ((float) ptrap->bottom) / 65536;
            glamor_set_const_ext(params, 2, vertices, 4, stride);
            vertices += 2;

            /* Set the left params. */
            params[0] = ((float) ptrap->left.p1.x) / 65536;
            params[1] = ((float) ptrap->left.p1.y) / 65536;

            if (ptrap->left.p1.x == ptrap->left.p2.x) {
                left_slope = 0.0;
                params[3] = 1.0;
            }
            else {
                left_slope = ((float) (ptrap->left.p1.y - ptrap->left.p2.y))
                    / ((float) (ptrap->left.p1.x - ptrap->left.p2.x));
                params[3] = 0.0;
            }
            params[2] = left_slope;
            glamor_set_const_ext(params, 4, vertices, 4, stride);
            vertices += 4;

            /* Set the left params. */
            params[0] = ((float) ptrap->right.p1.x) / 65536;
            params[1] = ((float) ptrap->right.p1.y) / 65536;

            if (ptrap->right.p1.x == ptrap->right.p2.x) {
                right_slope = 0.0;
                params[3] = 1.0;
            }
            else {
                right_slope = ((float) (ptrap->right.p1.y - ptrap->right.p2.y))
                    / ((float) (ptrap->right.p1.x - ptrap->right.p2.x));
                params[3] = 0.0;
            }
            params[2] = right_slope;
            glamor_set_const_ext(params, 4, vertices, 4, stride);
            vertices += 4;

            DEBUGF("trap_top = %f, trap_bottom = %f, "
                   "trap_left_x = %f, trap_left_y = %f, left_slope = %f, "
                   "trap_right_x = %f, trap_right_y = %f, right_slope = %f\n",
                   ((float) ptrap->top) / 65536,
                   ((float) ptrap->bottom) / 65536,
                   ((float) ptrap->left.p1.x) / 65536,
                   ((float) ptrap->left.p1.y) / 65536, left_slope,
                   ((float) ptrap->right.p1.x) / 65536,
                   ((float) ptrap->right.p1.y) / 65536, right_slope);

            glamor_priv->render_nr_verts += 4;
            vertices += 3 * stride;
        }

        i += mrect;

        glamor_put_vbo_space(screen);

        /* Now rendering. */
        if (!glamor_priv->render_nr_verts)
            continue;

        if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
            glDrawRangeElements(GL_TRIANGLES, 0,
                                glamor_priv->render_nr_verts,
                                (glamor_priv->render_nr_verts * 3) / 2,
                                GL_UNSIGNED_SHORT, NULL);
        } else {
            glDrawElements(GL_TRIANGLES,
                           (glamor_priv->render_nr_verts * 3) / 2,
                           GL_UNSIGNED_SHORT, NULL);
        }
    }

    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_BLEND);
    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_TOP_BOTTOM);
    glDisableVertexAttribArray(GLAMOR_VERTEX_LEFT_PARAM);
    glDisableVertexAttribArray(GLAMOR_VERTEX_RIGHT_PARAM);
    return TRUE;
}

#endif                          /*GLAMOR_TRAPEZOID_SHADER */

/**
 * Creates an appropriate picture for temp mask use.
 */
static PicturePtr
glamor_create_mask_picture(ScreenPtr screen,
                           PicturePtr dst,
                           PictFormatPtr pict_format,
                           CARD16 width, CARD16 height, int gpu)
{
    PixmapPtr pixmap;
    PicturePtr picture;
    int error;

    if (!pict_format) {
        if (dst->polyEdge == PolyEdgeSharp)
            pict_format = PictureMatchFormat(screen, 1, PICT_a1);
        else
            pict_format = PictureMatchFormat(screen, 8, PICT_a8);
        if (!pict_format)
            return 0;
    }

    if (gpu) {
        pixmap = glamor_create_pixmap(screen, width, height,
                                      pict_format->depth, 0);
    }
    else {
        pixmap = glamor_create_pixmap(screen, 0, 0,
                                      pict_format->depth,
                                      GLAMOR_CREATE_PIXMAP_CPU);
    }

    if (!pixmap)
        return 0;
    picture = CreatePicture(0, &pixmap->drawable, pict_format,
                            0, 0, serverClient, &error);
    glamor_destroy_pixmap(pixmap);
    return picture;
}

static int
_glamor_trapezoid_bounds(int ntrap, xTrapezoid *traps, BoxPtr box)
{
    int has_large_trapezoid = 0;

    box->y1 = MAXSHORT;
    box->y2 = MINSHORT;
    box->x1 = MAXSHORT;
    box->x2 = MINSHORT;

    for (; ntrap; ntrap--, traps++) {
        INT16 x1, y1, x2, y2;

        if (!xTrapezoidValid(traps))
            continue;
        y1 = xFixedToInt(traps->top);
        if (y1 < box->y1)
            box->y1 = y1;

        y2 = xFixedToInt(xFixedCeil(traps->bottom));
        if (y2 > box->y2)
            box->y2 = y2;

        x1 = xFixedToInt(min
                         (_glamor_linefixedX(&traps->left, traps->top, FALSE),
                          _glamor_linefixedX(&traps->left, traps->bottom,
                                             FALSE)));
        if (x1 < box->x1)
            box->x1 = x1;

        x2 = xFixedToInt(xFixedCeil
                         (max
                          (_glamor_linefixedX(&traps->right, traps->top, TRUE),
                           _glamor_linefixedX(&traps->right, traps->bottom,
                                              TRUE))));
        if (x2 > box->x2)
            box->x2 = x2;

        if (!has_large_trapezoid && (x2 - x1) > 256 && (y2 - y1) > 32)
            has_large_trapezoid = 1;
    }

    return has_large_trapezoid;
}

/**
 * glamor_trapezoids will first try to create a trapezoid mask using shader,
 * if failed, miTrapezoids will generate trapezoid mask accumulating in
 * system memory.
 */
static Bool
_glamor_trapezoids(CARD8 op,
                   PicturePtr src, PicturePtr dst,
                   PictFormatPtr mask_format, INT16 x_src, INT16 y_src,
                   int ntrap, xTrapezoid *traps, Bool fallback)
{
    ScreenPtr screen = dst->pDrawable->pScreen;
    BoxRec bounds;
    PicturePtr picture;
    INT16 x_dst, y_dst;
    INT16 x_rel, y_rel;
    int width, height, stride;
    PixmapPtr pixmap;
    pixman_image_t *image = NULL;
    int ret = 0;
    int has_large_trapezoid;

    /* If a mask format wasn't provided, we get to choose, but behavior should
     * be as if there was no temporary mask the traps were accumulated into.
     */
    if (!mask_format) {
        if (dst->polyEdge == PolyEdgeSharp)
            mask_format = PictureMatchFormat(screen, 1, PICT_a1);
        else
            mask_format = PictureMatchFormat(screen, 8, PICT_a8);
        for (; ntrap; ntrap--, traps++)
            glamor_trapezoids(op, src, dst, mask_format, x_src,
                              y_src, 1, traps);
        return TRUE;
    }

    has_large_trapezoid = _glamor_trapezoid_bounds(ntrap, traps, &bounds);
    DEBUGF("The bounds for all traps is: bounds.x1 = %d, bounds.x2 = %d, "
           "bounds.y1 = %d, bounds.y2 = %d, ---- ntrap = %d\n", bounds.x1,
           bounds.x2, bounds.y1, bounds.y2, ntrap);

    if (bounds.y1 >= bounds.y2 || bounds.x1 >= bounds.x2)
        return TRUE;

    x_dst = traps[0].left.p1.x >> 16;
    y_dst = traps[0].left.p1.y >> 16;

    width = bounds.x2 - bounds.x1;
    height = bounds.y2 - bounds.y1;
    stride = PixmapBytePad(width, mask_format->depth);

#ifdef GLAMOR_TRAPEZOID_SHADER
    /* We seperate the render to two paths.
       Some GL implemetation do not implement the Anti-Alias for triangles
       and polygen's filling. So when the edge is not vertical or horizontal,
       sawtooth will be obvious. The trapezoid is widely used to render wide
       lines and circles. In these case, the line or circle will be divided
       into a large number of small trapezoids to approximate it, so the sawtooth
       at the edge will cause the result not be acceptable.
       When the depth of the mask is 1, there is no Anti-Alias needed, so we
       use the clip logic to generate the result directly(fast path).
       When the depth is not 1, AA is needed and we use a shader to generate
       a temp mask pixmap.
     */
    if (mask_format->depth == 1) {
        ret = _glamor_trapezoids_with_shader(op, src, dst, mask_format,
                                             x_src, y_src, ntrap, traps);
        if (ret)
            return TRUE;
    }
    else {
        if (has_large_trapezoid || ntrap > 256) {
            /* The shader speed is relative slower than pixman when generating big chunk
               trapezoid mask. We fallback to pixman to improve the performance. */
            ;
        }
        else if (dst->polyMode == PolyModeImprecise) {
            /*  The precise mode is that we sample the trapezoid on the centre points of
               an (2*n+1)x(2*n-1) subpixel grid. It is computationally expensive in shader
               and we use inside area ratio to replace it if the polymode == Imprecise. */
            picture = glamor_create_mask_picture(screen, dst, mask_format,
                                                 width, height, 1);
            if (!picture)
                return TRUE;

            ret =
                _glamor_generate_trapezoid_with_shader(screen, picture, traps,
                                                       ntrap, &bounds);

            if (!ret)
                FreePicture(picture, 0);
        }
    }
#endif

    if (!ret) {
        DEBUGF("Fallback to sw rasterize of trapezoid\n");

        picture = glamor_create_mask_picture(screen, dst, mask_format,
                                             width, height, 0);
        if (!picture)
            return TRUE;

        image = pixman_image_create_bits(picture->format,
                                         width, height, NULL, stride);
        if (!image) {
            FreePicture(picture, 0);
            return TRUE;
        }

        for (; ntrap; ntrap--, traps++)
            pixman_rasterize_trapezoid(image,
                                       (pixman_trapezoid_t *) traps,
                                       -bounds.x1, -bounds.y1);

        pixmap = glamor_get_drawable_pixmap(picture->pDrawable);

        screen->ModifyPixmapHeader(pixmap, width, height,
                                   mask_format->depth,
                                   BitsPerPixel(mask_format->depth),
                                   PixmapBytePad(width,
                                                 mask_format->depth),
                                   pixman_image_get_data(image));
    }

    x_rel = bounds.x1 + x_src - x_dst;
    y_rel = bounds.y1 + y_src - y_dst;
    DEBUGF("x_src = %d, y_src = %d, x_dst = %d, y_dst = %d, "
           "x_rel = %d, y_rel = %d\n", x_src, y_src, x_dst,
           y_dst, x_rel, y_rel);

    CompositePicture(op, src, picture, dst,
                     x_rel, y_rel,
                     0, 0,
                     bounds.x1, bounds.y1,
                     bounds.x2 - bounds.x1, bounds.y2 - bounds.y1);

    if (image)
        pixman_image_unref(image);

    FreePicture(picture, 0);
    return TRUE;
}

void
glamor_trapezoids(CARD8 op,
                  PicturePtr src, PicturePtr dst,
                  PictFormatPtr mask_format, INT16 x_src, INT16 y_src,
                  int ntrap, xTrapezoid *traps)
{
    DEBUGF("x_src = %d, y_src = %d, ntrap = %d\n", x_src, y_src, ntrap);

    _glamor_trapezoids(op, src, dst, mask_format, x_src,
                       y_src, ntrap, traps, TRUE);
}

Bool
glamor_trapezoids_nf(CARD8 op,
                     PicturePtr src, PicturePtr dst,
                     PictFormatPtr mask_format, INT16 x_src, INT16 y_src,
                     int ntrap, xTrapezoid *traps)
{
    DEBUGF("x_src = %d, y_src = %d, ntrap = %d\n", x_src, y_src, ntrap);

    return _glamor_trapezoids(op, src, dst, mask_format, x_src,
                              y_src, ntrap, traps, FALSE);
}

#endif                          /* RENDER */
