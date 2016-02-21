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
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */

#ifndef GLAMOR_PRIV_H
#error This file can only be included by glamor_priv.h
#endif

#ifndef __GLAMOR_UTILS_H__
#define __GLAMOR_UTILS_H__

#include "glamor_prepare.h"
#include "mipict.h"

#define v_from_x_coord_x(_xscale_, _x_)          ( 2 * (_x_) * (_xscale_) - 1.0)
#define v_from_x_coord_y(_yscale_, _y_)          (2 * (_y_) * (_yscale_) - 1.0)
#define t_from_x_coord_x(_xscale_, _x_)          ((_x_) * (_xscale_))
#define t_from_x_coord_y(_yscale_, _y_)          ((_y_) * (_yscale_))

#define pixmap_priv_get_dest_scale(pixmap, _pixmap_priv_, _pxscale_, _pyscale_) \
  do {                                                                   \
    int _w_,_h_;                                                         \
    PIXMAP_PRIV_GET_ACTUAL_SIZE(pixmap, _pixmap_priv_, _w_, _h_);        \
    *(_pxscale_) = 1.0 / _w_;                                            \
    *(_pyscale_) = 1.0 / _h_;                                            \
   } while(0)

#define pixmap_priv_get_scale(_pixmap_priv_, _pxscale_, _pyscale_)	\
   do {									\
    *(_pxscale_) = 1.0 / (_pixmap_priv_)->fbo->width;			\
    *(_pyscale_) = 1.0 / (_pixmap_priv_)->fbo->height;			\
  } while(0)

#define PIXMAP_PRIV_GET_ACTUAL_SIZE(pixmap, priv, w, h)          \
  do {								\
	if (_X_UNLIKELY(glamor_pixmap_priv_is_large(priv))) {	\
		w = priv->box.x2 - priv->box.x1;	\
		h = priv->box.y2 - priv->box.y1;	\
	} else {						\
		w = (pixmap)->drawable.width;		\
		h = (pixmap)->drawable.height;		\
	}							\
  } while(0)

#define glamor_pixmap_fbo_fix_wh_ratio(wh, pixmap, priv)         \
  do {								\
	int actual_w, actual_h;					\
	PIXMAP_PRIV_GET_ACTUAL_SIZE(pixmap, priv, actual_w, actual_h);	\
	wh[0] = (float)priv->fbo->width / actual_w;	\
	wh[1] = (float)priv->fbo->height / actual_h;	\
	wh[2] = 1.0 / priv->fbo->width;			\
	wh[3] = 1.0 / priv->fbo->height;			\
  } while(0)

#define pixmap_priv_get_fbo_off(_priv_, _xoff_, _yoff_)		\
   do {								\
        if (_X_UNLIKELY(_priv_ && glamor_pixmap_priv_is_large(_priv_))) { \
		*(_xoff_) = - (_priv_)->box.x1;	\
		*(_yoff_) = - (_priv_)->box.y1;	\
	} else {						\
		*(_xoff_) = 0;					\
		*(_yoff_) = 0;					\
	}							\
   } while(0)

#define xFixedToFloat(_val_) ((float)xFixedToInt(_val_)			\
			      + ((float)xFixedFrac(_val_) / 65536.0))

#define glamor_picture_get_matrixf(_picture_, _matrix_)			\
  do {									\
    int _i_;								\
    if ((_picture_)->transform)						\
      {									\
	for(_i_ = 0; _i_ < 3; _i_++)					\
	  {								\
	    (_matrix_)[_i_ * 3 + 0] =					\
	      xFixedToFloat((_picture_)->transform->matrix[_i_][0]);	\
	    (_matrix_)[_i_ * 3 + 1] =					\
	      xFixedToFloat((_picture_)->transform->matrix[_i_][1]);	\
	    (_matrix_)[_i_ * 3 + 2] = \
	      xFixedToFloat((_picture_)->transform->matrix[_i_][2]);	\
	  }								\
      }									\
  }  while(0)

#define fmod(x, w)		(x - w * floor((float)x/w))

#define fmodulus(x, w, c)	do {c = fmod(x, w);		\
				    c = c >= 0 ? c : c + w;}	\
				while(0)
/* @x: is current coord
 * @x2: is the right/bottom edge
 * @w: is current width or height
 * @odd: is output value, 0 means we are in an even region, 1 means we are in a
 * odd region.
 * @c: is output value, equal to x mod w. */
#define fodd_repeat_mod(x, x2, w, odd, c)	\
  do {						\
	float shift;				\
	fmodulus((x), w, c); 			\
	shift = fabs((x) - (c));		\
	shift = floor(fabs(round(shift)) / w);	\
	odd = (int)shift & 1;			\
	if (odd && (((x2 % w) == 0) &&		\
	    round(fabs(x)) == x2))		\
		odd = 0;			\
  } while(0)

/* @txy: output value, is the corrected coords.
 * @xy: input coords to be fixed up.
 * @cd: xy mod wh, is a input value.
 * @wh: current width or height.
 * @bxy1,bxy2: current box edge's x1/x2 or y1/y2
 *
 * case 1:
 *  ----------
 *  |  *     |
 *  |        |
 *  ----------
 *  tx = (c - x1) mod w
 *
 *  case 2:
 *     ---------
 *  *  |       |
 *     |       |
 *     ---------
 *   tx = - (c - (x1 mod w))
 *
 *   case 3:
 *
 *   ----------
 *   |        |  *
 *   |        |
 *   ----------
 *   tx = ((x2 mod x) - c) + (x2 - x1)
 **/
#define __glamor_repeat_reflect_fixup(txy, xy,		\
				cd, wh, bxy1, bxy2)	\
  do {							\
	cd = wh - cd;					\
	if ( xy >= bxy1 && xy < bxy2) {			\
		cd = cd - bxy1;				\
		fmodulus(cd, wh, txy);			\
	} else	if (xy < bxy1) {			\
		float bxy1_mod;				\
		fmodulus(bxy1, wh, bxy1_mod);		\
		txy = -(cd - bxy1_mod);			\
	}						\
	else if (xy >= bxy2)	{			\
		float bxy2_mod;				\
		fmodulus(bxy2, wh, bxy2_mod);		\
		if (bxy2_mod == 0)			\
			bxy2_mod = wh;			\
		txy = (bxy2_mod - cd) + bxy2 - bxy1;	\
	} else {assert(0); txy = 0;}			\
  } while(0)

