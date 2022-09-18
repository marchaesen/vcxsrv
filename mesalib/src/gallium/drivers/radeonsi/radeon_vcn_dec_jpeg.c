/**************************************************************************
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "pipe/p_video_codec.h"
#include "radeon_vcn_dec.h"
#include "radeon_video.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"

#include <assert.h>
#include <stdio.h>

static struct pb_buffer *radeon_jpeg_get_decode_param(struct radeon_decoder *dec,
                                                      struct pipe_video_buffer *target,
                                                      struct pipe_picture_desc *picture)
{
   struct si_texture *luma = (struct si_texture *)((struct vl_video_buffer *)target)->resources[0];
   struct si_texture *chroma =
      (struct si_texture *)((struct vl_video_buffer *)target)->resources[1];

   dec->jpg.bsd_size = align(dec->bs_size, 128);
   dec->jpg.dt_luma_top_offset = luma->surface.u.gfx9.surf_offset;
   if (target->buffer_format == PIPE_FORMAT_NV12)
      dec->jpg.dt_chroma_top_offset = chroma->surface.u.gfx9.surf_offset;
   dec->jpg.dt_pitch = luma->surface.u.gfx9.surf_pitch * luma->surface.blk_w;
   dec->jpg.dt_uv_pitch = dec->jpg.dt_pitch / 2;

   return luma->buffer.buf;
}

/* add a new set register command to the IB */
static void set_reg_jpeg(struct radeon_decoder *dec, unsigned reg, unsigned cond, unsigned type,
                         uint32_t val)
{
   radeon_emit(&dec->jcs[dec->cb_idx], RDECODE_PKTJ(reg, cond, type));
   radeon_emit(&dec->jcs[dec->cb_idx], val);
}

/* send a bitstream buffer command */
static void send_cmd_bitstream(struct radeon_decoder *dec, struct pb_buffer *buf, uint32_t off,
                               unsigned usage, enum radeon_bo_domain domain)
{
   uint64_t addr;

   // jpeg soft reset
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 1);

   // ensuring the Reset is asserted in SCLK domain
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C2);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0x01400200);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 9));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

   // wait mem
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 0);

   // ensuring the Reset is de-asserted in SCLK domain
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (0 << 9));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

   dec->ws->cs_add_buffer(&dec->jcs[dec->cb_idx], buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   addr = dec->ws->buffer_get_virtual_address(buf);
   addr = addr + off;

   // set UVD_LMI_JPEG_READ_64BIT_BAR_LOW/HIGH based on bitstream buffer address
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_HIGH), COND0, TYPE0,
                (addr >> 32));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_LOW), COND0, TYPE0, addr);

   // set jpeg_rb_base
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_RB_BASE), COND0, TYPE0, 0);

   // set jpeg_rb_base
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_RB_SIZE), COND0, TYPE0, 0xFFFFFFF0);

   // set jpeg_rb_wptr
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_RB_WPTR), COND0, TYPE0, (dec->jpg.bsd_size >> 2));
}

