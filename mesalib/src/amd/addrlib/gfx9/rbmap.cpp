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

// This class generates rb id map based rb id equations

//#define DPI_DEBUG 1
// Unlock more verbose debug messages (V* borrows from dj -v * to indicate most verbosity)
//#define DPI_DEBUG_V4 1
//#define DPI_DEBUG_V5 1
//#define DPI_DEBUG_PIPE_CASES 1
//          "----+----|----+----|----+----|----+----|"
#include "addrcommon.h"
#include "rbmap.h"

RB_MAP::RB_MAP(void)
{
    Initialize();
}

VOID RB_MAP::Get_Comp_Block_Screen_Space( CoordEq& addr, int bytes_log2, int* w, int* h, int* d)
{
    int n, i;
    if( w ) *w = 0;
    if( h ) *h = 0;
    if( d ) *d = 0;
    for( n=0; n<bytes_log2; n++ ) { // go up to the bytes_log2 bit
        for( i=0; (unsigned)i<addr[n].getsize(); i++ ) {
            char dim = addr[n][i].getdim();
            int ord = addr[n][i].getord();
            if( w && dim == 'x' && ord >= *w ) *w = ord+1;
            if( h && dim == 'y' && ord >= *h ) *h = ord+1;
            if( d && dim == 'z' && ord >= *d ) *d = ord+1;
        }
    }
}

void
RB_MAP::Get_Meta_Block_Screen_Space( int num_comp_blocks_log2, bool is_thick, bool y_biased,
                                  int comp_block_width_log2, int comp_block_height_log2, int comp_block_depth_log2,

                                     // Outputs
                                  int& meta_block_width_log2, int& meta_block_height_log2, int& meta_block_depth_log2 )
{
    meta_block_width_log2 = comp_block_width_log2;
    meta_block_height_log2 = comp_block_height_log2;
    meta_block_depth_log2 = comp_block_depth_log2;
    int n;

    for( n=0; n<num_comp_blocks_log2; n++ ) {
        if( (meta_block_height_log2 < meta_block_width_log2) ||
            (y_biased && (meta_block_height_log2 == meta_block_width_log2)) ) {
            if ( !is_thick || (meta_block_height_log2 <= meta_block_depth_log2) )
                meta_block_height_log2++;
            else
                meta_block_depth_log2++;
        }
        else {
            if ( !is_thick || (meta_block_width_log2 <= meta_block_depth_log2) )
                meta_block_width_log2++;
            else
                meta_block_depth_log2++;
        }
    }
}

void
RB_MAP::cap_pipe( int xmode, bool is_thick, int& num_ses_log2, int bpp_log2, int num_samples_log2, int pipe_interleave_log2, int& block_size_log2, int& num_pipes_log2 )
{
    // pipes+SEs can't exceed 32 for now
    if( num_pipes_log2+num_ses_log2 > 5 ) {
        num_pipes_log2 = 5-num_ses_log2;
    }

    // Since we are not supporting SE affinity anymore, just add nu_ses to num_pipes, and set num_ses to 0
    num_pipes_log2 += num_ses_log2;
    num_ses_log2 = 0;

    // If block size is set to variable (0), compute the size
    if( block_size_log2 == 0 ) {
        //
        //TODO Temporary disable till RTL can drive Var signals properly
    }

    if( xmode != NONE ) {
        int max_pipes_log2 = block_size_log2 - pipe_interleave_log2;
        if( is_thick ) {
            // For 3d, treat the num_pipes as the sum of num_pipes and gpus
            num_pipes_log2 = num_pipes_log2 + num_ses_log2;
            num_ses_log2 = 0;
        } else {
            int block_space_used = num_pipes_log2+pipe_interleave_log2;
            if( block_space_used < 10+bpp_log2 ) block_space_used = 10+bpp_log2;
            // if the num gpus exceeds however many bits we have left between block size and block_space_used+num_samples
            // then set num_ses_log2 to 0
            if( num_ses_log2 > block_size_log2 - block_space_used - num_samples_log2) {
                num_pipes_log2 = num_pipes_log2 + num_ses_log2;
                num_ses_log2 = 0;
            }
        }
        if( num_pipes_log2 > max_pipes_log2 ) {
            // If it exceeds the space we have left, cap it to that
            num_pipes_log2 = max_pipes_log2;
        }
    } else {
        num_pipes_log2 = num_pipes_log2 + num_ses_log2;
        num_ses_log2 = 0;
    }
}

void RB_MAP::Get_Data_Offset_Equation( CoordEq& data_eq, int data_type, int bpp_log2, int num_samples_log2, int block_size_log2 )
{
    bool is_linear = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D_LINEAR );
    bool is_thick = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z );
    bool is_color = ( data_type == DATA_COLOR2D || data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    bool is_s = ( data_type == DATA_COLOR3D_S );
    Coordinate cx( 'x', 0 );
    Coordinate cy( 'y', 0 );
    Coordinate cz( 'z', 0 );
    Coordinate cs( 's', 0 );
    // Clear the equation
    data_eq.resize(0);
    data_eq.resize(27);
    if( block_size_log2 == 0 ) block_size_log2 = 16;

    if( is_linear ) {
        Coordinate cm( 'm', 0 );
        int i;
        data_eq.resize(49);
        for( i=0; i<49; i++ ) {
            data_eq[i].add(cm);
            cm++;
        }
    } else if( is_thick ) {
        // Color 3d (_S and _Z modes; _D is same as color 2d)
        int i;
        if( is_s ) {
            // Standard 3d swizzle
            // Fill in bottom x bits
            for( i=bpp_log2; i<4; i++ ) {
                data_eq[i].add(cx);
                cx++;
            }
            // Fill in 2 bits of y and then z
            for( i=4; i<6; i++ ) {
                data_eq[i].add(cy);
                cy++;
            }
            for( i=6; i<8; i++ ) {
                data_eq[i].add(cz);
                cz++;
            }
            if (bpp_log2 < 2) {
                // fill in z & y bit
                data_eq[8].add(cz);
                data_eq[9].add(cy);
                cz++;
                cy++;
            } else if( bpp_log2 == 2 ) {
                // fill in y and x bit
                data_eq[8].add(cy);
                data_eq[9].add(cx);
                cy++;
                cx++;
            } else {
                // fill in 2 x bits
                data_eq[8].add(cx);
                cx++;
                data_eq[9].add(cx);
                cx++;
            }
        } else {
            // Z 3d swizzle
            int m2d_end = (bpp_log2==0) ? 3 : ((bpp_log2 < 4) ? 4 : 5);
            int num_zs = (bpp_log2==0 || bpp_log2==4) ? 2 : ((bpp_log2==1) ? 3 : 1);
            data_eq.mort2d( cx, cy, bpp_log2, m2d_end );
            for( i=m2d_end+1; i<=m2d_end+num_zs; i++ ) {
                data_eq[i].add(cz);
                cz++;
            }
            if( bpp_log2 == 0 || bpp_log2 == 3 ) {
                // add an x and z
                data_eq[6].add(cx);
                data_eq[7].add(cz);
                cx++;
                cz++;
            } else if( bpp_log2 == 2 ) {
                // add a y and z
                data_eq[6].add(cy);
                data_eq[7].add(cz);
                cy++;
                cz++;
            }
            // add y and x
            data_eq[8].add(cy);
            data_eq[9].add(cx);
            cy++;
            cx++;
        }
        // Fill in bit 10 and up
        data_eq.mort3d( cz, cy, cx, 10 );
    } else if( is_color ) {
        // Color 2D
        int micro_y_bits = (8-bpp_log2) / 2;
        int tile_split_start = block_size_log2 - num_samples_log2;
        int i;
        // Fill in bottom x bits
        for( i=bpp_log2;i<4; i++ ) {
            data_eq[i].add(cx);
            cx++;
        }
        // Fill in bottom y bits
        for( i=4; i<4+micro_y_bits; i++ ) {
            data_eq[i].add(cy);
            cy++;
        }
        // Fill in last of the micro_x bits
        for( i=4+micro_y_bits; i<8; i++ ) {
            data_eq[i].add(cx);
            cx++;
        }
        // Fill in x/y bits below sample split
        data_eq.mort2d( cy, cx, 8, tile_split_start-1 );
        // Fill in sample bits
        for( i=0; i<num_samples_log2; i++ ) {
            cs.set( 's', i );
            data_eq[tile_split_start+i].add(cs);
        }
        // Fill in x/y bits above sample split
        if( (num_samples_log2 & 1) ^ (block_size_log2 & 1) ) data_eq.mort2d( cx, cy, block_size_log2 );
        else data_eq.mort2d( cy, cx, block_size_log2 );
    } else {
        // Z, stencil or fmask
        // First, figure out where each section of bits starts
        int sample_start = bpp_log2;
        int pixel_start = bpp_log2 + num_samples_log2;
        int y_maj_start = 6 + num_samples_log2;

        // Put in sample bits
        int s;
        for( s=0; s<num_samples_log2; s++ ) {
            cs.set( 's', s );
            data_eq[sample_start+s].add(cs);
        }
        // Put in the x-major order pixel bits
        data_eq.mort2d( cx, cy, pixel_start, y_maj_start-1 );
        // Put in the y-major order pixel bits
        data_eq.mort2d( cy, cx, y_maj_start );
    }
}