#define _glamor_repeat_reflect_fixup(txy, xy, cd, odd,	\
				     wh, bxy1, bxy2)	\
  do {							\
	if (odd) {					\
		__glamor_repeat_reflect_fixup(txy, xy, 	\
			cd, wh, bxy1, bxy2);		\
	} else						\
		txy = xy - bxy1;			\
  } while(0)

#define _glamor_get_reflect_transform_coords(pixmap, priv, repeat_type,	\
					    tx1, ty1, 		\
				            _x1_, _y1_)		\
  do {								\
	int odd_x, odd_y;					\
	float c, d;						\
	fodd_repeat_mod(_x1_,priv->box.x2,			\
		    (pixmap)->drawable.width,		\
		    odd_x, c);					\
	fodd_repeat_mod(_y1_,	priv->box.y2,			\
		    (pixmap)->drawable.height,		\
		    odd_y, d);					\
	DEBUGF("c %f d %f oddx %d oddy %d \n",			\
		c, d, odd_x, odd_y);				\
	DEBUGF("x2 %d x1 %d fbo->width %d \n", priv->box.x2,	\
		priv->box.x1, priv->fbo->width);		\
	DEBUGF("y2 %d y1 %d fbo->height %d \n", priv->box.y2, 	\
		priv->box.y1, priv->fbo->height);		\
	_glamor_repeat_reflect_fixup(tx1, _x1_, c, odd_x,	\
		(pixmap)->drawable.width,		\
		priv->box.x1, priv->box.x2);			\
	_glamor_repeat_reflect_fixup(ty1, _y1_, d, odd_y,	\
		(pixmap)->drawable.height,		\
		priv->box.y1, priv->box.y2);			\
   } while(0)

#define _glamor_get_repeat_coords(pixmap, priv, repeat_type, tx1,	\
				  ty1, tx2, ty2,		\
				  _x1_, _y1_, _x2_,		\
				  _y2_, c, d, odd_x, odd_y)	\
  do {								\
	if (repeat_type == RepeatReflect) {			\
		DEBUGF("x1 y1 %d %d\n",				\
			_x1_, _y1_ );				\
		DEBUGF("width %d box.x1 %d \n",			\
		       (pixmap)->drawable.width,	\
		       priv->box.x1);				\
		if (odd_x) {					\
			c = (pixmap)->drawable.width	\
				- c;				\
			tx1 = c - priv->box.x1;			\
			tx2 = tx1 - ((_x2_) - (_x1_));		\
		} else {					\
			tx1 = c - priv->box.x1;			\
			tx2 = tx1 + ((_x2_) - (_x1_));		\
		}						\
		if (odd_y){					\
			d = (pixmap)->drawable.height\
			    - d;				\
			ty1 = d - priv->box.y1;			\
			ty2 = ty1 - ((_y2_) - (_y1_));		\
		} else {					\
			ty1 = d - priv->box.y1;			\
			ty2 = ty1 + ((_y2_) - (_y1_));		\
		}						\
	} else { /* RepeatNormal*/				\
		tx1 = (c - priv->box.x1);  			\
		ty1 = (d - priv->box.y1);			\
		tx2 = tx1 + ((_x2_) - (_x1_));			\
		ty2 = ty1 + ((_y2_) - (_y1_));			\
	}							\
   } while(0)

/* _x1_ ... _y2_ may has fractional. */
#define glamor_get_repeat_transform_coords(pixmap, priv, repeat_type, tx1, \
					   ty1, _x1_, _y1_)		\
  do {									\
	DEBUGF("width %d box.x1 %d x2 %d y1 %d y2 %d\n",		\
		(pixmap)->drawable.width,			\
		priv->box.x1, priv->box.x2, priv->box.y1,		\
		priv->box.y2);						\
	DEBUGF("x1 %f y1 %f \n", _x1_, _y1_);				\
	if (repeat_type != RepeatReflect) {				\
		tx1 = _x1_ - priv->box.x1;				\
		ty1 = _y1_ - priv->box.y1;				\
	} else			\
                _glamor_get_reflect_transform_coords(pixmap, priv, repeat_type, \
				  tx1, ty1, 				\
				  _x1_, _y1_);				\
	DEBUGF("tx1 %f ty1 %f \n", tx1, ty1);				\
   } while(0)

/* _x1_ ... _y2_ must be integer. */
#define glamor_get_repeat_coords(pixmap, priv, repeat_type, tx1,		\
				 ty1, tx2, ty2, _x1_, _y1_, _x2_,	\
				 _y2_) 					\
  do {									\
	int c, d;							\
	int odd_x = 0, odd_y = 0;					\
	DEBUGF("width %d box.x1 %d x2 %d y1 %d y2 %d\n",		\
		(pixmap)->drawable.width,			\
		priv->box.x1, priv->box.x2,				\
		priv->box.y1, priv->box.y2);				\
	modulus((_x1_), (pixmap)->drawable.width, c); 	\
	modulus((_y1_), (pixmap)->drawable.height, d);	\
	DEBUGF("c %d d %d \n", c, d);					\
	if (repeat_type == RepeatReflect) {				\
		odd_x = abs((_x1_ - c)					\
                            / ((pixmap)->drawable.width)) & 1;            \
		odd_y = abs((_y1_ - d)					\
                            / ((pixmap)->drawable.height)) & 1;           \
	}								\
	_glamor_get_repeat_coords(pixmap, priv, repeat_type, tx1, ty1, tx2, ty2, \
				  _x1_, _y1_, _x2_, _y2_, c, d,		\
				  odd_x, odd_y);			\
   } while(0)