/* send a target buffer command */
static void send_cmd_target(struct radeon_decoder *dec, struct pb_buffer *buf, uint32_t off,
                            unsigned usage, enum radeon_bo_domain domain)
{
   uint64_t addr;

   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_PITCH), COND0, TYPE0, (dec->jpg.dt_pitch >> 4));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_UV_PITCH), COND0, TYPE0,
                ((dec->jpg.dt_uv_pitch * 2) >> 4));

   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_TILING_CTRL), COND0, TYPE0, 0);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_UV_TILING_CTRL), COND0, TYPE0, 0);

   dec->ws->cs_add_buffer(&dec->jcs[dec->cb_idx], buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   addr = dec->ws->buffer_get_virtual_address(buf);
   addr = addr + off;

   // set UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW/HIGH based on target buffer address
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH), COND0, TYPE0,
                (addr >> 32));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW), COND0, TYPE0, addr);

   // set output buffer data address
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_INDEX), COND0, TYPE0, 0);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_DATA), COND0, TYPE0, dec->jpg.dt_luma_top_offset);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_INDEX), COND0, TYPE0, 1);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_DATA), COND0, TYPE0, dec->jpg.dt_chroma_top_offset);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_TIER_CNTL2), COND0, TYPE3, 0);

   // set output buffer read pointer
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_RPTR), COND0, TYPE0, 0);

   // enable error interrupts
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_INT_EN), COND0, TYPE0, 0xFFFFFFFE);

   // start engine command
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 0x6);

   // wait for job completion, wait for job JBSI fetch done
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (dec->jpg.bsd_size >> 2));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C2);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0x01400200);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_RB_RPTR), COND0, TYPE3, 0xFFFFFFFF);

   // wait for job jpeg outbuf idle
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0xFFFFFFFF);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_WPTR), COND0, TYPE3, 0x00000001);

   // stop engine
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 0x4);

   // asserting jpeg lmi drop
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x0005);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 23 | 1 << 0));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE1, 0);

   // asserting jpeg reset
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 1);

   // ensure reset is asserted in sclk domain
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 9));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

   // de-assert jpeg reset
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 0);

   // ensure reset is de-asserted in sclk domain
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (0 << 9));
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

   // de-asserting jpeg lmi drop
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x0005);
   set_reg_jpeg(dec, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0);
}

/* send a bitstream buffer command */
static void send_cmd_bitstream_direct(struct radeon_decoder *dec, struct pb_buffer *buf,
                                      uint32_t off, unsigned usage,
                                      enum radeon_bo_domain domain)
{
   uint64_t addr;

   // jpeg soft reset
   set_reg_jpeg(dec, vcnipUVD_JPEG_DEC_SOFT_RST, COND0, TYPE0, 1);

   // ensuring the Reset is asserted in SCLK domain
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_COND_RD_TIMER, COND0, TYPE0, 0x01400200);
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_REF_DATA, COND0, TYPE0, (0x1 << 0x10));
   set_reg_jpeg(dec, vcnipUVD_JPEG_DEC_SOFT_RST, COND3, TYPE3, (0x1 << 0x10));

   // wait mem
   set_reg_jpeg(dec, vcnipUVD_JPEG_DEC_SOFT_RST, COND0, TYPE0, 0);

   // ensuring the Reset is de-asserted in SCLK domain
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_REF_DATA, COND0, TYPE0, (0 << 0x10));
   set_reg_jpeg(dec, vcnipUVD_JPEG_DEC_SOFT_RST, COND3, TYPE3, (0x1 << 0x10));

   dec->ws->cs_add_buffer(&dec->jcs[dec->cb_idx], buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   addr = dec->ws->buffer_get_virtual_address(buf);
   addr = addr + off;

   // set UVD_LMI_JPEG_READ_64BIT_BAR_LOW/HIGH based on bitstream buffer address
   set_reg_jpeg(dec, vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH, COND0, TYPE0, (addr >> 32));
   set_reg_jpeg(dec, vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW, COND0, TYPE0, addr);

   // set jpeg_rb_base
   set_reg_jpeg(dec, vcnipUVD_JPEG_RB_BASE, COND0, TYPE0, 0);

   // set jpeg_rb_base
   set_reg_jpeg(dec, vcnipUVD_JPEG_RB_SIZE, COND0, TYPE0, 0xFFFFFFF0);

   // set jpeg_rb_wptr
   set_reg_jpeg(dec, vcnipUVD_JPEG_RB_WPTR, COND0, TYPE0, (dec->jpg.bsd_size >> 2));
}

