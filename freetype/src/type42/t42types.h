/***************************************************************************/
/*                                                                         */
/*  t42types.h                                                             */
/*                                                                         */
/*    Type 42 font data types (specification only).                        */
/*                                                                         */
/*  Copyright 2002-2015 by                                                 */
/*  Roberto Alameda.                                                       */
/*                                                                         */
/*  This file is part of the FreeType project, and may only be used,       */
/*  modified, and distributed under the terms of the FreeType project      */
/*  license, LICENSE.TXT.  By continuing to use, modify, or distribute     */
/*  this file you indicate that you have read the license and              */
/*  understand and accept it fully.                                        */
/*                                                                         */
/***************************************************************************/


#ifndef __T42TYPES_H__
#define __T42TYPES_H__


#include <ft2build.h>
#include <freetype.h>
#include <t1tables.h>
#include <internal/t1types.h>
#include <internal/pshints.h>


FT_BEGIN_HEADER


  typedef struct  T42_FaceRec_
  {
    FT_FaceRec      root;
    T1_FontRec      type1;
    const void*     psnames;
    const void*     psaux;
#if 0
    const void*     afm_data;
#endif
    FT_Byte*        ttf_data;
    FT_Long         ttf_size;
    FT_Face         ttf_face;
    FT_CharMapRec   charmaprecs[2];
    FT_CharMap      charmaps[2];
    PS_UnicodesRec  unicode_map;

  } T42_FaceRec, *T42_Face;


FT_END_HEADER

#endif /* __T42TYPES_H__ */


/* END */