#define glamor_transform_point(matrix, tx, ty, x, y)			\
  do {									\
    int _i_;								\
    float _result_[4];							\
    for (_i_ = 0; _i_ < 3; _i_++) {					\
      _result_[_i_] = (matrix)[_i_ * 3] * (x) + (matrix)[_i_ * 3 + 1] * (y)	\
	+ (matrix)[_i_ * 3 + 2];					\
    }									\
    tx = _result_[0] / _result_[2];					\
    ty = _result_[1] / _result_[2];					\
  } while(0)

#define _glamor_set_normalize_tpoint(xscale, yscale, _tx_, _ty_,	\
				     texcoord)                          \
  do {									\
	(texcoord)[0] = t_from_x_coord_x(xscale, _tx_);			\
        (texcoord)[1] = t_from_x_coord_y(yscale, _ty_);                 \
        DEBUGF("normalized point tx %f ty %f \n", (texcoord)[0],	\
		(texcoord)[1]);						\
  } while(0)

#define glamor_set_transformed_point(priv, matrix, xscale,              \
				     yscale, texcoord,			\
                                     x, y)				\
  do {									\
    float tx, ty;							\
    int fbo_x_off, fbo_y_off;						\
    pixmap_priv_get_fbo_off(priv, &fbo_x_off, &fbo_y_off);		\
    glamor_transform_point(matrix, tx, ty, x, y);			\
    DEBUGF("tx %f ty %f fbooff %d %d \n",				\
	    tx, ty, fbo_x_off, fbo_y_off);				\
									\
    tx += fbo_x_off;							\
    ty += fbo_y_off;							\
    (texcoord)[0] = t_from_x_coord_x(xscale, tx);			\
    (texcoord)[1] = t_from_x_coord_y(yscale, ty);                       \
    DEBUGF("normalized tx %f ty %f \n", (texcoord)[0], (texcoord)[1]);	\
  } while(0)

#define glamor_set_transformed_normalize_tri_tcoords(priv,		\
						     matrix,		\
						     xscale,		\
						     yscale,		\
						     vtx,		\
						     texcoords)		\
    do {								\
	glamor_set_transformed_point(priv, matrix, xscale, yscale,	\
				     texcoords, (vtx)[0], (vtx)[1]);    \
	glamor_set_transformed_point(priv, matrix, xscale, yscale,	\
				     texcoords+2, (vtx)[2], (vtx)[3]);  \
	glamor_set_transformed_point(priv, matrix, xscale, yscale,	\
				     texcoords+4, (vtx)[4], (vtx)[5]);  \
    } while (0)

#define glamor_set_transformed_normalize_tcoords_ext( priv,		\
						  matrix,		\
						  xscale,		\
						  yscale,		\
                                                  tx1, ty1, tx2, ty2,   \
                                                  texcoords,		\
						  stride)		\
  do {									\
    glamor_set_transformed_point(priv, matrix, xscale, yscale,		\
				 texcoords, tx1, ty1);                  \
    glamor_set_transformed_point(priv, matrix, xscale, yscale,		\
				 texcoords + 1 * stride, tx2, ty1);     \
    glamor_set_transformed_point(priv, matrix, xscale, yscale,		\
				 texcoords + 2 * stride, tx2, ty2);     \
    glamor_set_transformed_point(priv, matrix, xscale, yscale,		\
				 texcoords + 3 * stride, tx1, ty2);     \
  } while (0)

#define glamor_set_transformed_normalize_tcoords( priv,			\
						  matrix,		\
						  xscale,		\
						  yscale,		\
                                                  tx1, ty1, tx2, ty2,   \
                                                  texcoords)            \
  do {									\
	glamor_set_transformed_normalize_tcoords_ext( priv,		\
						  matrix,		\
						  xscale,		\
						  yscale,		\
                                                  tx1, ty1, tx2, ty2,   \
                                                  texcoords,		\
						  2);			\
  } while (0)

#define glamor_set_normalize_tri_tcoords(xscale,		\
					 yscale,		\
					 vtx,			\
					 texcoords)		\
    do {							\
	_glamor_set_normalize_tpoint(xscale, yscale,		\
				(vtx)[0], (vtx)[1],		\
				texcoords);			\
	_glamor_set_normalize_tpoint(xscale, yscale,		\
				(vtx)[2], (vtx)[3],		\
				texcoords+2);			\
	_glamor_set_normalize_tpoint(xscale, yscale,		\
				(vtx)[4], (vtx)[5],		\
				texcoords+4);			\
    } while (0)

#define glamor_set_repeat_transformed_normalize_tcoords_ext(pixmap, priv, \
							 repeat_type,	\
							 matrix,	\
							 xscale,	\
							 yscale,	\
							 _x1_, _y1_,	\
							 _x2_, _y2_,   	\
							 texcoords,	\
							 stride)	\
  do {									\
    if (_X_LIKELY(glamor_pixmap_priv_is_small(priv))) {		\
	glamor_set_transformed_normalize_tcoords_ext(priv, matrix, xscale,	\
						 yscale, _x1_, _y1_,	\
						 _x2_, _y2_,	\
						 texcoords, stride);	\
    } else {								\
    float tx1, ty1, tx2, ty2, tx3, ty3, tx4, ty4;			\
    float ttx1, tty1, ttx2, tty2, ttx3, tty3, ttx4, tty4;		\
    DEBUGF("original coords %d %d %d %d\n", _x1_, _y1_, _x2_, _y2_);	\
    glamor_transform_point(matrix, tx1, ty1, _x1_, _y1_);		\
    glamor_transform_point(matrix, tx2, ty2, _x2_, _y1_);		\
    glamor_transform_point(matrix, tx3, ty3, _x2_, _y2_);		\
    glamor_transform_point(matrix, tx4, ty4, _x1_, _y2_);		\
    DEBUGF("transformed %f %f %f %f %f %f %f %f\n",			\
	   tx1, ty1, tx2, ty2, tx3, ty3, tx4, ty4);			\
    glamor_get_repeat_transform_coords(pixmap, priv, repeat_type, \
				       ttx1, tty1, 			\
				       tx1, ty1);			\
    glamor_get_repeat_transform_coords(pixmap, priv, repeat_type, 	\
				       ttx2, tty2, 			\
				       tx2, ty2);			\
    glamor_get_repeat_transform_coords(pixmap, priv, repeat_type, 	\
				       ttx3, tty3, 			\
				       tx3, ty3);			\
    glamor_get_repeat_transform_coords(pixmap, priv, repeat_type, 	\
				       ttx4, tty4, 			\
				       tx4, ty4);			\
    DEBUGF("repeat transformed %f %f %f %f %f %f %f %f\n", ttx1, tty1, 	\
	    ttx2, tty2,	ttx3, tty3, ttx4, tty4);			\
    _glamor_set_normalize_tpoint(xscale, yscale, ttx1, tty1,		\
				 texcoords);			\
    _glamor_set_normalize_tpoint(xscale, yscale, ttx2, tty2,		\
				 texcoords + 1 * stride);	\
    _glamor_set_normalize_tpoint(xscale, yscale, ttx3, tty3,		\
				 texcoords + 2 * stride);	\
    _glamor_set_normalize_tpoint(xscale, yscale, ttx4, tty4,		\
				 texcoords + 3 * stride);	\
   }									\
  } while (0)