/* send a target buffer command */
static void send_cmd_target_direct(struct radeon_decoder *dec, struct pb_buffer *buf, uint32_t off,
                                   unsigned usage, enum radeon_bo_domain domain)
{
   uint64_t addr;

   set_reg_jpeg(dec, vcnipUVD_JPEG_PITCH, COND0, TYPE0, (dec->jpg.dt_pitch >> 4));
   set_reg_jpeg(dec, vcnipUVD_JPEG_UV_PITCH, COND0, TYPE0, ((dec->jpg.dt_uv_pitch * 2) >> 4));

   set_reg_jpeg(dec, vcnipJPEG_DEC_ADDR_MODE, COND0, TYPE0, 0);
   set_reg_jpeg(dec, vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE, COND0, TYPE0, 0);
   set_reg_jpeg(dec, vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE, COND0, TYPE0, 0);

   dec->ws->cs_add_buffer(&dec->jcs[dec->cb_idx], buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   addr = dec->ws->buffer_get_virtual_address(buf);
   addr = addr + off;

   // set UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW/HIGH based on target buffer address
   set_reg_jpeg(dec, vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH, COND0, TYPE0, (addr >> 32));
   set_reg_jpeg(dec, vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW, COND0, TYPE0, addr);

   // set output buffer data address
   set_reg_jpeg(dec, vcnipUVD_JPEG_INDEX, COND0, TYPE0, 0);
   set_reg_jpeg(dec, vcnipUVD_JPEG_DATA, COND0, TYPE0, dec->jpg.dt_luma_top_offset);
   set_reg_jpeg(dec, vcnipUVD_JPEG_INDEX, COND0, TYPE0, 1);
   set_reg_jpeg(dec, vcnipUVD_JPEG_DATA, COND0, TYPE0, dec->jpg.dt_chroma_top_offset);
   set_reg_jpeg(dec, vcnipUVD_JPEG_TIER_CNTL2, COND0, 0, 0);

   // set output buffer read pointer
   set_reg_jpeg(dec, vcnipUVD_JPEG_OUTBUF_RPTR, COND0, TYPE0, 0);
   set_reg_jpeg(dec, vcnipUVD_JPEG_OUTBUF_CNTL, COND0, TYPE0,
                ((0x00001587 & (~0x00000180L)) | (0x1 << 0x7) | (0x1 << 0x6)));

   // enable error interrupts
   set_reg_jpeg(dec, vcnipUVD_JPEG_INT_EN, COND0, TYPE0, 0xFFFFFFFE);

   // start engine command
   set_reg_jpeg(dec, vcnipUVD_JPEG_CNTL, COND0, TYPE0, 0x6);

   // wait for job completion, wait for job JBSI fetch done
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_REF_DATA, COND0, TYPE0, (dec->jpg.bsd_size >> 2));
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_COND_RD_TIMER, COND0, TYPE0, 0x01400200);
   set_reg_jpeg(dec, vcnipUVD_JPEG_RB_RPTR, COND3, TYPE3, 0xFFFFFFFF);

   // wait for job jpeg outbuf idle
   set_reg_jpeg(dec, vcnipUVD_JRBC_IB_REF_DATA, COND0, TYPE0, 0xFFFFFFFF);
   set_reg_jpeg(dec, vcnipUVD_JPEG_OUTBUF_WPTR, COND3, TYPE3, 0x00000001);

   // stop engine
   set_reg_jpeg(dec, vcnipUVD_JPEG_CNTL, COND0, TYPE0, 0x4);
}

/**
 * send cmd for vcn jpeg
 */
void send_cmd_jpeg(struct radeon_decoder *dec, struct pipe_video_buffer *target,
                   struct pipe_picture_desc *picture)
{
   struct pb_buffer *dt;
   struct rvid_buffer *bs_buf;

   bs_buf = &dec->bs_buffers[dec->cur_buffer];

   memset(dec->bs_ptr, 0, align(dec->bs_size, 128) - dec->bs_size);
   dec->ws->buffer_unmap(dec->ws, bs_buf->res->buf);
   dec->bs_ptr = NULL;

   dt = radeon_jpeg_get_decode_param(dec, target, picture);

   if (dec->jpg.direct_reg == true) {
      send_cmd_bitstream_direct(dec, bs_buf->res->buf, 0, RADEON_USAGE_READ, RADEON_DOMAIN_GTT);
      send_cmd_target_direct(dec, dt, 0, RADEON_USAGE_WRITE, RADEON_DOMAIN_VRAM);
   } else {
      send_cmd_bitstream(dec, bs_buf->res->buf, 0, RADEON_USAGE_READ, RADEON_DOMAIN_GTT);
      send_cmd_target(dec, dt, 0, RADEON_USAGE_WRITE, RADEON_DOMAIN_VRAM);
   }
}