void RB_MAP::Get_RB_Equation( CoordEq& rb_equation, int num_ses_log2, int num_rbs_log2 )
{
    // RB's are distributed on 16x16, except when we have 1 rb per se, in which case its 32x32
    int rb_region = (num_rbs_log2 == 0) ? 5 : 4;
    Coordinate cx( 'x', rb_region );
    Coordinate cy( 'y', rb_region );
    int i, start = 0, num_total_rbs_log2 = num_ses_log2 + num_rbs_log2;
    // Clear the rb equation
    rb_equation.resize(0);
    rb_equation.resize(num_total_rbs_log2);
    if( num_ses_log2 > 0 && num_rbs_log2 == 1 ) {
        // Special case when more than 1 SE, and only 1 RB per SE
        rb_equation[0].add(cx);
        rb_equation[0].add(cy);
        cx++;
        cy++;
        rb_equation[0].add(cy);
        start++;
    }
    for( i=0; i<2*(num_total_rbs_log2-start); i++ ) {
        int index = start + (((start+i)>=num_total_rbs_log2) ? 2*(num_total_rbs_log2-start)-i-1 : i);
        Coordinate& c = ((i % 2) == 1) ? cx : cy;
        rb_equation[index].add(c);
        c++;
    }
}

//void getcheq( CoordEq& pipe_equation, CoordEq& addr, int pipe_interleave_log2, int num_pipes_log2,
void
RB_MAP::Get_Pipe_Equation( CoordEq& pipe_equation, CoordEq& addr,
                           int pipe_interleave_log2,
                           int num_pipes_log2,

                           int block_size_log2,
                           int num_samples_log2,

                           int xmode, int data_type
                         )
{
    int pipe;
    CoordEq addr_f, xormask, xormask2;
    Coordinate tile_min( 'x', 3 );

    bool is_color = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D || data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR2D_LINEAR || data_type == DATA_COLOR3D_D_NOT_USED );
    bool is_thick = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z );

    // For color, filter out sample bits only
    // otherwise filter out everything under an 8x8 tile
    if( is_color )
        tile_min.set( 'x', 0 );

    addr.copy( addr_f );

    // Z/stencil is no longer tile split
    if( is_color )
        addr_f.shift( -num_samples_log2, block_size_log2- num_samples_log2 );

    int i;
    addr_f.copy( pipe_equation, pipe_interleave_log2, num_pipes_log2 ); //@todo kr needs num_ses_log2??


    // This section should only apply to z/stencil, maybe fmask
    // If the pipe bit is below the comp block size, then keep moving up the address until we find a bit that is above
    for( pipe=0; addr_f[pipe_interleave_log2 + pipe][0] < tile_min; pipe++ ) {
    }

    // if pipe is 0, then the first pipe bit is above the comp block size, so we don't need to do anything
    // Note, this if condition is not necessary, since if we execute the loop when pipe==0, we will get the same pipe equation
    if ( pipe != 0 ) {
        int j = pipe;


        for( i=0; i<num_pipes_log2; i++ ) {
            // Copy the jth bit above pipe interleave to the current pipe equation bit
            addr_f[pipe_interleave_log2 + j].copyto(pipe_equation[i]);
            j++;


        }


    }

    if( xmode == PRT ) {
        // Clear out bits above the block size if prt's are enabled
        addr_f.resize(block_size_log2);
        addr_f.resize(48);
    }

    if( xmode != NONE ) {
        if( is_thick ) {
            addr_f.copy( xormask2, pipe_interleave_log2+num_pipes_log2, 2*num_pipes_log2 );

            xormask.resize( num_pipes_log2 );
            for( pipe=0; pipe<num_pipes_log2; pipe++ ) {
                xormask[pipe].add( xormask2[2*pipe] );
                xormask[pipe].add( xormask2[2*pipe+1] );
            }
        } else {
            Coordinate co;
            // Xor in the bits above the pipe+gpu bits
            addr_f.copy( xormask, pipe_interleave_log2 + pipe + num_pipes_log2, num_pipes_log2 );
            if( num_samples_log2 == 0 && (xmode != PRT) ) {
                // if 1xaa and not prt, then xor in the z bits
                xormask2.resize(0);
                xormask2.resize(num_pipes_log2);
                for( pipe=0; pipe<num_pipes_log2; pipe++ ) {
                    co.set( 'z', num_pipes_log2-1 - pipe );
                    xormask2[pipe].add( co );
                }

                pipe_equation.xorin( xormask2 );
            }
        }

        xormask.reverse();
        pipe_equation.xorin( xormask );

    }
}