#define glamor_set_repeat_transformed_normalize_tcoords( pixmap,        \
                                                         priv,          \
							 repeat_type,	\
							 matrix,	\
							 xscale,	\
							 yscale,	\
							 _x1_, _y1_,	\
							 _x2_, _y2_,   	\
							 texcoords)	\
  do {									\
      glamor_set_repeat_transformed_normalize_tcoords_ext( pixmap,      \
                                                           priv,	\
							 repeat_type,	\
							 matrix,	\
							 xscale,	\
							 yscale,	\
							 _x1_, _y1_,	\
							 _x2_, _y2_,   	\
							 texcoords,	\
							 2);	\
  } while (0)

#define _glamor_set_normalize_tcoords(xscale, yscale, tx1,		\
				      ty1, tx2, ty2,			\
				      vertices, stride)                 \
  do {									\
    /* vertices may be write-only, so we use following			\
     * temporary variable. */ 						\
    float _t0_, _t1_, _t2_, _t5_;					\
    (vertices)[0] = _t0_ = t_from_x_coord_x(xscale, tx1);		\
    (vertices)[1 * stride] = _t2_ = t_from_x_coord_x(xscale, tx2);	\
    (vertices)[2 * stride] = _t2_;					\
    (vertices)[3 * stride] = _t0_;					\
    (vertices)[1] = _t1_ = t_from_x_coord_y(yscale, ty1);               \
    (vertices)[2 * stride + 1] = _t5_ = t_from_x_coord_y(yscale, ty2);  \
    (vertices)[1 * stride + 1] = _t1_;					\
    (vertices)[3 * stride + 1] = _t5_;					\
  } while(0)

#define glamor_set_normalize_tcoords_ext(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                     vertices, stride)	\
  do {									\
     if (_X_UNLIKELY(glamor_pixmap_priv_is_large(priv))) {		\
	float tx1, tx2, ty1, ty2;					\
	int fbo_x_off, fbo_y_off;					\
	pixmap_priv_get_fbo_off(priv, &fbo_x_off, &fbo_y_off);		\
	tx1 = x1 + fbo_x_off; 						\
	tx2 = x2 + fbo_x_off;						\
	ty1 = y1 + fbo_y_off;						\
	ty2 = y2 + fbo_y_off;						\
	_glamor_set_normalize_tcoords(xscale, yscale, tx1, ty1,		\
                                      tx2, ty2, vertices,               \
				   stride);				\
     } else								\
	_glamor_set_normalize_tcoords(xscale, yscale, x1, y1,		\
                                      x2, y2, vertices, stride);        \
 } while(0)

#define glamor_set_normalize_tcoords(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                     vertices)		\
  do {									\
	glamor_set_normalize_tcoords_ext(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                     vertices, 2);			\
 } while(0)

#define glamor_set_repeat_normalize_tcoords_ext(pixmap, priv, repeat_type, \
					    xscale, yscale,		\
					    _x1_, _y1_, _x2_, _y2_,	\
	                                    vertices, stride)		\
  do {									\
     if (_X_UNLIKELY(glamor_pixmap_priv_is_large(priv))) {		\
	float tx1, tx2, ty1, ty2;					\
	if (repeat_type == RepeatPad) {					\
		tx1 = _x1_ - priv->box.x1;			        \
		ty1 = _y1_ - priv->box.y1;			        \
		tx2 = tx1 + ((_x2_) - (_x1_));				\
		ty2 = ty1 + ((_y2_) - (_y1_));				\
	} else {							\
            glamor_get_repeat_coords(pixmap, priv, repeat_type,         \
				 tx1, ty1, tx2, ty2,			\
				 _x1_, _y1_, _x2_, _y2_);		\
	}								\
	_glamor_set_normalize_tcoords(xscale, yscale, tx1, ty1,		\
                                      tx2, ty2, vertices,               \
				   stride);				\
     } else								\
	_glamor_set_normalize_tcoords(xscale, yscale, _x1_, _y1_,	\
                                      _x2_, _y2_, vertices,             \
				   stride);				\
 } while(0)

#define glamor_set_repeat_normalize_tcoords(priv, repeat_type,		\
					    xscale, yscale,		\
					    _x1_, _y1_, _x2_, _y2_,	\
	                                    vertices)                   \
  do {									\
	glamor_set_repeat_normalize_tcoords_ext(priv, repeat_type,	\
					    xscale, yscale,		\
					    _x1_, _y1_, _x2_, _y2_,	\
	                                    vertices, 2);		\
 } while(0)

