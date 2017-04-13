/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

// This class RB_MAP contains the top-level calculation functions which are used to generate rb id map based rb id equations

#ifndef __RB_MAP_H
#define __RB_MAP_H

#include "coord.h"

class RB_MAP
{
public:

    enum MAX_VALUES {
        MAX_SES_LOG2 = 3,
        MAX_RBS_LOG2 = 2
    };

    enum COMPRESSED_DATABLOCKS_IN_METABLOCK_PER_RB_LOG2 {
        COMPRESSED_DATABLOCKS_IN_METABLOCK_PER_RB_LOG2_2D = 10,
        COMPRESSED_DATABLOCKS_IN_METABLOCK_PER_RB_LOG2_3D = 18
    };

    RB_MAP(void);

    void Get_Comp_Block_Screen_Space( CoordEq& addr, int bytes_log2, int* w, int* h, int* d = NULL);

    void Get_Meta_Block_Screen_Space( int num_comp_blocks_log2, bool is_thick, bool y_biased,
                                      int comp_block_width_log2, int comp_block_height_log2, int comp_block_depth_log2,
                                      int& meta_block_width_log2, int& meta_block_height_log2, int& meta_block_depth_log2 );
    void cap_pipe( int xmode, bool is_thick, int& num_ses_log2, int bpp_log2, int num_samples_log2, int pipe_interleave_log2,
                   int& block_size_log2, int& num_pipes_log2 );

    void Get_Data_Offset_Equation( CoordEq& data_eq, int data_type, int bpp_log2, int num_samples_log2, int block_size_log2 );

    void Get_RB_Equation( CoordEq& rb_equation, int num_ses_log2, int num_rbs_log2 );

    void Get_Pipe_Equation( CoordEq& pipe_equation, CoordEq& addr,
                           int pipe_interleave_log2,
                           int num_pipes_log2,
                           int block_size_log2,
                           int num_samples_log2,
                           int xmode, int data_type
                         );

    void get_meta_miptail_coord( int& x, int& y, int& z, int mip_in_tail, int blk_width_log2, int blk_height_log2, int blk_depth_log2 );

    void get_mip_coord( int& x, int& y, int& z, int mip,
                        int meta_blk_width_log2, int meta_blk_height_log2, int meta_blk_depth_log2,
                        int data_blk_width_log2, int data_blk_height_log2,
                        int& surf_width, int& surf_height, int& surf_depth, int epitch, int max_mip,
                        int data_type, int bpp_log2, bool meta_linear );

    void get_mip_coord_linear( int& x, int& y, int& z, int mip, int data_blk_width_log2, int data_blk_height_log2,
                               int& surf_width, int& surf_height, int& surf_depth, int epitch, int max_mip, int data_type, int bpp_log2 );

    void get_mip_coord_nonlinear( int& x, int& y, int& z, int mip, int meta_blk_width_log2, int meta_blk_height_log2, int meta_blk_depth_log2,
                                  int& surf_width, int& surf_height, int& surf_depth, int epitch, int max_mip, int data_type );

    void get_meta_eq( CoordEq& metaaddr, int max_mip, int num_ses_log2, int num_rbs_log2, int &num_pipes_log2,
                      int block_size_log2, int bpp_log2, int num_samples_log2, int max_comp_frag_log2,
                      int pipe_interleave_log2, int xmode, int data_type, int meta_alignment, bool meta_linear);

#if 0
    long get_meta_addr( int x, int y, int z, int s, int mip,
                        int surf_width, int surf_height, int surf_depth, int epitch,
                        long surf_base, int pipe_xor, int max_mip,
                        int num_ses_log2, int num_rbs_log2, int num_pipes_log2,
                        int block_size_log2, int bpp_log2, int num_samples_log2, int max_comp_frag_log2,
                        int pipe_interleave_log2, int xmode, int data_type, int meta_alignment, bool meta_linear);
#endif

    long get_meta_addr_calc( int x, int y, int z, int s,
                    long surf_base, int element_bytes_log2, int num_samples_log2, int max_comp_frag_log2,
                    long pitch, long slice,
                    int max_mip,
                    //int swizzle_mode,
                    int xmode, int pipe_xor, int block_size_log2,
                    /*int num_banks_log2,*/ int num_pipes_log2,
                    int pipe_interleave_log2, int meta_alignment, int dim_type, int x_mip_org, int y_mip_org,
                    int z_mip_org, int num_ses_log2, int num_rbs_log2, /*bool se_affinity_enable,*/ int data_type,
                    int l2_metablk_w, int l2_metablk_h, int l2_metablk_d, bool meta_linear);

    void Initialize(void);

public:
    enum XOR_RANGE {
        NONE = 0,
        XOR = 1,
        PRT = 2
    };


    enum DATA_TYPE_ENUM {
        DATA_COLOR1D,
        DATA_COLOR2D,
        DATA_COLOR3D_S,
        DATA_COLOR3D_Z,
        DATA_Z_STENCIL,
        DATA_FMASK,
        DATA_COLOR2D_LINEAR,
        DATA_COLOR3D_D_NOT_USED  // should not be used; use COLOR2D instead
    };

    enum META_ALIGNMENT {
        META_ALIGN_NONE,
        META_ALIGN_PIPE,
        META_ALIGN_RB,
        META_ALIGN_PIPE_RB
    };

    CoordEq   rb_equation[MAX_SES_LOG2+1][MAX_RBS_LOG2+1];
    CoordEq   zaddr [4][4];
    CoordEq   caddr [5][4];
    CoordEq   c3addr[5][2];
};

#endif