void RB_MAP::get_meta_miptail_coord( int& x, int& y, int& z, int mip_in_tail, int blk_width_log2, int blk_height_log2, int blk_depth_log2 )
{
    bool is_thick = (blk_depth_log2>0);
    int m;
    int mip_width = 1 << blk_width_log2;
    int mip_height = 1 << (blk_height_log2-1);
    int mip_depth = 1 << blk_depth_log2;

    // Find the minimal increment, based on the block size and 2d/3d
    int min_inc;
    if(is_thick) {
        min_inc = (blk_height_log2 >= 9) ? 128 : ((blk_height_log2 == 8) ? 64 : 32);
    } else if(blk_height_log2>=10) {
        min_inc = 256;
    } else if(blk_height_log2==9) {
        min_inc = 128;
    } else {
        min_inc = 64;
    }

    for( m=0; m<mip_in_tail; m++ ) {
        if( mip_width <= 32 ) {
            // special case when below 32x32 mipmap
            switch(mip_in_tail-m) {
            case 0: break;              // 32x32
            case 1: x+=32; break;       // 16x16
            case 2: y+=32; break;       // 8x8
            case 3: y+=32; x+=16; break;// 4x4
            case 4: y+=32; x+=32; break;// 2x2
            case 5: y+=32; x+=48; break;// 1x1
            // The following are for BC/ASTC formats
            case 6: y+=48; break;       // 1/2 x 1/2
            case 7: y+=48; x+=16; break;// 1/4 x 1/4
            case 8: y+=48; x+=32; break;// 1/8 x 1/8
            default:y+=48; x+=48; break;// 1/16 x 1/16
            }
            m = mip_in_tail; // break the loop
        } else {
            if( mip_width <= min_inc ) {
                // if we're below the minimal increment...
                if( is_thick ) {
                    // For 3d, just go in z direction
                    z += mip_depth;
                } else {
                    // For 2d, first go across, then down
                    if( mip_width * 2 == min_inc ) {
                        // if we're 2 mips below, that's when we go back in x, and down in y
                        x -= min_inc;
                        y += min_inc;
                    } else {
                        // otherwise, just go across in x
                        x += min_inc;
                    }
                }
            } else {
                // On even mip, go down, otherwise, go across
                if( m&1 ) {
                    x += mip_width;
                } else {
                    y += mip_height;
                }
            }
            // Divide the width by 2
            mip_width = mip_width / 2;
            // After the first mip in tail, the mip is always a square
            mip_height = mip_width;
            // ...or for 3d, a cube
            if(is_thick) mip_depth = mip_width;
        }
    }
}

void RB_MAP::get_mip_coord( int& x, int& y, int& z, int mip,
                            int meta_blk_width_log2, int meta_blk_height_log2, int meta_blk_depth_log2,
                            int data_blk_width_log2, int data_blk_height_log2,
                            int& surf_width, int& surf_height, int& surf_depth, int epitch, int max_mip,
                            int data_type, int bpp_log2, bool meta_linear )
{
    if( meta_linear ) {
        get_mip_coord_linear( x, y, z, mip, data_blk_width_log2, data_blk_height_log2,
                              surf_width, surf_height, surf_depth, epitch, max_mip, data_type, bpp_log2 );
    } else {
        get_mip_coord_nonlinear( x, y, z, mip, meta_blk_width_log2, meta_blk_height_log2, meta_blk_depth_log2,
                                surf_width, surf_height, surf_depth, epitch, max_mip, data_type );
    }
}

void RB_MAP::get_mip_coord_linear( int& x, int& y, int& z,
                                   int mip,
                                   int data_blk_width_log2, int data_blk_height_log2,
                                   int& surf_width, int& surf_height, int& surf_depth, int epitch,
                                   int max_mip, int data_type, int bpp_log2
                                 )
{
    bool data_linear = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D_LINEAR );

    if( data_linear ) {
        // linear width is padded out to 256 Bytes
        int width_padding = 8 - bpp_log2;
        int width_pad_mask = ~(0xffffffff << width_padding);
        int padded_surf_width = surf_width;
        int padded_surf_height = (data_type == DATA_COLOR1D) ? 1 : surf_height;

        if( max_mip > 0 ) {
            int mip_width = padded_surf_width;
            int mip_height = padded_surf_height;
            int padded_mip_height = 0;
            int mip_base = 0;
            int m = 0;
            while( (mip_width >= 1 || mip_height >= 1) && m <= max_mip ) {
                if( mip == m ) mip_base = padded_mip_height;
                padded_mip_height += mip_height;
                m++;
                mip_width = (mip_width / 2) + (mip_width & 1);
                mip_height = (mip_height / 2) + (mip_height & 1);
            }
            if( mip >= m ) {
                // assert error
                mip_base = padded_mip_height - mip_height;
            }
            padded_surf_height = padded_mip_height;

            if(epitch > 0){
                padded_surf_height = epitch;
            }
            y += mip_base;
            padded_surf_width = ((surf_width >> width_padding) + ((surf_width & width_pad_mask) ? 1 : 0)) << width_padding;
        }
            else{
            padded_surf_width = ((surf_width >> width_padding) + ((surf_width & width_pad_mask) ? 1 : 0)) << width_padding;

            // Pad up epitch to meta block width
            if( (epitch & width_pad_mask) != 0 ) {
                epitch = ((epitch >> width_padding) + 1) << width_padding;
            }
            // Take max of epitch and computed surf width
            if( epitch < padded_surf_width ) {
                // assert error
            } else {
                padded_surf_width = epitch;
            }
        }

        surf_width = padded_surf_width;
        surf_height = padded_surf_height;
    }
    else {
        // padding based data block size
        int width_pad_mask  = ~(0xffffffff << data_blk_width_log2);
        int height_pad_mask = ~(0xffffffff << data_blk_height_log2);

        // Pad the data surface dimensions by the block dimensions, and put the result in compressed block dimension units
        surf_width = ((surf_width >> data_blk_width_log2) + ((surf_width & width_pad_mask) ? 1 : 0)) << data_blk_width_log2;
        surf_height = ((surf_height >> data_blk_height_log2) + ((surf_height & height_pad_mask) ? 1 : 0)) << data_blk_height_log2;

        // Tiled data, linear metadata
        if( max_mip > 0 ) {
            // we don't allow mipmapping on tiled data, with linear metadata
            // assert error
        }

        // Pad up epitch to data block width
        if( (epitch & width_pad_mask) != 0 ) {
            epitch = ((epitch >> data_blk_width_log2) + 1) << data_blk_width_log2;
        }
        // Take max of epitch and computed surf width
        if( epitch < surf_width ) {
            // assert error
        } else {
            surf_width = epitch;
        }
    }
}