#define glamor_set_normalize_tcoords_tri_stripe(xscale, yscale,		\
						x1, y1, x2, y2,		\
						vertices)               \
    do {								\
	(vertices)[0] = t_from_x_coord_x(xscale, x1);			\
	(vertices)[2] = t_from_x_coord_x(xscale, x2);			\
	(vertices)[6] = (vertices)[2];					\
	(vertices)[4] = (vertices)[0];					\
        (vertices)[1] = t_from_x_coord_y(yscale, y1);                   \
        (vertices)[7] = t_from_x_coord_y(yscale, y2);                   \
	(vertices)[3] = (vertices)[1];					\
	(vertices)[5] = (vertices)[7];					\
    } while(0)

#define glamor_set_tcoords(x1, y1, x2, y2, vertices)            \
    do {							\
	(vertices)[0] = (x1);					\
	(vertices)[2] = (x2);					\
	(vertices)[4] = (vertices)[2];				\
	(vertices)[6] = (vertices)[0];				\
        (vertices)[1] = (y1);                                   \
        (vertices)[5] = (y2);                                   \
	(vertices)[3] = (vertices)[1];				\
	(vertices)[7] = (vertices)[5];				\
    } while(0)

#define glamor_set_tcoords_ext(x1, y1, x2, y2, vertices, stride)        \
    do {							\
	(vertices)[0] = (x1);					\
	(vertices)[1*stride] = (x2);				\
	(vertices)[2*stride] = (vertices)[1*stride];		\
	(vertices)[3*stride] = (vertices)[0];			\
        (vertices)[1] = (y1);                                   \
        (vertices)[2*stride + 1] = (y2);			\
	(vertices)[1*stride + 1] = (vertices)[1];		\
	(vertices)[3*stride + 1] = (vertices)[2*stride + 1];	\
    } while(0)

#define glamor_set_normalize_one_vcoord(xscale, yscale, x, y,		\
					vertices)                       \
    do {								\
	(vertices)[0] = v_from_x_coord_x(xscale, x);			\
        (vertices)[1] = v_from_x_coord_y(yscale, y);                    \
    } while(0)

#define glamor_set_normalize_tri_vcoords(xscale, yscale, vtx,		\
					 vertices)                      \
    do {								\
	glamor_set_normalize_one_vcoord(xscale, yscale,			\
					(vtx)[0], (vtx)[1],		\
					vertices);                      \
	glamor_set_normalize_one_vcoord(xscale, yscale,			\
					(vtx)[2], (vtx)[3],		\
					vertices+2);                    \
	glamor_set_normalize_one_vcoord(xscale, yscale,			\
					(vtx)[4], (vtx)[5],		\
					vertices+4);                    \
    } while(0)

#define glamor_set_tcoords_tri_strip(x1, y1, x2, y2, vertices)          \
    do {								\
	(vertices)[0] = (x1);						\
	(vertices)[2] = (x2);						\
	(vertices)[6] = (vertices)[2];					\
	(vertices)[4] = (vertices)[0];					\
        (vertices)[1] = (y1);                                           \
        (vertices)[7] = (y2);                                           \
	(vertices)[3] = (vertices)[1];					\
	(vertices)[5] = (vertices)[7];					\
    } while(0)

#define glamor_set_normalize_vcoords_ext(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                         vertices, stride)              \
  do {									\
    int fbo_x_off, fbo_y_off;						\
    /* vertices may be write-only, so we use following			\
     * temporary variable. */						\
    float _t0_, _t1_, _t2_, _t5_;					\
    pixmap_priv_get_fbo_off(priv, &fbo_x_off, &fbo_y_off);		\
    (vertices)[0] = _t0_ = v_from_x_coord_x(xscale, x1 + fbo_x_off);	\
    (vertices)[1 * stride] = _t2_ = v_from_x_coord_x(xscale,		\
					x2 + fbo_x_off);		\
    (vertices)[2 * stride] = _t2_;					\
    (vertices)[3 * stride] = _t0_;					\
    (vertices)[1] = _t1_ = v_from_x_coord_y(yscale, y1 + fbo_y_off);    \
    (vertices)[2 * stride + 1] = _t5_ =                                 \
        v_from_x_coord_y(yscale, y2 + fbo_y_off);                       \
    (vertices)[1 * stride + 1] = _t1_;					\
    (vertices)[3 * stride + 1] = _t5_;					\
  } while(0)

#define glamor_set_normalize_vcoords(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                     vertices)				\
  do {									\
	glamor_set_normalize_vcoords_ext(priv, xscale, yscale,		\
				     x1, y1, x2, y2,			\
                                     vertices, 2);			\
  } while(0)

#define glamor_set_const_ext(params, nparam, vertices, nverts, stride)	\
    do {								\
	int _i_ = 0, _j_ = 0;						\
	for(; _i_ < nverts; _i_++) {					\
	    for(_j_ = 0; _j_ < nparam; _j_++) {				\
		vertices[stride*_i_ + _j_] = params[_j_];		\
	    }								\
	}								\
    } while(0)

#define glamor_set_normalize_vcoords_tri_strip(xscale, yscale,		\
					       x1, y1, x2, y2,		\
					       vertices)		\
    do {								\
	(vertices)[0] = v_from_x_coord_x(xscale, x1);			\
	(vertices)[2] = v_from_x_coord_x(xscale, x2);			\
	(vertices)[6] = (vertices)[2];					\
	(vertices)[4] = (vertices)[0];					\
        (vertices)[1] = v_from_x_coord_y(yscale, y1);                   \
        (vertices)[7] = v_from_x_coord_y(yscale, y2);                   \
	(vertices)[3] = (vertices)[1];					\
	(vertices)[5] = (vertices)[7];					\
    } while(0)

#define glamor_set_normalize_pt(xscale, yscale, x, y,		\
                                pt)				\
    do {							\
        (pt)[0] = t_from_x_coord_x(xscale, x);			\
        (pt)[1] = t_from_x_coord_y(yscale, y);                  \
    } while(0)

#define glamor_set_circle_centre(width, height, x, y,	\
				 c)		\
    do {						\
        (c)[0] = (float)x;				\
        (c)[1] = (float)y;				\
    } while(0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define ALIGN(i,m)	(((i) + (m) - 1) & ~((m) - 1))
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX(a,b)	((a) > (b) ? (a) : (b))

#define glamor_check_fbo_size(_glamor_,_w_, _h_)    ((_w_) > 0 && (_h_) > 0 \
                                                    && (_w_) <= _glamor_->max_fbo_size  \
                                                    && (_h_) <= _glamor_->max_fbo_size)

/* For 1bpp pixmap, we don't store it as texture. */
#define glamor_check_pixmap_fbo_depth(_depth_) (			\
						_depth_ == 8		\
						|| _depth_ == 15	\
						|| _depth_ == 16	\
						|| _depth_ == 24	\
						|| _depth_ == 30	\
						|| _depth_ == 32)

#define GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv)    (pixmap_priv->gl_fbo == GLAMOR_FBO_NORMAL)

/**
 * Borrow from uxa.
 */
static inline CARD32
format_for_depth(int depth)
{
    switch (depth) {
    case 1:
        return PICT_a1;
    case 4:
        return PICT_a4;
    case 8:
        return PICT_a8;
    case 15:
        return PICT_x1r5g5b5;
    case 16:
        return PICT_r5g6b5;
    default:
    case 24:
        return PICT_x8r8g8b8;
#if XORG_VERSION_CURRENT >= 10699900
    case 30:
        return PICT_x2r10g10b10;
#endif
    case 32:
        return PICT_a8r8g8b8;
    }
}

static inline GLenum
gl_iformat_for_pixmap(PixmapPtr pixmap)
{
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private((pixmap)->drawable.pScreen);

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP &&
        ((pixmap)->drawable.depth == 1 || (pixmap)->drawable.depth == 8)) {
        return glamor_priv->one_channel_format;
    } else {
        return GL_RGBA;
    }
}

static inline CARD32
format_for_pixmap(PixmapPtr pixmap)
{
    return format_for_depth((pixmap)->drawable.depth);
}

#define REVERT_NONE       		0
#define REVERT_NORMAL     		1
#define REVERT_DOWNLOADING_A1		2
#define REVERT_UPLOADING_A1		3
#define REVERT_DOWNLOADING_2_10_10_10 	4
#define REVERT_UPLOADING_2_10_10_10 	5
#define REVERT_DOWNLOADING_1_5_5_5  	7
#define REVERT_UPLOADING_1_5_5_5    	8
#define REVERT_DOWNLOADING_10_10_10_2 	9
#define REVERT_UPLOADING_10_10_10_2 	10

#define SWAP_NONE_DOWNLOADING  	0
#define SWAP_DOWNLOADING  	1
#define SWAP_UPLOADING	  	2
#define SWAP_NONE_UPLOADING	3

/* borrowed from uxa */
static inline Bool
glamor_get_rgba_from_pixel(CARD32 pixel,
                           float *red,
                           float *green,
                           float *blue, float *alpha, CARD32 format)
{
    int rbits, bbits, gbits, abits;
    int rshift, bshift, gshift, ashift;

    rbits = PICT_FORMAT_R(format);
    gbits = PICT_FORMAT_G(format);
    bbits = PICT_FORMAT_B(format);
    abits = PICT_FORMAT_A(format);

    if (PICT_FORMAT_TYPE(format) == PICT_TYPE_A) {
        rshift = gshift = bshift = ashift = 0;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ARGB) {
        bshift = 0;
        gshift = bbits;
        rshift = gshift + gbits;
        ashift = rshift + rbits;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ABGR) {
        rshift = 0;
        gshift = rbits;
        bshift = gshift + gbits;
        ashift = bshift + bbits;
#if XORG_VERSION_CURRENT >= 10699900
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_BGRA) {
        ashift = 0;
        rshift = abits;
        if (abits == 0)
            rshift = PICT_FORMAT_BPP(format) - (rbits + gbits + bbits);
        gshift = rshift + rbits;
        bshift = gshift + gbits;
#endif
    }
    else {
        return FALSE;
    }
#define COLOR_INT_TO_FLOAT(_fc_, _p_, _s_, _bits_)	\
  *_fc_ = (((_p_) >> (_s_)) & (( 1 << (_bits_)) - 1))	\
    / (float)((1<<(_bits_)) - 1)

    if (rbits)
        COLOR_INT_TO_FLOAT(red, pixel, rshift, rbits);
    else
        *red = 0;

    if (gbits)
        COLOR_INT_TO_FLOAT(green, pixel, gshift, gbits);
    else
        *green = 0;

    if (bbits)
        COLOR_INT_TO_FLOAT(blue, pixel, bshift, bbits);
    else
        *blue = 0;

    if (abits)
        COLOR_INT_TO_FLOAT(alpha, pixel, ashift, abits);
    else
        *alpha = 1;

    return TRUE;
}

inline static Bool
glamor_is_large_pixmap(PixmapPtr pixmap)
{
    glamor_pixmap_private *priv;

    priv = glamor_get_pixmap_private(pixmap);
    return (glamor_pixmap_priv_is_large(priv));
}

static inline void
_glamor_dump_pixmap_bits(PixmapPtr pixmap, int x, int y, int w, int h)
{
    int i, j;
    unsigned char *p = (pixmap)->devPrivate.ptr;
    int stride = (pixmap)->devKind;

    p = p + y * stride + x;

    for (i = 0; i < h; i++) {
        ErrorF("line %3d: ", i);
        for (j = 0; j < w; j++)
            ErrorF("%2d ", (p[j / 8] & (1 << (j % 8))) >> (j % 8));
        p += stride;
        ErrorF("\n");
    }
}

static inline void
_glamor_dump_pixmap_byte(PixmapPtr pixmap, int x, int y, int w, int h)
{
    int i, j;
    unsigned char *p = (pixmap)->devPrivate.ptr;
    int stride = (pixmap)->devKind;

    p = p + y * stride + x;

    for (i = 0; i < h; i++) {
        ErrorF("line %3d: ", i);
        for (j = 0; j < w; j++)
            ErrorF("%2x ", p[j]);
        p += stride;
        ErrorF("\n");
    }
}