void RB_MAP::get_mip_coord_nonlinear( int& x, int& y, int& z,
                                      int mip,
                                      int meta_blk_width_log2, int meta_blk_height_log2, int meta_blk_depth_log2,

                                      // Outputs
                                      int& surf_width, int& surf_height, int& surf_depth,

                                      int epitch, int max_mip, int data_type
                                    )
{
    bool is3d = (data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    int order;  // 0 = xmajor, 1 = ymajor, 2 = zmajor

    int mip_width  = surf_width;
    int mip_height = surf_height;
    int mip_depth  = (is3d) ? surf_depth : 1;

    // Divide surface w/h/d by block size, padding if needed
    surf_width  = (((surf_width  & ((1<<meta_blk_width_log2 )-1)) != 0) ? 1 : 0) + (surf_width  >> meta_blk_width_log2);
    surf_height = (((surf_height & ((1<<meta_blk_height_log2)-1)) != 0) ? 1 : 0) + (surf_height >> meta_blk_height_log2);
    surf_depth  = (((surf_depth  & ((1<<meta_blk_depth_log2 )-1)) != 0) ? 1 : 0) + (surf_depth  >> meta_blk_depth_log2);
    epitch      = (((epitch      & ((1<<meta_blk_width_log2 )-1)) != 0) ? 1 : 0) + (epitch      >> meta_blk_width_log2);

    if( max_mip > 0 ) {
        // Determine major order
        if( is3d && surf_depth > surf_width && surf_depth > surf_height ) {
            order = 2;  // Z major
        }
        else if( surf_width >= surf_height ) {
            order = 0;  // X major
        }
        else {
            order = 1;  // Y major
        }

        // Check if mip 0 is in the tail
        bool in_tail = (mip_width <= (1<<meta_blk_width_log2)) &&
            (mip_height <= (1<<(meta_blk_height_log2-1))) &&
            (!is3d || (mip_depth <= (1<<meta_blk_depth_log2)));
        // Pad the mip w/h/d, which is just the surf w/h/d times blk dim
        mip_width = surf_width << meta_blk_width_log2;
        mip_height = surf_height << meta_blk_height_log2;
        mip_depth = surf_depth << meta_blk_depth_log2;

        if( !in_tail ) {
            // Select the dimension that stores the mip chain, based on major order
            // Then pad it out to max(2, ceil(mip_dim/2))
            int& mip_dim = (order == 1) ? surf_width : surf_height;
            // in y-major, if height > 2 blocks, then we need extra padding;
            // in x or z major, it only occurs if width/depth is greater than 4 blocks
            // Height is special, since we can enter the mip tail when height is 1/2 block high
            int order_dim_limit = (order == 1) ? 2 : 4;
            int& order_dim = (order == 0) ? surf_width : ((order == 1) ? surf_height : surf_depth);
            if( mip_dim < 3 && order_dim > order_dim_limit && max_mip >= 3 ) mip_dim += 2;
            else mip_dim += (mip_dim/2) + (mip_dim&1);
        }

        int m;
        for( m=0; m<mip; m++ ) {
            if( in_tail ) {
                get_meta_miptail_coord( x, y, z, mip-m, meta_blk_width_log2, meta_blk_height_log2, meta_blk_depth_log2 );
                m = mip;  // break the loop
            } else {
                // Move either x, y, or z by the mip dimension based on which mip we're on and the order
                if(m>=3 || m&1) {
                    switch(order) {
                    case 0: x += mip_width; break;
                    case 1: y += mip_height; break;
                    case 2: z += mip_depth; break;
                    }
                } else {
                    switch(order) {
                    case 0: y += mip_height; break;
                    case 1: x += mip_width; break;
                    case 2: y += mip_height; break;
                    }
                }
                // Compute next mip's dimensions
                mip_width = (mip_width/2);
                mip_height = (mip_height/2);
                mip_depth = (mip_depth/2);
                // See if it's in the tail
                in_tail = (mip_width <= (1<<meta_blk_width_log2)) &&
                    (mip_height <= (1<<(meta_blk_height_log2-1))) &&
                    (!is3d || (mip_depth <= (1<<meta_blk_depth_log2)));
                // Pad out mip dimensions
                mip_width  = ((mip_width  >> meta_blk_width_log2)  + ((mip_width  & ((1<<meta_blk_width_log2) -1)) != 0)) << meta_blk_width_log2;
                mip_height = ((mip_height >> meta_blk_height_log2) + ((mip_height & ((1<<meta_blk_height_log2)-1)) != 0)) << meta_blk_height_log2;
                mip_depth  = ((mip_depth  >> meta_blk_depth_log2)  + ((mip_depth  & ((1<<meta_blk_depth_log2) -1)) != 0)) << meta_blk_depth_log2;
            }
        }
    } else {
        // Take max of epitch and computed surf width
        surf_width = (surf_width > epitch) ? surf_width : epitch;
    }

    // Multiply the surface dimension by block size
    surf_width = surf_width << meta_blk_width_log2;
    surf_height = surf_height << meta_blk_height_log2;
    surf_depth = surf_depth << meta_blk_depth_log2;

}

void
RB_MAP::get_meta_eq( CoordEq& metaaddr,
                     int max_mip, int num_ses_log2, int num_rbs_log2,
                     int &num_pipes_log2,
                     int block_size_log2, int bpp_log2, int num_samples_log2, int max_comp_frag_log2,
                     int pipe_interleave_log2,
                     int xmode,
                     int data_type,
                     int meta_alignment, bool meta_linear)
{
    // Metaaddressing
    Coordinate co;
    CoordEq cur_rbeq, pipe_equation, orig_pipe_equation;

    bool data_linear = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D_LINEAR );
    bool is_color = ( data_linear || data_type == DATA_COLOR2D || data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    //bool is3d = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    bool is_thick = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z );

    bool is_fmask = (data_type == DATA_FMASK);
    bool is_pipe_aligned = (meta_alignment == META_ALIGN_PIPE) || (meta_alignment == META_ALIGN_PIPE_RB);
    bool is_rb_aligned = (meta_alignment == META_ALIGN_RB) || (meta_alignment == META_ALIGN_PIPE_RB);

    bool is_mipmapped = (max_mip > 0) ? true : false;

    int pipe_mask = 0x0;
    int comp_frag_log2 = (is_color && (num_samples_log2 > max_comp_frag_log2)) ? max_comp_frag_log2 : num_samples_log2;

    int uncomp_frag_log2 = num_samples_log2 - comp_frag_log2;

    // Constraints on linear
    if ( data_linear ) {
        xmode = NONE;
        num_samples_log2 = 0;
        is_rb_aligned = false;
        meta_linear = true;
    }
    if( meta_linear && !data_linear ) {
        is_pipe_aligned = false;
    }

    // Min metablock size if thick is 64KB, otherwise 4KB
    int min_meta_block_size_log2 = (is_thick) ? 16 : 12;

    // metadata word size is 1/2 byte for cmask, 1 byte for color, and 4 bytes for z/stencil
    int metadata_word_size_log2 = (is_fmask) ? -1 : ((is_color) ? 0 : 2);

    int metadata_words_per_page_log2 = min_meta_block_size_log2 - metadata_word_size_log2;

    // Get the total # of RB's before modifying due to rb align
    int num_total_rbs_pre_rb_align_log2 = num_ses_log2 + num_rbs_log2;

    // Cap the pipe bits to block size
    int num_ses_data_log2 = num_ses_log2;
    cap_pipe( xmode, is_thick, num_ses_data_log2, bpp_log2,
                   num_samples_log2, pipe_interleave_log2, block_size_log2, num_pipes_log2 );

    // if not pipe aligned, set num_pipes_log2, num_ses_log2 to 0
    if( !is_pipe_aligned ) {
        num_pipes_log2 = 0;
        num_ses_data_log2 = 0;
    }

    // Get the correct data address and rb equation
    CoordEq dataaddr;
    Get_Data_Offset_Equation( dataaddr,
                              (meta_linear) ? DATA_COLOR1D : data_type,
                              bpp_log2, num_samples_log2, block_size_log2 );


    // if not rb aligned, set num_ses_log2/rbs_log2 to 0; note, this is done after generating the data equation
    if( !is_rb_aligned ) {
        num_ses_log2 = 0;
        num_rbs_log2 = 0;
    }

    // Get pipe and rb equations
    Get_Pipe_Equation( pipe_equation, dataaddr, pipe_interleave_log2,
                       num_pipes_log2, block_size_log2, num_samples_log2, xmode, data_type );

    CoordEq& this_rbeq = rb_equation[num_ses_log2][num_rbs_log2];

    num_pipes_log2 = pipe_equation.getsize();

    if( meta_linear ) {
        dataaddr.copy( metaaddr );
        if( data_linear ) {
            if( is_pipe_aligned ) {
                // Remove the pipe bits
                metaaddr.shift( -num_pipes_log2, pipe_interleave_log2 );
            }
            // Divide by comp block size, which for linear (which is always color) is 256 B
            metaaddr.shift( -8 );
            if( is_pipe_aligned ) {
                // Put pipe bits back in
                metaaddr.shift( num_pipes_log2, pipe_interleave_log2 );
                int i;
                for( i=0; i<num_pipes_log2; i++ ) {
                    pipe_equation[i].copyto(metaaddr[pipe_interleave_log2+i]);
                }
            }
        }
        metaaddr.shift( 1 );
        return;
    }

    int i, j, k, old_size, new_size;
    int num_total_rbs_log2 = num_ses_log2 + num_rbs_log2;

    // For non-color surfaces, compessed block size is always 8x8; for color, it's always a 256 bytes sized region
    int comp_blk_width_log2 = 3, comp_blk_height_log2 = 3, comp_blk_depth_log2 = 0;
    int comp_blk_size_log2 = 8;

    // For color surfaces, compute the comp block width, height, and depth
    // For non-color surfaces, compute the comp block size
    if( is_color ) {
        Get_Comp_Block_Screen_Space( dataaddr, comp_blk_size_log2, &comp_blk_width_log2, &comp_blk_height_log2, &comp_blk_depth_log2 );
        metadata_words_per_page_log2 -= num_samples_log2;  // factor out num fragments for color surfaces
    }
    else {
        comp_blk_size_log2 = 6 + num_samples_log2 + bpp_log2;
    }

    // Compute meta block width and height
    int num_comp_blks_per_meta_blk;
    if (num_pipes_log2==0 && num_ses_log2==0 && num_rbs_log2==0)  {
        num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }
    else {
        num_comp_blks_per_meta_blk = num_total_rbs_pre_rb_align_log2 + ((is_thick) ? 18 : 10);

        if( num_comp_blks_per_meta_blk + comp_blk_size_log2 > 27+bpp_log2)
            num_comp_blks_per_meta_blk = 27+bpp_log2 - comp_blk_size_log2;

        if( metadata_words_per_page_log2 > num_comp_blks_per_meta_blk )
            num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }

    int meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2;
    Get_Meta_Block_Screen_Space( num_comp_blks_per_meta_blk, is_thick, is_mipmapped, // mipmaps should be y-biased
                                 comp_blk_width_log2, comp_blk_height_log2, comp_blk_depth_log2,
                                 meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2 );

    // Make sure the metaaddr is cleared
    metaaddr.resize(0);
    metaaddr.resize(27);

    //------------------------------------------------------------------------------------------------------------------------
    // Use the growing square or growing cube order for thick as a starting point for the metadata address
    //------------------------------------------------------------------------------------------------------------------------
    if( is_thick ) {
        Coordinate cx( 'x', 0 );
        Coordinate cy( 'y', 0 );
        Coordinate cz( 'z', 0 );
        if(is_mipmapped) {
            metaaddr.mort3d( cy, cx, cz );
        } else {
            metaaddr.mort3d( cx, cy, cz );
        }
    }
    else {
        Coordinate cx( 'x', 0 );
        Coordinate cy( 'y', 0 );
        Coordinate cs;

        if(is_mipmapped) {
            metaaddr.mort2d( cy, cx, comp_frag_log2 );
        } else {
            metaaddr.mort2d( cx, cy, comp_frag_log2 );
        }

        //------------------------------------------------------------------------------------------------------------------------
        // Put the compressible fragments at the lsb
        // the uncompressible frags will be at the msb of the micro address
        //------------------------------------------------------------------------------------------------------------------------
        int s;
        for( s=0; s<comp_frag_log2; s++ ) {
            cs.set( 's', s );
            metaaddr[s].add(cs);
        }
    }

    // Keep a copy of the pipe and rb equations
    this_rbeq.copy( cur_rbeq );
    pipe_equation.copy( orig_pipe_equation );

    // filter out everything under the compressed block size
    co.set( 'x', comp_blk_width_log2 );
    metaaddr.Filter( '<', co, 0, 'x' );
    co.set( 'y', comp_blk_height_log2 );
    metaaddr.Filter( '<', co, 0, 'y' );
    co.set( 'z', comp_blk_depth_log2 );
    metaaddr.Filter( '<', co, 0, 'z' );
    // For non-color, filter out sample bits
    if( !is_color ) {
        co.set( 'x', 0 );
        metaaddr.Filter( '<', co, 0, 's' );
    }

    // filter out everything above the metablock size
    co.set( 'x', meta_block_width_log2-1 );
    metaaddr.Filter( '>', co, 0, 'x' );
    co.set( 'y', meta_block_height_log2-1 );
    metaaddr.Filter( '>', co, 0, 'y' );
    co.set( 'z', meta_block_depth_log2-1 );
    metaaddr.Filter( '>', co, 0, 'z' );

    // filter out everything above the metablock size for the channel bits
    co.set( 'x', meta_block_width_log2-1 );
    pipe_equation.Filter( '>', co, 0, 'x' );
    co.set( 'y', meta_block_height_log2-1 );
    pipe_equation.Filter( '>', co, 0, 'y' );
    co.set( 'z', meta_block_depth_log2-1 );
    pipe_equation.Filter( '>', co, 0, 'z' );

    // Make sure we still have the same number of channel bits
    if( pipe_equation.getsize() != static_cast<UINT_32>(num_pipes_log2) ) {
        // assert
    }

    // Loop through all channel and rb bits, and make sure these components exist in the metadata address
    for( i=0; i<num_pipes_log2; i++ ) {
        for( j=pipe_equation[i].getsize()-1; j>=0; j-- ) {
            if( !metaaddr.Exists( pipe_equation[i][j] ) ) {
                // assert
            }
        }
    }
    for( i=0; i<num_total_rbs_log2; i++ ) {
        for( j=cur_rbeq[i].getsize()-1; j>=0; j-- ) {
            if( !metaaddr.Exists( cur_rbeq[i][j] ) ) {
                // assert
            }
        }
    }

    // Loop through each rb id bit; if it is equal to any of the filtered channel bits, clear it
    int old_rb_bits_left = num_total_rbs_log2;
    for( i=0; i<num_total_rbs_log2; i++ ) {
        for(j=0; j<num_pipes_log2; j++ ) {
            if( cur_rbeq[i] == pipe_equation[j] ) {
                cur_rbeq[i].Clear();
                old_rb_bits_left--;
                // Mark which pipe bit caused the RB bit to be dropped
                pipe_mask |= (1 << j);
            }
        }
    }

    // Loop through each bit of the channel, get the smallest coordinate, and remove it from the metaaddr, and rb_equation
    for( i=0; i<num_pipes_log2; i++ ) {
        pipe_equation[i].getsmallest( co );

        old_size = metaaddr.getsize();
        metaaddr.Filter( '=', co );
        new_size = metaaddr.getsize();
        if( new_size != old_size-1 ) {
            // assert warning
        }
        pipe_equation.remove( co );
        for( j=0; j<num_total_rbs_log2; j++ ) {
            if( cur_rbeq[j].remove( co ) ) {
                // if we actually removed something from this bit, then add the remaining
                // channel bits, as these can be removed for this bit
                for( k=0; (unsigned)k<pipe_equation[i].getsize(); k++ ) {
                    if( pipe_equation[i][k] != co ) {
                        cur_rbeq[j].add( pipe_equation[i][k] );
                    }
                }
                // if the rb bit is still empty, then we have to mark all pipe bits as affecting the RB
                if( cur_rbeq[j].getsize() == 0 ) {
                    pipe_mask = (1 << num_pipes_log2) - 1;
                }
            }
        }
    }

    // Loop through the rb bits and see what remain; filter out the smallest coordinate if it remains
    int rb_bits_left = 0;
    for( i=0; i<num_total_rbs_log2; i++ ) {
        if( cur_rbeq[i].getsize() > 0 ) {
            rb_bits_left++;
            cur_rbeq[i].getsmallest( co );
            old_size = metaaddr.getsize();
            metaaddr.Filter( '=', co );
            new_size = metaaddr.getsize();
            if( new_size != old_size-1 ) {
                // assert warning
            }
            for( j=i+1; j<num_total_rbs_log2; j++ ) {
                if( cur_rbeq[j].remove( co ) ) {
                    // if we actually removed something from this bit, then add the remaining
                    // rb bits, as these can be removed for this bit
                    for( k=0; (unsigned)k<cur_rbeq[i].getsize(); k++ ) {
                        if( cur_rbeq[i][k] != co ) {
                            cur_rbeq[j].add( cur_rbeq[i][k] );
                        }
                    }
                }
            }
        }
    }

    // capture the size of the metaaddr
    i = metaaddr.getsize();
    // resize to 49 bits...make this a nibble address
    metaaddr.resize(49);
    // Concatenate the macro address above the current address
    for( j=0; i<49; i++, j++ ) {
        co.set( 'm', j );
        metaaddr[i].add( co );
    }

    // Multiply by meta element size (in nibbles)
    if( is_color ) {
        metaaddr.shift( 1 );  // Byte size element
    } else if( data_type == DATA_Z_STENCIL ) {
        metaaddr.shift( 3 );  // 4 Byte size elements
    }

    //------------------------------------------------------------------------------------------------------------------------
    // Note the pipe_interleave_log2+1 is because address is a nibble address
    // Shift up from pipe interleave number of channel and rb bits left, and uncompressed fragments
    //------------------------------------------------------------------------------------------------------------------------

    metaaddr.shift( num_pipes_log2 + rb_bits_left + uncomp_frag_log2,
                    pipe_interleave_log2+1 );

    // Put in the channel bits
    for( i=0; i<num_pipes_log2; i++ ) {
        orig_pipe_equation[i].copyto( metaaddr[pipe_interleave_log2+1 + i] );
    }

    // Put in remaining rb bits
    i = 0;
    for( j=0; j<rb_bits_left; i=(i+1) %  num_total_rbs_log2 ) {
        if( cur_rbeq[i].getsize() > 0 ) {
            rb_equation[num_ses_log2][num_rbs_log2][i].copyto( metaaddr[pipe_interleave_log2+1 + num_pipes_log2 + j] );
            // Mark any rb bit we add in to the rb mask
            j++;
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    // Put in the uncompressed fragment bits
    //------------------------------------------------------------------------------------------------------------------------
    for( i=0; i<uncomp_frag_log2; i++ ) {
        co.set( 's', comp_frag_log2+i );
        metaaddr[pipe_interleave_log2+1 + num_pipes_log2 + rb_bits_left + i].add( co );
    }


    //------------------------------------------------------------------------------------------------------------------------
    // Check that the metadata SE bits match the data address
    //------------------------------------------------------------------------------------------------------------------------
    for( i=0; i<num_ses_data_log2; i++ ) {
        if(num_total_rbs_log2-num_ses_data_log2+i >= 0){
            if( metaaddr[ pipe_interleave_log2+1 + num_pipes_log2-num_ses_data_log2 + i ] != dataaddr[ pipe_interleave_log2 + num_pipes_log2-num_ses_data_log2 + i ] ||
                metaaddr[ pipe_interleave_log2+1 + num_pipes_log2-num_ses_data_log2 + i ] != rb_equation[num_ses_log2][num_rbs_log2][num_total_rbs_log2-num_ses_data_log2+i]) {
                //FIXME: Removed to prevent logs from growing large in size // cout << "Warning: GPU bit " << i << " differs from data addr or RB equation on " << data_name << title << endl;
                //FIXME: Removed to prevent logs from growing large in size // cout << " Data: " << dataaddr[ pipe_interleave_log2 + num_pipes_log2-num_ses_data_log2 + i ] << endl;
                //FIXME: Removed to prevent logs from growing large in size // cout << "MData: " << metaaddr[ pipe_interleave_log2+1 + num_pipes_log2-num_ses_data_log2 + i ] << endl;
                //FIXME: Removed to prevent logs from growing large in size // cout << " RBeq: " << rb_equation[num_ses_log2][num_rbs_log2][num_total_rbs_log2-num_ses_data_log2+i] << endl;
                //FIXME: Removed to prevent logs from growing large in size // cout << " Pipe: " << orig_pipe_equation << endl;
                //FIXME: Removed to prevent logs from growing large in size // cout << "  DEq: " << dataaddr << endl;
            }
        }
    }
}

long
RB_MAP::get_meta_addr_calc( int x, int y, int z, int s,
                            long surf_base, int element_bytes_log2, int num_samples_log2, int max_comp_frag_log2,
                            long pitch, long slice,
                            int max_mip,

                            //int swizzle_mode,
                            int xmode, int pipe_xor, int block_size_log2,

                            /*int num_banks_log2,*/
                            int num_pipes_log2,
                            int pipe_interleave_log2,

                            int meta_alignment,
                            int dim_type,
                            int x_mip_org, int y_mip_org, int z_mip_org,

                            int num_ses_log2, int num_rbs_log2,
                            /*bool se_affinity_enable, */

                            int data_type,

                            int l2_metablk_w, int l2_metablk_h, int l2_metablk_d,
                            bool meta_linear
                          )
{
    int bpp_log2 = element_bytes_log2;
    int mip_base_x = x_mip_org;
    int mip_base_y = y_mip_org;
    int mip_base_z = z_mip_org;

    CoordEq metaaddr;

    //bool se_affinity_enable = false;
    //int max_pipe_bytes = std::max(1<<num_pipes_log2 * 1<<pipe_interleave_log2, 1024 * 1<<log2_element_bytes);
    //int max_banks_samples = std::max(1<<num_banks_log2, 1<<num_samples_log2);
    //int block_size_log2 = max(4096, max_pipe_bytes * max_bank_samples * 1<<num_ses_log2);

    bool data_linear = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D_LINEAR );
    bool is_color = ( data_linear || data_type == DATA_COLOR2D || data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    bool is_thick = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z );
    bool is_fmask = (data_type == DATA_FMASK);

    bool is_pipe_aligned = (meta_alignment == META_ALIGN_PIPE) || (meta_alignment == META_ALIGN_PIPE_RB);
    bool is_rb_aligned = (meta_alignment == META_ALIGN_RB) || (meta_alignment == META_ALIGN_PIPE_RB);

    if ( data_linear )
        meta_linear = true;

    if ( !data_linear && meta_linear)
        max_mip = 0;

    // Min metablock size if thick is 64KB, otherwise 4KB
    int min_meta_block_size_log2 = (is_thick) ? 16 : 12;

    // metadata word size is 1/2 byte for cmask, 1 byte for color, and 4 bytes for z/stencil
    int metadata_word_size_log2 = (is_fmask) ? -1 : ((is_color) ? 0 : 2);
    int metadata_words_per_page_log2 = min_meta_block_size_log2 - metadata_word_size_log2;

    int num_ses_data_log2 = num_ses_log2;
    int block_size_data_log2 = block_size_log2;
    int num_pipes_data_log2 = num_pipes_log2;

    //int num_banks_data_log2 = num_banks_log2;
    cap_pipe( xmode, is_thick, num_ses_data_log2, bpp_log2, num_samples_log2, pipe_interleave_log2, block_size_data_log2, num_pipes_data_log2/*, num_banks_data_log2 */);

    // Get the correct data address and rb equation
    CoordEq dataaddr;
    Get_Data_Offset_Equation( dataaddr, data_type, bpp_log2, num_samples_log2, block_size_data_log2 );

    get_meta_eq( metaaddr, max_mip, num_ses_log2, num_rbs_log2, num_pipes_log2, /*num_banks_log2,*/ block_size_log2,
         bpp_log2, num_samples_log2, max_comp_frag_log2, pipe_interleave_log2, xmode,
         data_type, meta_alignment, meta_linear);
     // For non-color surfaces, compessed block size is always 8x8; for color, it's always a 256 bytes sized region
    int comp_blk_width_log2 = 3, comp_blk_height_log2 = 3, comp_blk_depth_log2 = 0;
    int comp_blk_size_log2 = 8;

    if ( is_color ){
        Get_Comp_Block_Screen_Space( dataaddr, comp_blk_size_log2, &comp_blk_width_log2, &comp_blk_height_log2, &comp_blk_depth_log2 );
        metadata_words_per_page_log2 -= num_samples_log2;  // factor out num fragments for color surfaces
    }
    else {
        comp_blk_size_log2 = 6 + num_samples_log2 + bpp_log2;
    }

    // Compute meta block width and height
    int num_total_rbs_log2 = num_ses_log2 + num_rbs_log2;
    int num_comp_blks_per_meta_blk;
    if((!is_pipe_aligned || num_pipes_log2==0) && (!is_rb_aligned || (num_ses_log2==0 && num_rbs_log2==0)))  {
        num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }
    else {
        num_comp_blks_per_meta_blk = num_total_rbs_log2 + ((is_thick) ? 18 : 10);
        if( num_comp_blks_per_meta_blk + comp_blk_size_log2 > 27+bpp_log2) num_comp_blks_per_meta_blk = 27+bpp_log2 - comp_blk_size_log2;
        if( metadata_words_per_page_log2 > num_comp_blks_per_meta_blk )
            num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }

    int meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2;

    //@@todo kr missing meta_block_width*

    // Get the data block size
    int data_block_width_log2, data_block_height_log2, data_block_depth_log2;

    Get_Meta_Block_Screen_Space( block_size_log2 - comp_blk_size_log2,
                         is_thick, true,
                         comp_blk_width_log2, comp_blk_height_log2, comp_blk_depth_log2,
                         data_block_width_log2, data_block_height_log2, data_block_depth_log2 );

    meta_block_width_log2 = l2_metablk_w;
    meta_block_height_log2 = l2_metablk_h;
    meta_block_depth_log2 = l2_metablk_d;

    int meta_x = mip_base_x + x ;
    int meta_y = mip_base_y + y ;
    int meta_z = mip_base_z + z ;

    if( meta_linear ){
        if(!data_linear) {
            // Tiled data, linear metadata
            meta_x = meta_x >> comp_blk_width_log2;
            meta_y = meta_y >> comp_blk_height_log2;
            meta_z = meta_z >> comp_blk_depth_log2;
            pitch = pitch >> comp_blk_width_log2;
            slice = slice >> (comp_blk_width_log2 + comp_blk_height_log2);
        }
        else{
            meta_x = meta_x << bpp_log2;
            meta_y = meta_y << bpp_log2;
            meta_z = meta_z << bpp_log2;
        }
    }
    else{
        meta_x = meta_x >> meta_block_width_log2;
        meta_y = meta_y >> meta_block_height_log2;
        meta_z = meta_z >> meta_block_depth_log2;

        pitch = pitch >> meta_block_width_log2;
        slice = slice >> (meta_block_width_log2 + meta_block_height_log2);
    }

    long macroaddr = (long)meta_x + (long)meta_y*(long)pitch + (long)meta_z*(long)slice;

    int mip_tail_x, mip_tail_y, mip_tail_z;
    mip_tail_x = mip_base_x & ((1 << meta_block_width_log2 )-1);
    mip_tail_y = mip_base_y & ((1 << meta_block_height_log2)-1);
    mip_tail_z = mip_base_z & ((1 << meta_block_depth_log2)-1);

    int mip_x = x + mip_tail_x;
    int mip_y = y + mip_tail_y;
    int mip_z = z + mip_tail_z;

    // the pipe_interleave_log2+1 is because we are dealing with nibble addresses
    long pipe_xor_mask = (pipe_xor & ((1 << num_pipes_data_log2)-1)) << (pipe_interleave_log2+1);

    // shift surf_base to make it a nibble address
    long meta_offset_from_base_nibble_address = metaaddr.solve( mip_x, mip_y, mip_z, s, macroaddr );

    long address = (surf_base << 1) + (meta_offset_from_base_nibble_address ^ pipe_xor_mask);

    return address;
}

#if 0
long
RB_MAP::get_meta_addr( int x, int y, int z, int s, int mip,
                    int surf_width, int surf_height, int surf_depth, int lpitch,
                    long surf_base, int pipe_xor, int max_mip,
                    int num_ses_log2, int num_rbs_log2, int num_pipes_log2,
                    int block_size_log2, int bpp_log2, int num_samples_log2, int max_comp_frag_log2,
                    int pipe_interleave_log2, int xmode, int data_type, int meta_alignment, bool meta_linear)
{
    CoordEq metaaddr;

    bool data_linear = ( data_type == DATA_COLOR1D || data_type == DATA_COLOR2D_LINEAR );
    bool is_color = ( data_linear || data_type == DATA_COLOR2D || data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z || data_type == DATA_COLOR3D_D_NOT_USED );
    bool is_thick = ( data_type == DATA_COLOR3D_S || data_type == DATA_COLOR3D_Z );
    bool is_fmask = (data_type == DATA_FMASK);

    bool is_pipe_aligned = (meta_alignment == META_ALIGN_PIPE) || (meta_alignment == META_ALIGN_PIPE_RB);
    bool is_rb_aligned = (meta_alignment == META_ALIGN_RB) || (meta_alignment == META_ALIGN_PIPE_RB);

    bool is_mipmapped = (max_mip > 0) ? true : false;

    if( data_linear ) meta_linear = true;
    // Don't allow mipmapping on the tiled data, meta linear case
    // or if we have linear 2d/3d surface

    #ifdef ADDRESS__LPITCH_DISABLE__0
    if( (!data_linear && meta_linear) || (data_type == DATA_COLOR2D_LINEAR) ) max_mip = 0;
    #else
    if( !data_linear && meta_linear)  max_mip = 0;
    #endif

    // Min metablock size if thick is 64KB, otherwise 4KB
    int min_meta_block_size_log2 = (is_thick) ? 16 : 12;


    // metadata word size is 1/2 byte for cmask, 1 byte for color, and 4 bytes for z/stencil
    int metadata_word_size_log2 = (is_fmask) ? -1 : ((is_color) ? 0 : 2);
    int metadata_words_per_page_log2 = min_meta_block_size_log2 - metadata_word_size_log2;

    // Cap the pipe bits to block size
    int num_ses_data_log2 = num_ses_log2;
    int block_size_data_log2 = block_size_log2;
    int num_pipes_data_log2 = num_pipes_log2;

    cap_pipe( xmode, is_thick, num_ses_data_log2, bpp_log2, num_samples_log2, pipe_interleave_log2, block_size_data_log2, num_pipes_data_log2 );

    // Get the correct data address and rb equation
    CoordEq dataaddr;
    Get_Data_Offset_Equation( dataaddr, data_type, bpp_log2, num_samples_log2, block_size_data_log2 );

    get_meta_eq( metaaddr, max_mip, num_ses_log2, num_rbs_log2, num_pipes_log2, block_size_log2,
                 bpp_log2, num_samples_log2, max_comp_frag_log2, pipe_interleave_log2, xmode, data_type,
                 meta_alignment, meta_linear);

    // For non-color surfaces, compessed block size is always 8x8; for color, it's always a 256 bytes sized region
    int comp_blk_width_log2 = 3, comp_blk_height_log2 = 3, comp_blk_depth_log2 = 0;
    int comp_blk_size_log2 = 8;

    if ( is_color ) {
        Get_Comp_Block_Screen_Space( dataaddr, comp_blk_size_log2, &comp_blk_width_log2, &comp_blk_height_log2, &comp_blk_depth_log2 );
        metadata_words_per_page_log2 -= num_samples_log2;  // factor out num fragments for color surfaces
    } else {
        comp_blk_size_log2 = 6 + num_samples_log2 + bpp_log2;
    }

    // Compute meta block width and height
    int num_total_rbs_log2 = num_ses_log2 + num_rbs_log2;

    int num_comp_blks_per_meta_blk;
    if((!is_pipe_aligned || num_pipes_log2==0) && (!is_rb_aligned || (num_ses_log2==0 && num_rbs_log2==0)))  {
        num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }
    else {
        num_comp_blks_per_meta_blk = num_total_rbs_log2 + ((is_thick) ? 18 : 10);

        if( num_comp_blks_per_meta_blk + comp_blk_size_log2 > 27+bpp_log2) num_comp_blks_per_meta_blk = 27+bpp_log2 - comp_blk_size_log2;

        if( metadata_words_per_page_log2 > num_comp_blks_per_meta_blk )
            num_comp_blks_per_meta_blk = metadata_words_per_page_log2;
    }

    int meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2;


    Get_Meta_Block_Screen_Space( num_comp_blks_per_meta_blk, is_thick, is_mipmapped,
                                 comp_blk_width_log2, comp_blk_height_log2, comp_blk_depth_log2,
                                 meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2 );

    // Get the data block size
    int data_block_width_log2, data_block_height_log2, data_block_depth_log2;

    Get_Meta_Block_Screen_Space( block_size_log2 - comp_blk_size_log2, is_thick, true,
                                 comp_blk_width_log2, comp_blk_height_log2, comp_blk_depth_log2,
                                 data_block_width_log2, data_block_height_log2, data_block_depth_log2 );

    int meta_x, meta_y, meta_z;
    int meta_surf_width = surf_width;
    int meta_surf_height = surf_height;
    int meta_surf_depth = surf_depth;

    int mip_base_x=0, mip_base_y=0, mip_base_z=0;
    get_mip_coord( mip_base_x, mip_base_y, mip_base_z, mip,
                   meta_block_width_log2, meta_block_height_log2, meta_block_depth_log2,
                   data_block_width_log2, data_block_height_log2,
                   meta_surf_width, meta_surf_height, meta_surf_depth, lpitch, max_mip,
                   data_type, bpp_log2, meta_linear );

    meta_x = mip_base_x + x;
    meta_y = mip_base_y + y;
    meta_z = mip_base_z + z;

    if( meta_linear ) {
        if( !data_linear ) {
            // Tiled data, linear metadata
            meta_x = meta_x >> comp_blk_width_log2;
            meta_y = meta_y >> comp_blk_height_log2;
            meta_z = meta_z >> comp_blk_depth_log2;
            meta_surf_width = meta_surf_width >> comp_blk_width_log2;
            meta_surf_height = meta_surf_height >> comp_blk_height_log2;
        }
        else{
            meta_x = meta_x << bpp_log2;
            meta_y = meta_y << bpp_log2;
            meta_z = meta_z << bpp_log2;
        }
    } else {
        meta_x = meta_x >> meta_block_width_log2;
        meta_y = meta_y >> meta_block_height_log2;
        meta_z = meta_z >> meta_block_depth_log2;
        meta_surf_width = meta_surf_width >> meta_block_width_log2;
        meta_surf_height = meta_surf_height >> meta_block_height_log2;
    }

    long macroaddr = (long)meta_x + (long)meta_y*(long)meta_surf_width + (long)meta_z*(long)meta_surf_width*(long)meta_surf_height;

    int mip_tail_x, mip_tail_y, mip_tail_z;
    mip_tail_x = mip_base_x & ((1 << meta_block_width_log2 )-1);
    mip_tail_y = mip_base_y & ((1 << meta_block_height_log2)-1);
    mip_tail_z = mip_base_z & ((1 << meta_block_depth_log2)-1);

    int mip_x = x + mip_tail_x;
    int mip_y = y + mip_tail_y;
    int mip_z = z + mip_tail_z;

    // the pipe_interleave_log2+1 is because we are dealing with nibble addresses
    long pipe_xor_mask = (pipe_xor & ((1 << num_pipes_data_log2)-1)) << (pipe_interleave_log2+1);

    // shift surf_base to make it a nibble address
    long address = (surf_base << 1) + (metaaddr.solve( mip_x, mip_y, mip_z, s, macroaddr ) ^ pipe_xor_mask);

    return address;
}
#endif

void
RB_MAP::Initialize()
{
    int num_se_log2, num_rb_per_se_log2;
    for( num_se_log2=0; num_se_log2<5; num_se_log2++ ) {
        for( num_rb_per_se_log2=0; num_rb_per_se_log2<3; num_rb_per_se_log2++ ) {
            Get_RB_Equation( rb_equation[num_se_log2][num_rb_per_se_log2], num_se_log2, num_rb_per_se_log2 );
        }
    }

    int pix_size_log2, num_samples_log2;
    for( pix_size_log2=0; pix_size_log2<4; pix_size_log2++ ) {
        for( num_samples_log2=0; num_samples_log2<4; num_samples_log2++ ) {
            Get_Data_Offset_Equation( zaddr[pix_size_log2][num_samples_log2], DATA_Z_STENCIL, pix_size_log2, num_samples_log2, 16 );
        }
    }

    for( pix_size_log2=0; pix_size_log2<5; pix_size_log2++ ) {
        for( num_samples_log2=0; num_samples_log2<4; num_samples_log2++ ) {
            Get_Data_Offset_Equation( caddr[pix_size_log2][num_samples_log2], DATA_COLOR2D, pix_size_log2, num_samples_log2, 16 );
        }
    }

    for( pix_size_log2=0; pix_size_log2<5; pix_size_log2++ ) {
        Get_Data_Offset_Equation( c3addr[pix_size_log2][0], DATA_COLOR3D_S, pix_size_log2, 0, 16 );
        Get_Data_Offset_Equation( c3addr[pix_size_log2][1], DATA_COLOR3D_Z, pix_size_log2, 0, 16 );
    }
}