static inline void
_glamor_dump_pixmap_sword(PixmapPtr pixmap, int x, int y, int w, int h)
{
    int i, j;
    unsigned short *p = (pixmap)->devPrivate.ptr;
    int stride = (pixmap)->devKind / 2;

    p = p + y * stride + x;

    for (i = 0; i < h; i++) {
        ErrorF("line %3d: ", i);
        for (j = 0; j < w; j++)
            ErrorF("%2x ", p[j]);
        p += stride;
        ErrorF("\n");
    }
}

static inline void
_glamor_dump_pixmap_word(PixmapPtr pixmap, int x, int y, int w, int h)
{
    int i, j;
    unsigned int *p = (pixmap)->devPrivate.ptr;
    int stride = (pixmap)->devKind / 4;

    p = p + y * stride + x;

    for (i = 0; i < h; i++) {
        ErrorF("line %3d: ", i);
        for (j = 0; j < w; j++)
            ErrorF("%2x ", p[j]);
        p += stride;
        ErrorF("\n");
    }
}

static inline void
glamor_dump_pixmap(PixmapPtr pixmap, int x, int y, int w, int h)
{
    w = ((x + w) > (pixmap)->drawable.width) ? ((pixmap)->drawable.width - x) : w;
    h = ((y + h) > (pixmap)->drawable.height) ? ((pixmap)->drawable.height - y) : h;

    glamor_prepare_access(&(pixmap)->drawable, GLAMOR_ACCESS_RO);
    switch ((pixmap)->drawable.depth) {
    case 8:
        _glamor_dump_pixmap_byte(pixmap, x, y, w, h);
        break;
    case 15:
    case 16:
        _glamor_dump_pixmap_sword(pixmap, x, y, w, h);
        break;

    case 24:
    case 32:
        _glamor_dump_pixmap_word(pixmap, x, y, w, h);
        break;
    case 1:
        _glamor_dump_pixmap_bits(pixmap, x, y, w, h);
        break;
    default:
        ErrorF("dump depth %d, not implemented.\n", (pixmap)->drawable.depth);
    }
    glamor_finish_access(&(pixmap)->drawable);
}

static inline void
_glamor_compare_pixmaps(PixmapPtr pixmap1, PixmapPtr pixmap2,
                        int x, int y, int w, int h,
                        PictFormatShort short_format, int all, int diffs)
{
    int i, j;
    unsigned char *p1 = pixmap1->devPrivate.ptr;
    unsigned char *p2 = pixmap2->devPrivate.ptr;
    int line_need_printed = 0;
    int test_code = 0xAABBCCDD;
    int little_endian = 0;
    unsigned char *p_test;
    int bpp = pixmap1->drawable.depth == 8 ? 1 : 4;
    int stride = pixmap1->devKind;

    assert(pixmap1->devKind == pixmap2->devKind);

    ErrorF("stride:%d, width:%d, height:%d\n", stride, w, h);

    p1 = p1 + y * stride + x;
    p2 = p2 + y * stride + x;

    if (all) {
        for (i = 0; i < h; i++) {
            ErrorF("line %3d: ", i);

            for (j = 0; j < stride; j++) {
                if (j % bpp == 0)
                    ErrorF("[%d]%2x:%2x ", j / bpp, p1[j], p2[j]);
                else
                    ErrorF("%2x:%2x ", p1[j], p2[j]);
            }

            p1 += stride;
            p2 += stride;
            ErrorF("\n");
        }
    }
    else {
        if (short_format == PICT_a8r8g8b8) {
            p_test = (unsigned char *) &test_code;
            little_endian = (*p_test == 0xDD);
            bpp = 4;

            for (i = 0; i < h; i++) {
                line_need_printed = 0;

                for (j = 0; j < stride; j++) {
                    if (p1[j] != p2[j] &&
                        (p1[j] - p2[j] > diffs || p2[j] - p1[j] > diffs)) {
                        if (line_need_printed) {
                            if (little_endian) {
                                switch (j % 4) {
                                case 2:
                                    ErrorF("[%d]RED:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 1:
                                    ErrorF("[%d]GREEN:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 0:
                                    ErrorF("[%d]BLUE:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 3:
                                    ErrorF("[%d]Alpha:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                }
                            }
                            else {
                                switch (j % 4) {
                                case 1:
                                    ErrorF("[%d]RED:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 2:
                                    ErrorF("[%d]GREEN:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 3:
                                    ErrorF("[%d]BLUE:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                case 0:
                                    ErrorF("[%d]Alpha:%2x:%2x ", j / bpp, p1[j],
                                           p2[j]);
                                    break;
                                }
                            }
                        }
                        else {
                            line_need_printed = 1;
                            j = -1;
                            ErrorF("line %3d: ", i);
                            continue;
                        }
                    }
                }

                p1 += stride;
                p2 += stride;
                ErrorF("\n");
            }
        }                       //more format can be added here.
        else {                  // the default format, just print.
            for (i = 0; i < h; i++) {
                line_need_printed = 0;

                for (j = 0; j < stride; j++) {
                    if (p1[j] != p2[j]) {
                        if (line_need_printed) {
                            ErrorF("[%d]%2x:%2x ", j / bpp, p1[j], p2[j]);
                        }
                        else {
                            line_need_printed = 1;
                            j = -1;
                            ErrorF("line %3d: ", i);
                            continue;
                        }
                    }
                }

                p1 += stride;
                p2 += stride;
                ErrorF("\n");
            }
        }
    }
}

static inline void
glamor_compare_pixmaps(PixmapPtr pixmap1, PixmapPtr pixmap2,
                       int x, int y, int w, int h, int all, int diffs)
{
    assert(pixmap1->drawable.depth == pixmap2->drawable.depth);

    if (glamor_prepare_access(&pixmap1->drawable, GLAMOR_ACCESS_RO) &&
        glamor_prepare_access(&pixmap2->drawable, GLAMOR_ACCESS_RO)) {
        _glamor_compare_pixmaps(pixmap1, pixmap2, x, y, w, h, -1, all, diffs);
    }
    glamor_finish_access(&pixmap1->drawable);
    glamor_finish_access(&pixmap2->drawable);
}

/* This function is used to compare two pictures.
   If the picture has no drawable, we use fb functions to generate it. */
static inline void
glamor_compare_pictures(ScreenPtr screen,
                        PicturePtr fst_picture,
                        PicturePtr snd_picture,
                        int x_source, int y_source,
                        int width, int height, int all, int diffs)
{
    PixmapPtr fst_pixmap;
    PixmapPtr snd_pixmap;
    int fst_generated, snd_generated;
    int error;
    int fst_type = -1;
    int snd_type = -1;          // -1 represent has drawable.

    if (fst_picture->format != snd_picture->format) {
        ErrorF("Different picture format can not compare!\n");
        return;
    }

    if (!fst_picture->pDrawable) {
        fst_type = fst_picture->pSourcePict->type;
    }

    if (!snd_picture->pDrawable) {
        snd_type = snd_picture->pSourcePict->type;
    }

    if ((fst_type != -1) && (snd_type != -1) && (fst_type != snd_type)) {
        ErrorF("Different picture type will never be same!\n");
        return;
    }

    fst_generated = snd_generated = 0;

    if (!fst_picture->pDrawable) {
        PicturePtr pixman_pic;
        PixmapPtr pixmap = NULL;
        PictFormatShort format;

        format = fst_picture->format;

        pixmap = glamor_create_pixmap(screen,
                                      width, height,
                                      PIXMAN_FORMAT_DEPTH(format),
                                      GLAMOR_CREATE_PIXMAP_CPU);

        pixman_pic = CreatePicture(0,
                                   &(pixmap)->drawable,
                                   PictureMatchFormat(screen,
                                                      PIXMAN_FORMAT_DEPTH
                                                      (format), format), 0, 0,
                                   serverClient, &error);

        fbComposite(PictOpSrc, fst_picture, NULL, pixman_pic,
                    x_source, y_source, 0, 0, 0, 0, width, height);

        glamor_destroy_pixmap(pixmap);

        fst_picture = pixman_pic;
        fst_generated = 1;
    }

    if (!snd_picture->pDrawable) {
        PicturePtr pixman_pic;
        PixmapPtr pixmap = NULL;
        PictFormatShort format;

        format = snd_picture->format;

        pixmap = glamor_create_pixmap(screen,
                                      width, height,
                                      PIXMAN_FORMAT_DEPTH(format),
                                      GLAMOR_CREATE_PIXMAP_CPU);

        pixman_pic = CreatePicture(0,
                                   &(pixmap)->drawable,
                                   PictureMatchFormat(screen,
                                                      PIXMAN_FORMAT_DEPTH
                                                      (format), format), 0, 0,
                                   serverClient, &error);

        fbComposite(PictOpSrc, snd_picture, NULL, pixman_pic,
                    x_source, y_source, 0, 0, 0, 0, width, height);

        glamor_destroy_pixmap(pixmap);

        snd_picture = pixman_pic;
        snd_generated = 1;
    }

    fst_pixmap = glamor_get_drawable_pixmap(fst_picture->pDrawable);
    snd_pixmap = glamor_get_drawable_pixmap(snd_picture->pDrawable);

    if (fst_pixmap->drawable.depth != snd_pixmap->drawable.depth) {
        if (fst_generated)
            miDestroyPicture(fst_picture);
        if (snd_generated)
            miDestroyPicture(snd_picture);

        ErrorF("Different pixmap depth can not compare!\n");
        return;
    }

    if ((fst_type == SourcePictTypeLinear) ||
        (fst_type == SourcePictTypeRadial) ||
        (fst_type == SourcePictTypeConical) ||
        (snd_type == SourcePictTypeLinear) ||
        (snd_type == SourcePictTypeRadial) ||
        (snd_type == SourcePictTypeConical)) {
        x_source = y_source = 0;
    }

    if (glamor_prepare_access(&fst_pixmap->drawable, GLAMOR_ACCESS_RO) &&
        glamor_prepare_access(&snd_pixmap->drawable, GLAMOR_ACCESS_RO)) {
        _glamor_compare_pixmaps(fst_pixmap, snd_pixmap,
                                x_source, y_source,
                                width, height, fst_picture->format,
                                all, diffs);
    }
    glamor_finish_access(&fst_pixmap->drawable);
    glamor_finish_access(&snd_pixmap->drawable);

    if (fst_generated)
        miDestroyPicture(fst_picture);
    if (snd_generated)
        miDestroyPicture(snd_picture);

    return;
}

#ifdef __i386__
static inline unsigned long
__fls(unsigned long x)
{
 asm("bsr %1,%0":"=r"(x)
 :     "rm"(x));
    return x;
}
#else
static inline unsigned long
__fls(unsigned long x)
{
    int n;

    if (x == 0)
        return (0);
    n = 0;
    if (x <= 0x0000FFFF) {
        n = n + 16;
        x = x << 16;
    }
    if (x <= 0x00FFFFFF) {
        n = n + 8;
        x = x << 8;
    }
    if (x <= 0x0FFFFFFF) {
        n = n + 4;
        x = x << 4;
    }
    if (x <= 0x3FFFFFFF) {
        n = n + 2;
        x = x << 2;
    }
    if (x <= 0x7FFFFFFF) {
        n = n + 1;
    }
    return 31 - n;
}
#endif

static inline void
glamor_make_current(glamor_screen_private *glamor_priv)
{
    if (lastGLContext != &glamor_priv->ctx) {
        lastGLContext = &glamor_priv->ctx;
        glamor_priv->ctx.make_current(&glamor_priv->ctx);
    }
}

/**
 * Helper function for implementing draws with GL_QUADS on GLES2,
 * where we don't have them.
 */
static inline void
glamor_glDrawArrays_GL_QUADS(glamor_screen_private *glamor_priv, unsigned count)
{
    if (glamor_priv->use_quads) {
        glDrawArrays(GL_QUADS, 0, count * 4);
    } else {
        glamor_gldrawarrays_quads_using_indices(glamor_priv, count);
    }
}


#endif
