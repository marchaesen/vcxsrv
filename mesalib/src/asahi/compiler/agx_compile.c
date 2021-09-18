/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2020 Collabora Ltd.
 * Copyright © 2016 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "main/mtypes.h"
#include "compiler/nir_types.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_debug.h"
#include "util/fast_idiv_by_const.h"
#include "agx_compile.h"
#include "agx_compiler.h"
#include "agx_builder.h"

static const struct debug_named_value agx_debug_options[] = {
   {"msgs",      AGX_DBG_MSGS,		"Print debug messages"},
   {"shaders",   AGX_DBG_SHADERS,	"Dump shaders in NIR and AIR"},
   {"shaderdb",  AGX_DBG_SHADERDB,	"Print statistics"},
   {"verbose",   AGX_DBG_VERBOSE,	"Disassemble verbosely"},
   {"internal",  AGX_DBG_INTERNAL,	"Dump even internal shaders"},
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(agx_debug, "AGX_MESA_DEBUG", agx_debug_options, 0)

int agx_debug = 0;

#define DBG(fmt, ...) \
   do { if (agx_debug & AGX_DBG_MSGS) \
      fprintf(stderr, "%s:%d: "fmt, \
            __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

static void
agx_block_add_successor(agx_block *block, agx_block *successor)
{
   assert(block != NULL && successor != NULL);

   /* Cull impossible edges */
   if (block->unconditional_jumps)
      return;

   for (unsigned i = 0; i < ARRAY_SIZE(block->successors); ++i) {
      if (block->successors[i]) {
         if (block->successors[i] == successor)
            return;
         else
            continue;
      }

      block->successors[i] = successor;
      _mesa_set_add(successor->predecessors, block);
      return;
   }

   unreachable("Too many successors");
}

static void
agx_emit_load_const(agx_builder *b, nir_load_const_instr *instr)
{
   /* Ensure we've been scalarized and bit size lowered */
   unsigned bit_size = instr->def.bit_size;
   assert(instr->def.num_components == 1);
   assert(bit_size == 1 || bit_size == 16 || bit_size == 32);

   /* Emit move, later passes can inline/push if useful */
   agx_mov_imm_to(b,
                  agx_get_index(instr->def.index, agx_size_for_bits(bit_size)),
                  nir_const_value_as_uint(instr->value[0], bit_size));
}

/* Emit code dividing P by Q */
static agx_index
agx_udiv_const(agx_builder *b, agx_index P, uint32_t Q)
{
   /* P / 1 = P */
   if (Q == 1) {
      return P;
   }

   /* P / UINT32_MAX = 0, unless P = UINT32_MAX when it's one */
   if (Q == UINT32_MAX) {
      agx_index max = agx_mov_imm(b, 32, UINT32_MAX);
      agx_index one = agx_mov_imm(b, 32, 1);
      return agx_icmpsel(b, P, max, one, agx_zero(), AGX_ICOND_UEQ);
   }

   /* P / 2^N = P >> N */
   if (util_is_power_of_two_or_zero(Q)) {
      return agx_ushr(b, P, agx_mov_imm(b, 32, util_logbase2(Q)));
   }

   /* Fall back on multiplication by a magic number */
   struct util_fast_udiv_info info = util_compute_fast_udiv_info(Q, 32, 32);
   agx_index preshift = agx_mov_imm(b, 32, info.pre_shift);
   agx_index increment = agx_mov_imm(b, 32, info.increment);
   agx_index postshift = agx_mov_imm(b, 32, info.post_shift);
   agx_index multiplier = agx_mov_imm(b, 32, info.multiplier);
   agx_index multiplied = agx_temp(b->shader, AGX_SIZE_64);
   agx_index n = P;

   if (info.pre_shift != 0) n = agx_ushr(b, n, preshift);
   if (info.increment != 0) n = agx_iadd(b, n, increment, 0);

   /* 64-bit multiplication, zero extending 32-bit x 32-bit, get the top word */
   agx_imad_to(b, multiplied, agx_abs(n), agx_abs(multiplier), agx_zero(), 0);
   n = agx_temp(b->shader, AGX_SIZE_32);
   agx_p_extract_to(b, n, multiplied, 1);

   if (info.post_shift != 0) n = agx_ushr(b, n, postshift);

   return n;
}

/* AGX appears to lack support for vertex attributes. Lower to global loads. */
static agx_instr *
agx_emit_load_attr(agx_builder *b, nir_intrinsic_instr *instr)
{
   nir_src *offset_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_src) && "no attribute indirects");
   unsigned index = nir_intrinsic_base(instr) +
                    nir_src_as_uint(*offset_src);

   struct agx_shader_key *key = b->shader->key;
   struct agx_attribute attrib = key->vs.attributes[index];

   /* address = base + (stride * vertex_id) + src_offset */
   unsigned buf = attrib.buf;
   unsigned stride = key->vs.vbuf_strides[buf];
   unsigned shift = agx_format_shift(attrib.format);

   agx_index shifted_stride = agx_mov_imm(b, 32, stride >> shift);
   agx_index src_offset = agx_mov_imm(b, 32, attrib.src_offset);

   agx_index vertex_id = agx_register(10, AGX_SIZE_32);
   agx_index instance_id = agx_register(12, AGX_SIZE_32);

   /* A nonzero divisor requires dividing the instance ID. A zero divisor
    * specifies per-instance data. */
   agx_index element_id = (attrib.divisor == 0) ? vertex_id :
                          agx_udiv_const(b, instance_id, attrib.divisor);

   agx_index offset = agx_imad(b, element_id, shifted_stride, src_offset, 0);

   /* Each VBO has a 64-bit = 4 x 16-bit address, lookup the base address as a sysval */
   unsigned num_vbos = key->vs.num_vbufs;
   unsigned base_length = (num_vbos * 4);
   agx_index base = agx_indexed_sysval(b->shader,
                                       AGX_PUSH_VBO_BASES, AGX_SIZE_64, buf * 4, base_length);

   /* Load the data */
   assert(instr->num_components <= 4);

   bool pad = ((attrib.nr_comps_minus_1 + 1) < instr->num_components);
   agx_index real_dest = agx_dest_index(&instr->dest);
   agx_index dest = pad ? agx_temp(b->shader, AGX_SIZE_32) : real_dest;

   agx_device_load_to(b, dest, base, offset, attrib.format,
                      BITFIELD_MASK(attrib.nr_comps_minus_1 + 1), 0);

   agx_wait(b, 0);

   if (pad) {
      agx_index one = agx_mov_imm(b, 32, fui(1.0));
      agx_index zero = agx_mov_imm(b, 32, 0);
      agx_index channels[4] = { zero, zero, zero, one };
      for (unsigned i = 0; i < (attrib.nr_comps_minus_1 + 1); ++i)
         channels[i] = agx_p_extract(b, dest, i);
      for (unsigned i = instr->num_components; i < 4; ++i)
         channels[i] = agx_null();
      agx_p_combine_to(b, real_dest, channels[0], channels[1], channels[2], channels[3]);
   }

   return NULL;
}

static agx_instr *
agx_emit_load_vary_flat(agx_builder *b, nir_intrinsic_instr *instr)
{
   unsigned components = instr->num_components;
   assert(components >= 1 && components <= 4);

   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");
   unsigned imm_index = b->shader->varyings[nir_intrinsic_base(instr)];
   imm_index += nir_src_as_uint(*offset);

   agx_index chan[4] = { agx_null() };

   for (unsigned i = 0; i < components; ++i) {
      /* vec3 for each vertex, unknown what first 2 channels are for */
      agx_index values = agx_ld_vary_flat(b, agx_immediate(imm_index + i), 1);
      chan[i] = agx_p_extract(b, values, 2);
   }

   return agx_p_combine_to(b, agx_dest_index(&instr->dest),
         chan[0], chan[1], chan[2], chan[3]);
}

static agx_instr *
agx_emit_load_vary(agx_builder *b, nir_intrinsic_instr *instr)
{
   ASSERTED unsigned components = instr->num_components;
   ASSERTED nir_intrinsic_instr *parent = nir_src_as_intrinsic(instr->src[0]);

   assert(components >= 1 && components <= 4);
   assert(parent);

   /* TODO: Interpolation modes */
   assert(parent->intrinsic == nir_intrinsic_load_barycentric_pixel);

   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");
   unsigned imm_index = b->shader->varyings[nir_intrinsic_base(instr)];
   imm_index += nir_src_as_uint(*offset) * 4;

   return agx_ld_vary_to(b, agx_dest_index(&instr->dest),
         agx_immediate(imm_index), components, true);
}

static agx_instr *
agx_emit_store_vary(agx_builder *b, nir_intrinsic_instr *instr)
{
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "todo: indirects");
   unsigned imm_index = b->shader->varyings[nir_intrinsic_base(instr)];
   imm_index += nir_intrinsic_component(instr);
   imm_index += nir_src_as_uint(*offset);

   /* nir_lower_io_to_scalar */
   assert(nir_intrinsic_write_mask(instr) == 0x1);

   return agx_st_vary(b,
               agx_immediate(imm_index),
               agx_src_index(&instr->src[0]));
}

static agx_instr *
agx_emit_fragment_out(agx_builder *b, nir_intrinsic_instr *instr)
{
   const nir_variable *var =
      nir_find_variable_with_driver_location(b->shader->nir,
            nir_var_shader_out, nir_intrinsic_base(instr));
   assert(var);

   unsigned loc = var->data.location;
   assert(var->data.index == 0 && "todo: dual-source blending");
   assert(loc == FRAG_RESULT_DATA0 && "todo: MRT");
   unsigned rt = (loc - FRAG_RESULT_DATA0);

   /* TODO: Reverse-engineer interactions with MRT */
   if (b->shader->nir->info.internal) {
      /* clear */
   } else if (b->shader->did_writeout) {
	   agx_writeout(b, 0x0004);
   } else {
	   agx_writeout(b, 0xC200);
	   agx_writeout(b, 0x000C);
   }

   b->shader->did_writeout = true;
   return agx_st_tile(b, agx_src_index(&instr->src[0]),
             b->shader->key->fs.tib_formats[rt]);
}

static agx_instr *
agx_emit_load_tile(agx_builder *b, nir_intrinsic_instr *instr)
{
   const nir_variable *var =
      nir_find_variable_with_driver_location(b->shader->nir,
            nir_var_shader_out, nir_intrinsic_base(instr));
   assert(var);

   unsigned loc = var->data.location;
   assert(var->data.index == 0 && "todo: dual-source blending");
   assert(loc == FRAG_RESULT_DATA0 && "todo: MRT");
   unsigned rt = (loc - FRAG_RESULT_DATA0);

   /* TODO: Reverse-engineer interactions with MRT */
   agx_writeout(b, 0xC200);
   agx_writeout(b, 0x0008);
   b->shader->did_writeout = true;
   b->shader->out->reads_tib = true;

   return agx_ld_tile_to(b, agx_dest_index(&instr->dest),
         b->shader->key->fs.tib_formats[rt]);
}

static enum agx_format
agx_format_for_bits(unsigned bits)
{
   switch (bits) {
   case 8: return AGX_FORMAT_I8;
   case 16: return AGX_FORMAT_I16;
   case 32: return AGX_FORMAT_I32;
   default: unreachable("Invalid bit size for load/store");
   }
}

static agx_instr *
agx_emit_load_ubo(agx_builder *b, nir_intrinsic_instr *instr)
{
   bool kernel_input = (instr->intrinsic == nir_intrinsic_load_kernel_input);
   nir_src *offset = nir_get_io_offset_src(instr);

   if (!kernel_input && !nir_src_is_const(instr->src[0]))
      unreachable("todo: indirect UBO access");

   /* Constant offsets for device_load are 16-bit */
   bool offset_is_const = nir_src_is_const(*offset);
   assert(offset_is_const && "todo: indirect UBO access");
   int32_t const_offset = offset_is_const ? nir_src_as_int(*offset) : 0;

   /* Offsets are shifted by the type size, so divide that out */
   unsigned bytes = nir_dest_bit_size(instr->dest) / 8;
   assert((const_offset & (bytes - 1)) == 0);
   const_offset = const_offset / bytes;
   int16_t const_as_16 = const_offset;

   /* UBO blocks are specified (kernel inputs are always 0) */
   uint32_t block = kernel_input ? 0 : nir_src_as_uint(instr->src[0]);

   /* Each UBO has a 64-bit = 4 x 16-bit address */
   unsigned num_ubos = b->shader->nir->info.num_ubos;
   unsigned base_length = (num_ubos * 4);
   unsigned index = block * 4; /* 16 bit units */

   /* Lookup the base address (TODO: indirection) */
   agx_index base = agx_indexed_sysval(b->shader,
                                       AGX_PUSH_UBO_BASES, AGX_SIZE_64,
                                       index, base_length);

   /* Load the data */
   assert(instr->num_components <= 4);

   agx_device_load_to(b, agx_dest_index(&instr->dest),
                      base,
                      (offset_is_const && (const_offset == const_as_16)) ?
                      agx_immediate(const_as_16) : agx_mov_imm(b, 32, const_offset),
                      agx_format_for_bits(nir_dest_bit_size(instr->dest)),
                      BITFIELD_MASK(instr->num_components), 0);

   return agx_wait(b, 0);
}

static agx_instr *
agx_emit_load_frag_coord(agx_builder *b, nir_intrinsic_instr *instr)
{
   agx_index xy[2];

   for (unsigned i = 0; i < 2; ++i) {
      xy[i] = agx_fadd(b, agx_convert(b, agx_immediate(AGX_CONVERT_U32_TO_F),
               agx_get_sr(b, 32, AGX_SR_THREAD_POSITION_IN_GRID_X + i),
               AGX_ROUND_RTE), agx_immediate_f(0.5f));
   }

   /* Ordering by the ABI */
   agx_index z = agx_ld_vary(b, agx_immediate(1), 1, false);
   agx_index w = agx_ld_vary(b, agx_immediate(0), 1, false);

   return agx_p_combine_to(b, agx_dest_index(&instr->dest),
         xy[0], xy[1], z, w);
}

static agx_instr *
agx_blend_const(agx_builder *b, agx_index dst, unsigned comp)
{
     agx_index val = agx_indexed_sysval(b->shader,
           AGX_PUSH_BLEND_CONST, AGX_SIZE_32, comp * 2, 4 * 2);

     return agx_mov_to(b, dst, val);
}

static agx_instr *
agx_emit_intrinsic(agx_builder *b, nir_intrinsic_instr *instr)
{
  agx_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest ?
     agx_dest_index(&instr->dest) : agx_null();
  gl_shader_stage stage = b->shader->stage;

  switch (instr->intrinsic) {
  case nir_intrinsic_load_barycentric_pixel:
  case nir_intrinsic_load_barycentric_centroid:
  case nir_intrinsic_load_barycentric_sample:
  case nir_intrinsic_load_barycentric_at_sample:
  case nir_intrinsic_load_barycentric_at_offset:
     /* handled later via load_vary */
     return NULL;
  case nir_intrinsic_load_interpolated_input:
     assert(stage == MESA_SHADER_FRAGMENT);
     return agx_emit_load_vary(b, instr);

  case nir_intrinsic_load_input:
     if (stage == MESA_SHADER_FRAGMENT)
        return agx_emit_load_vary_flat(b, instr);
     else if (stage == MESA_SHADER_VERTEX)
        return agx_emit_load_attr(b, instr);
     else
        unreachable("Unsupported shader stage");

  case nir_intrinsic_store_output:
     if (stage == MESA_SHADER_FRAGMENT)
        return agx_emit_fragment_out(b, instr);
     else if (stage == MESA_SHADER_VERTEX)
        return agx_emit_store_vary(b, instr);
     else
        unreachable("Unsupported shader stage");

  case nir_intrinsic_load_output:
     assert(stage == MESA_SHADER_FRAGMENT);
     return agx_emit_load_tile(b, instr);

  case nir_intrinsic_load_ubo:
  case nir_intrinsic_load_kernel_input:
     return agx_emit_load_ubo(b, instr);

  case nir_intrinsic_load_frag_coord:
     return agx_emit_load_frag_coord(b, instr);

  case nir_intrinsic_load_back_face_agx:
     return agx_get_sr_to(b, dst, AGX_SR_BACKFACING);

  case nir_intrinsic_load_vertex_id:
     return agx_mov_to(b, dst, agx_abs(agx_register(10, AGX_SIZE_32)));

  case nir_intrinsic_load_instance_id:
     return agx_mov_to(b, dst, agx_abs(agx_register(12, AGX_SIZE_32)));

  case nir_intrinsic_load_blend_const_color_r_float: return agx_blend_const(b, dst, 0);
  case nir_intrinsic_load_blend_const_color_g_float: return agx_blend_const(b, dst, 1);
  case nir_intrinsic_load_blend_const_color_b_float: return agx_blend_const(b, dst, 2);
  case nir_intrinsic_load_blend_const_color_a_float: return agx_blend_const(b, dst, 3);

  default:
       fprintf(stderr, "Unhandled intrinsic %s\n", nir_intrinsic_infos[instr->intrinsic].name);
       unreachable("Unhandled intrinsic");
  }
}

static agx_index
agx_alu_src_index(agx_builder *b, nir_alu_src src)
{
   /* Check well-formedness of the input NIR */
   ASSERTED unsigned bitsize = nir_src_bit_size(src.src);
   unsigned comps = nir_src_num_components(src.src);
   unsigned channel = src.swizzle[0];

   assert(bitsize == 1 || bitsize == 16 || bitsize == 32 || bitsize == 64);
   assert(!(src.negate || src.abs));
   assert(channel < comps);

   agx_index idx = agx_src_index(&src.src);

   /* We only deal with scalars, emit p_extract if needed */
   if (comps > 1)
      return agx_p_extract(b, idx, channel);
   else
      return idx;
}

static agx_instr *
agx_emit_alu_bool(agx_builder *b, nir_op op,
      agx_index dst, agx_index s0, agx_index s1, agx_index s2)
{
   /* Handle 1-bit bools as zero/nonzero rather than specifically 0/1 or 0/~0.
    * This will give the optimizer flexibility. */
   agx_index f = agx_immediate(0);
   agx_index t = agx_immediate(0x1);

   switch (op) {
   case nir_op_feq: return agx_fcmpsel_to(b, dst, s0, s1, t, f, AGX_FCOND_EQ);
   case nir_op_flt: return agx_fcmpsel_to(b, dst, s0, s1, t, f, AGX_FCOND_LT);
   case nir_op_fge: return agx_fcmpsel_to(b, dst, s0, s1, t, f, AGX_FCOND_GE);
   case nir_op_fneu: return agx_fcmpsel_to(b, dst, s0, s1, f, t, AGX_FCOND_EQ);

   case nir_op_ieq: return agx_icmpsel_to(b, dst, s0, s1, t, f, AGX_ICOND_UEQ);
   case nir_op_ine: return agx_icmpsel_to(b, dst, s0, s1, f, t, AGX_ICOND_UEQ);
   case nir_op_ilt: return agx_icmpsel_to(b, dst, s0, s1, t, f, AGX_ICOND_SLT);
   case nir_op_ige: return agx_icmpsel_to(b, dst, s0, s1, f, t, AGX_ICOND_SLT);
   case nir_op_ult: return agx_icmpsel_to(b, dst, s0, s1, t, f, AGX_ICOND_ULT);
   case nir_op_uge: return agx_icmpsel_to(b, dst, s0, s1, f, t, AGX_ICOND_ULT);

   case nir_op_mov: return agx_mov_to(b, dst, s0);
   case nir_op_iand: return agx_and_to(b, dst, s0, s1);
   case nir_op_ior: return agx_or_to(b, dst, s0, s1);
   case nir_op_ixor: return agx_xor_to(b, dst, s0, s1);
   case nir_op_inot: return agx_xor_to(b, dst, s0, t);

   case nir_op_f2b1: return agx_fcmpsel_to(b, dst, s0, f, f, t, AGX_FCOND_EQ);
   case nir_op_i2b1: return agx_icmpsel_to(b, dst, s0, f, f, t, AGX_ICOND_UEQ);
   case nir_op_b2b1: return agx_icmpsel_to(b, dst, s0, f, f, t, AGX_ICOND_UEQ);

   case nir_op_bcsel:
      return agx_icmpsel_to(b, dst, s0, f, s2, s1, AGX_ICOND_UEQ);

   default:
      fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[op].name);
      unreachable("Unhandled boolean ALU instruction");
   }
}

static agx_instr *
agx_emit_alu(agx_builder *b, nir_alu_instr *instr)
{
   unsigned srcs = nir_op_infos[instr->op].num_inputs;
   unsigned sz = nir_dest_bit_size(instr->dest.dest);
   unsigned src_sz = srcs ? nir_src_bit_size(instr->src[0].src) : 0;
   ASSERTED unsigned comps = nir_dest_num_components(instr->dest.dest);

   assert(comps == 1 || nir_op_is_vec(instr->op));
   assert(sz == 1 || sz == 16 || sz == 32 || sz == 64);

   agx_index dst = agx_dest_index(&instr->dest.dest);
   agx_index s0 = srcs > 0 ? agx_alu_src_index(b, instr->src[0]) : agx_null();
   agx_index s1 = srcs > 1 ? agx_alu_src_index(b, instr->src[1]) : agx_null();
   agx_index s2 = srcs > 2 ? agx_alu_src_index(b, instr->src[2]) : agx_null();
   agx_index s3 = srcs > 3 ? agx_alu_src_index(b, instr->src[3]) : agx_null();

   /* 1-bit bools are a bit special, only handle with select ops */
   if (sz == 1)
      return agx_emit_alu_bool(b, instr->op, dst, s0, s1, s2);

#define UNOP(nop, aop) \
   case nir_op_ ## nop: return agx_ ## aop ## _to(b, dst, s0);
#define BINOP(nop, aop) \
   case nir_op_ ## nop: return agx_ ## aop ## _to(b, dst, s0, s1);
#define TRIOP(nop, aop) \
   case nir_op_ ## nop: return agx_ ## aop ## _to(b, dst, s0, s1, s2);

   switch (instr->op) {
   BINOP(fadd, fadd);
   BINOP(fmul, fmul);
   TRIOP(ffma, fma);

   UNOP(f2f16, fmov);
   UNOP(f2f32, fmov);
   UNOP(fround_even, roundeven);
   UNOP(ftrunc, trunc);
   UNOP(ffloor, floor);
   UNOP(fceil, ceil);
   UNOP(frcp, rcp);
   UNOP(frsq, rsqrt);
   UNOP(flog2, log2);
   UNOP(fexp2, exp2);

   UNOP(fddx, dfdx);
   UNOP(fddx_coarse, dfdx);
   UNOP(fddx_fine, dfdx);

   UNOP(fddy, dfdy);
   UNOP(fddy_coarse, dfdy);
   UNOP(fddy_fine, dfdy);

   UNOP(mov, mov);
   UNOP(u2u16, mov);
   UNOP(u2u32, mov);
   UNOP(inot, not);
   BINOP(iand, and);
   BINOP(ior, or);
   BINOP(ixor, xor);

   case nir_op_fsqrt: return agx_fmul_to(b, dst, s0, agx_srsqrt(b, s0));
   case nir_op_fsub: return agx_fadd_to(b, dst, s0, agx_neg(s1));
   case nir_op_fabs: return agx_fmov_to(b, dst, agx_abs(s0));
   case nir_op_fneg: return agx_fmov_to(b, dst, agx_neg(s0));

   case nir_op_fmin: return agx_fcmpsel_to(b, dst, s0, s1, s0, s1, AGX_FCOND_LTN);
   case nir_op_fmax: return agx_fcmpsel_to(b, dst, s0, s1, s0, s1, AGX_FCOND_GTN);
   case nir_op_imin: return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_SLT);
   case nir_op_imax: return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_SGT);
   case nir_op_umin: return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_ULT);
   case nir_op_umax: return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_UGT);

   case nir_op_iadd: return agx_iadd_to(b, dst, s0, s1, 0);
   case nir_op_isub: return agx_iadd_to(b, dst, s0, agx_neg(s1), 0);
   case nir_op_ineg: return agx_iadd_to(b, dst, agx_zero(), agx_neg(s0), 0);
   case nir_op_imul: return agx_imad_to(b, dst, s0, s1, agx_zero(), 0);

   case nir_op_ishl: return agx_bfi_to(b, dst, agx_zero(), s0, s1, 0);
   case nir_op_ushr: return agx_ushr_to(b, dst, s0, s1);
   case nir_op_ishr: return agx_asr_to(b, dst, s0, s1);

   case nir_op_bcsel:
      return agx_icmpsel_to(b, dst, s0, agx_zero(), s2, s1, AGX_ICOND_UEQ);

   case nir_op_b2i32:
   case nir_op_b2i16:
      return agx_icmpsel_to(b, dst, s0, agx_zero(), agx_zero(), agx_immediate(1), AGX_ICOND_UEQ);

   case nir_op_b2f16:
   case nir_op_b2f32:
   {
      /* At this point, boolean is just zero/nonzero, so compare with zero */
      agx_index one = (sz == 16) ?
         agx_mov_imm(b, 16, _mesa_float_to_half(1.0)) :
         agx_mov_imm(b, 32, fui(1.0));

      agx_index zero = agx_zero();

      return agx_fcmpsel_to(b, dst, s0, zero, zero, one, AGX_FCOND_EQ);
   }

   case nir_op_i2i32:
   {
      if (s0.size != AGX_SIZE_16)
         unreachable("todo: more conversions");

      return agx_iadd_to(b, dst, s0, agx_zero(), 0);
   }

   case nir_op_i2i16:
   {
      if (s0.size != AGX_SIZE_32)
         unreachable("todo: more conversions");

      return agx_iadd_to(b, dst, s0, agx_zero(), 0);
   }

   case nir_op_iadd_sat:
   {
      agx_instr *I = agx_iadd_to(b, dst, s0, s1, 0);
      I->saturate = true;
      return I;
   }

   case nir_op_isub_sat:
   {
      agx_instr *I = agx_iadd_to(b, dst, s0, agx_neg(s1), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_uadd_sat:
   {
      agx_instr *I = agx_iadd_to(b, dst, agx_abs(s0), agx_abs(s1), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_usub_sat:
   {
      agx_instr *I = agx_iadd_to(b, dst, agx_abs(s0), agx_neg(agx_abs(s1)), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_fsat:
   {
      agx_instr *I = agx_fadd_to(b, dst, s0, agx_negzero());
      I->saturate = true;
      return I;
   }

   case nir_op_fsin_agx:
   {
      agx_index fixup = agx_sin_pt_1(b, s0);
      agx_index sinc = agx_sin_pt_2(b, fixup);
      return agx_fmul_to(b, dst, sinc, fixup);
   }

   case nir_op_f2i16:
      return agx_convert_to(b, dst,
            agx_immediate(AGX_CONVERT_F_TO_S16), s0, AGX_ROUND_RTZ);

   case nir_op_f2i32:
      return agx_convert_to(b, dst,
            agx_immediate(AGX_CONVERT_F_TO_S32), s0, AGX_ROUND_RTZ);

   case nir_op_f2u16:
      return agx_convert_to(b, dst,
            agx_immediate(AGX_CONVERT_F_TO_U16), s0, AGX_ROUND_RTZ);

   case nir_op_f2u32:
      return agx_convert_to(b, dst,
            agx_immediate(AGX_CONVERT_F_TO_U32), s0, AGX_ROUND_RTZ);

   case nir_op_u2f16:
   case nir_op_u2f32:
   {
      if (src_sz == 64)
         unreachable("64-bit conversions unimplemented");

      enum agx_convert mode =
         (src_sz == 32) ? AGX_CONVERT_U32_TO_F :
         (src_sz == 16) ? AGX_CONVERT_U16_TO_F :
                          AGX_CONVERT_U8_TO_F;

      return agx_convert_to(b, dst, agx_immediate(mode), s0, AGX_ROUND_RTE);
   }

   case nir_op_i2f16:
   case nir_op_i2f32:
   {
      if (src_sz == 64)
         unreachable("64-bit conversions unimplemented");

      enum agx_convert mode =
         (src_sz == 32) ? AGX_CONVERT_S32_TO_F :
         (src_sz == 16) ? AGX_CONVERT_S16_TO_F :
                          AGX_CONVERT_S8_TO_F;

      return agx_convert_to(b, dst, agx_immediate(mode), s0, AGX_ROUND_RTE);
   }

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
      return agx_p_combine_to(b, dst, s0, s1, s2, s3);

   case nir_op_vec8:
   case nir_op_vec16:
      unreachable("should've been lowered");

   default:
      fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
      unreachable("Unhandled ALU instruction");
   }
}

static enum agx_dim
agx_tex_dim(enum glsl_sampler_dim dim, bool array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      return array ? AGX_DIM_TEX_1D_ARRAY : AGX_DIM_TEX_1D;

   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return array ? AGX_DIM_TEX_2D_ARRAY : AGX_DIM_TEX_2D;

   case GLSL_SAMPLER_DIM_MS:
      assert(!array && "multisampled arrays unsupported");
      return AGX_DIM_TEX_2D_MS;

   case GLSL_SAMPLER_DIM_3D:
      assert(!array && "3D arrays unsupported");
      return AGX_DIM_TEX_3D;

   case GLSL_SAMPLER_DIM_CUBE:
      return array ? AGX_DIM_TEX_CUBE_ARRAY : AGX_DIM_TEX_CUBE;

   default:
      unreachable("Invalid sampler dim\n");
   }
}

static void
agx_emit_tex(agx_builder *b, nir_tex_instr *instr)
{
   switch (instr->op) {
   case nir_texop_tex:
   case nir_texop_txl:
      break;
   default:
      unreachable("Unhandled texture op");
   }

   enum agx_lod_mode lod_mode = (instr->op == nir_texop_tex) ?
      AGX_LOD_MODE_AUTO_LOD : AGX_LOD_MODE_LOD_MIN;

   agx_index coords = agx_null(),
             texture = agx_immediate(instr->texture_index),
             sampler = agx_immediate(instr->sampler_index),
             lod = agx_immediate(0),
             offset = agx_null();

   for (unsigned i = 0; i < instr->num_srcs; ++i) {
      agx_index index = agx_src_index(&instr->src[i].src);

      switch (instr->src[i].src_type) {
      case nir_tex_src_coord:
         coords = index;
         break;

      case nir_tex_src_lod:
         lod = index;
         break;

      case nir_tex_src_bias:
      case nir_tex_src_ms_index:
      case nir_tex_src_offset:
      case nir_tex_src_comparator:
      case nir_tex_src_texture_offset:
      case nir_tex_src_sampler_offset:
      default:
         unreachable("todo");
      }
   }

   agx_texture_sample_to(b, agx_dest_index(&instr->dest),
         coords, lod, texture, sampler, offset,
         agx_tex_dim(instr->sampler_dim, instr->is_array),
         lod_mode,
         0xF, /* TODO: wrmask */
         0);

   agx_wait(b, 0);
}

/* NIR loops are treated as a pair of AGX loops:
 *
 *    do {
 *       do {
 *          ...
 *       } while (0);
 *    } while (cond);
 *
 * By manipulating the nesting counter (r0l), we may break out of nested loops,
 * so under the model, both break and continue may be implemented as breaks,
 * where break breaks out of the outer loop (2 layers) and continue breaks out
 * of the inner loop (1 layer).
 *
 * After manipulating the nesting counter directly, pop_exec #0 must be used to
 * flush the update to the execution mask.
 */

static void
agx_emit_jump(agx_builder *b, nir_jump_instr *instr)
{
   agx_context *ctx = b->shader;
   assert (instr->type == nir_jump_break || instr->type == nir_jump_continue);

   /* Break out of either one or two loops */
   unsigned nestings = b->shader->loop_nesting;

   if (instr->type == nir_jump_continue) {
      nestings += 1;
      agx_block_add_successor(ctx->current_block, ctx->continue_block);
   } else if (instr->type == nir_jump_break) {
      nestings += 2;
      agx_block_add_successor(ctx->current_block, ctx->break_block);
   }

   /* Update the counter and flush */
   agx_index r0l = agx_register(0, false);
   agx_mov_to(b, r0l, agx_immediate(nestings));
   agx_pop_exec(b, 0);

   ctx->current_block->unconditional_jumps = true;
}

static void
agx_emit_instr(agx_builder *b, struct nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_load_const:
      agx_emit_load_const(b, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_intrinsic:
      agx_emit_intrinsic(b, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_alu:
      agx_emit_alu(b, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_tex:
      agx_emit_tex(b, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_jump:
      agx_emit_jump(b, nir_instr_as_jump(instr));
      break;

   default:
      unreachable("should've been lowered");
   }
}

static agx_block *
agx_create_block(agx_context *ctx)
{
   agx_block *blk = rzalloc(ctx, agx_block);

   blk->predecessors = _mesa_set_create(blk,
         _mesa_hash_pointer, _mesa_key_pointer_equal);

   return blk;
}

static agx_block *
emit_block(agx_context *ctx, nir_block *block)
{
   if (ctx->after_block) {
      ctx->current_block = ctx->after_block;
      ctx->after_block = NULL;
   } else {
      ctx->current_block = agx_create_block(ctx);
   }

   agx_block *blk = ctx->current_block;
   list_addtail(&blk->link, &ctx->blocks);
   list_inithead(&blk->instructions);

   agx_builder _b = agx_init_builder(ctx, agx_after_block(blk));

   nir_foreach_instr(instr, block) {
      agx_emit_instr(&_b, instr);
   }

   return blk;
}

static agx_block *
emit_cf_list(agx_context *ctx, struct exec_list *list);

/* Emit if-else as
 *
 *    if_icmp cond != 0
 *       ...
 *    else_icmp cond == 0
 *       ...
 *    pop_exec
 *
 * If the else is empty, we can omit the else_icmp. This is not usually
 * optimal, but it's a start.
 */

static void
emit_if(agx_context *ctx, nir_if *nif)
{
   nir_block *nir_else_block = nir_if_first_else_block(nif);
   bool empty_else_block =
      (nir_else_block == nir_if_last_else_block(nif) &&
       exec_list_is_empty(&nir_else_block->instr_list));

   agx_block *first_block = ctx->current_block;
   agx_builder _b = agx_init_builder(ctx, agx_after_block(first_block));
   agx_index cond = agx_src_index(&nif->condition);

   agx_if_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, true);
   ctx->loop_nesting++;

   /* Emit the two subblocks. */
   agx_block *if_block = emit_cf_list(ctx, &nif->then_list);
   agx_block *end_then = ctx->current_block;

   if (!empty_else_block) {
      _b.cursor = agx_after_block(ctx->current_block);
      agx_else_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, false);
   }

   agx_block *else_block = emit_cf_list(ctx, &nif->else_list);
   agx_block *end_else = ctx->current_block;

   ctx->after_block = agx_create_block(ctx);

   agx_block_add_successor(first_block, if_block);
   agx_block_add_successor(first_block, else_block);
   agx_block_add_successor(end_then, ctx->after_block);
   agx_block_add_successor(end_else, ctx->after_block);

   _b.cursor = agx_after_block(ctx->current_block);
   agx_pop_exec(&_b, 1);
   ctx->loop_nesting--;
}

static void
emit_loop(agx_context *ctx, nir_loop *nloop)
{
   /* We only track nesting within the innermost loop, so reset */
   ctx->loop_nesting = 0;

   agx_block *popped_break = ctx->break_block;
   agx_block *popped_continue = ctx->continue_block;

   ctx->break_block = agx_create_block(ctx);
   ctx->continue_block = agx_create_block(ctx);

   /* Make room for break/continue nesting (TODO: skip if no divergent CF) */
   agx_builder _b = agx_init_builder(ctx, agx_after_block(ctx->current_block));
   agx_push_exec(&_b, 2);

   /* Fallthrough to body */
   agx_block_add_successor(ctx->current_block, ctx->continue_block);

   /* Emit the body */
   ctx->after_block = ctx->continue_block;
   agx_block *start_block = emit_cf_list(ctx, &nloop->body);

   /* Fix up the nesting counter via an always true while_icmp, and branch back
    * to start of loop if any lanes are active */
   _b.cursor = agx_after_block(ctx->current_block);
   agx_while_icmp(&_b, agx_zero(), agx_zero(), 2, AGX_ICOND_UEQ, false);
   agx_jmp_exec_any(&_b, start_block);
   agx_pop_exec(&_b, 2);
   agx_block_add_successor(ctx->current_block, ctx->continue_block);

   /* Pop off */
   ctx->after_block = ctx->break_block;
   ctx->break_block = popped_break;
   ctx->continue_block = popped_continue;

   /* Update shader-db stats */
   ++ctx->loop_count;

   /* All nested control flow must have finished */
   assert(ctx->loop_nesting == 0);
}

/* Before the first control flow structure, the nesting counter (r0l) needs to
 * be zeroed for correct operation. This only happens at most once, since by
 * definition this occurs at the end of the first block, which dominates the
 * rest of the program. */

static void
emit_first_cf(agx_context *ctx)
{
   if (ctx->any_cf)
      return;

   agx_builder _b = agx_init_builder(ctx, agx_after_block(ctx->current_block));
   agx_index r0l = agx_register(0, false);

   agx_mov_to(&_b, r0l, agx_immediate(0));
   ctx->any_cf = true;
}

static agx_block *
emit_cf_list(agx_context *ctx, struct exec_list *list)
{
   agx_block *start_block = NULL;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         agx_block *block = emit_block(ctx, nir_cf_node_as_block(node));

         if (!start_block)
            start_block = block;

         break;
      }

      case nir_cf_node_if:
         emit_first_cf(ctx);
         emit_if(ctx, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         emit_first_cf(ctx);
         emit_loop(ctx, nir_cf_node_as_loop(node));
         break;

      default:
         unreachable("Unknown control flow");
      }
   }

   return start_block;
}

static void
agx_set_st_vary_final(agx_context *ctx)
{
   agx_foreach_instr_global_rev(ctx, I) {
      if (I->op == AGX_OPCODE_ST_VARY) {
         I->last = true;
         return;
      }
   }
}

static void
agx_print_stats(agx_context *ctx, unsigned size, FILE *fp)
{
   unsigned nr_ins = 0, nr_bytes = 0, nr_threads = 1;

   /* TODO */
   fprintf(stderr, "%s shader: %u inst, %u bytes, %u threads, %u loops,"
           "%u:%u spills:fills\n",
           ctx->nir->info.label ?: "",
           nr_ins, nr_bytes, nr_threads, ctx->loop_count,
           ctx->spills, ctx->fills);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static bool
agx_lower_sincos_filter(const nir_instr *instr, UNUSED const void *_)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   return alu->op == nir_op_fsin || alu->op == nir_op_fcos;
}

/* Sine and cosine are implemented via the sin_pt_1 and sin_pt_2 opcodes for
 * heavy lifting. sin_pt_2 implements sinc in the first quadrant, expressed in
 * turns (sin (tau x) / x), while sin_pt_1 implements a piecewise sign/offset
 * fixup to transform a quadrant angle [0, 4] to [-1, 1]. The NIR opcode
 * fsin_agx models the fixup, sinc, and multiply to obtain sine, so we just
 * need to change units from radians to quadrants modulo turns. Cosine is
 * implemented by shifting by one quadrant: cos(x) = sin(x + tau/4).
 */

static nir_ssa_def *
agx_lower_sincos_impl(struct nir_builder *b, nir_instr *instr, UNUSED void *_)
{
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   nir_ssa_def *x = nir_mov_alu(b, alu->src[0], 1);
   nir_ssa_def *turns = nir_fmul_imm(b, x, M_1_PI * 0.5f);

   if (alu->op == nir_op_fcos)
      turns = nir_fadd_imm(b, turns, 0.25f);

   nir_ssa_def *quadrants = nir_fmul_imm(b, nir_ffract(b, turns), 4.0);
   return nir_fsin_agx(b, quadrants);
}

static bool
agx_lower_sincos(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader,
         agx_lower_sincos_filter, agx_lower_sincos_impl, NULL);
}

static bool
agx_lower_front_face(struct nir_builder *b,
                     nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_front_face)
      return false;

   assert(intr->dest.is_ssa);
   nir_ssa_def *def = &intr->dest.ssa;
   assert(def->bit_size == 1);

   b->cursor = nir_before_instr(&intr->instr);
   nir_ssa_def_rewrite_uses(def, nir_inot(b, nir_load_back_face_agx(b, 1)));
   return true;
}

static bool
agx_lower_point_coord(struct nir_builder *b,
                      nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   if (var->data.mode != nir_var_shader_in)
      return false;

   if (var->data.location != VARYING_SLOT_PNTC)
      return false;

   assert(intr->dest.is_ssa);
   assert(intr->dest.ssa.num_components == 2);

   b->cursor = nir_after_instr(&intr->instr);
   nir_ssa_def *def = nir_load_deref(b, deref);
   nir_ssa_def *y = nir_channel(b, def, 1);
   nir_ssa_def *flipped_y = nir_fadd_imm(b, nir_fneg(b, y), 1.0);
   nir_ssa_def *flipped = nir_vec2(b, nir_channel(b, def, 0), flipped_y);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, flipped);
   return true;
}

static void
agx_optimize_nir(nir_shader *nir)
{
   bool progress;

   nir_lower_idiv_options idiv_options = {
      .imprecise_32bit_lowering = true,
      .allow_fp16 = true,
   };

   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   NIR_PASS_V(nir, nir_lower_int64);
   NIR_PASS_V(nir, nir_lower_idiv, &idiv_options);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   NIR_PASS_V(nir, nir_lower_flrp, 16 | 32 | 64, false);
   NIR_PASS_V(nir, agx_lower_sincos);
   NIR_PASS_V(nir, nir_shader_instructions_pass,
         agx_lower_front_face,
         nir_metadata_block_index | nir_metadata_dominance, NULL);

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_lower_undef_to_zero);

      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);

   NIR_PASS_V(nir, nir_opt_algebraic_late);
   NIR_PASS_V(nir, nir_opt_constant_folding);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, nir_opt_cse);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);

   /* Cleanup optimizations */
   nir_move_options move_all =
      nir_move_const_undef | nir_move_load_ubo | nir_move_load_input |
      nir_move_comparisons | nir_move_copies | nir_move_load_ssbo;

   NIR_PASS_V(nir, nir_opt_sink, move_all);
   NIR_PASS_V(nir, nir_opt_move, move_all);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);
}

/* ABI: position first, then user, then psiz */
static void
agx_remap_varyings_vs(nir_shader *nir, struct agx_varyings *varyings,
                      unsigned *remap)
{
   unsigned base = 0;

   nir_variable *pos = nir_find_variable_with_location(nir, nir_var_shader_out, VARYING_SLOT_POS);
   if (pos) {
      assert(pos->data.driver_location < AGX_MAX_VARYINGS);
      remap[pos->data.driver_location] = base;
      base += 4;
   }

   nir_foreach_shader_out_variable(var, nir) {
      unsigned loc = var->data.location;

      if(loc == VARYING_SLOT_POS || loc == VARYING_SLOT_PSIZ) {
         continue;
      }

      assert(var->data.driver_location < AGX_MAX_VARYINGS);
      remap[var->data.driver_location] = base;
      base += 4;
   }

   nir_variable *psiz = nir_find_variable_with_location(nir, nir_var_shader_out, VARYING_SLOT_PSIZ);
   if (psiz) {
      assert(psiz->data.driver_location < AGX_MAX_VARYINGS);
      remap[psiz->data.driver_location] = base;
      base += 1;
   }

   varyings->nr_slots = base;
}

static void
agx_remap_varyings_fs(nir_shader *nir, struct agx_varyings *varyings,
                      unsigned *remap)
{
   struct agx_varying_packed *packed = varyings->packed;
   unsigned base = 0;

   agx_pack(packed, VARYING, cfg) {
      cfg.type = AGX_VARYING_TYPE_FRAGCOORD_W;
      cfg.components = 1;
      cfg.triangle_slot = cfg.point_slot = base;
   }

   base++;
   packed++;

   agx_pack(packed, VARYING, cfg) {
      cfg.type = AGX_VARYING_TYPE_FRAGCOORD_Z;
      cfg.components = 1;
      cfg.triangle_slot = cfg.point_slot = base;
   }

   base++;
   packed++;

   unsigned comps[MAX_VARYING] = { 0 };

   nir_foreach_shader_in_variable(var, nir) {
     unsigned loc = var->data.driver_location;
     const struct glsl_type *column =
        glsl_without_array_or_matrix(var->type);
     unsigned chan = glsl_get_components(column);

     /* If we have a fractional location added, we need to increase the size
      * so it will fit, i.e. a vec3 in YZW requires us to allocate a vec4.
      * We could do better but this is an edge case as it is, normally
      * packed varyings will be aligned.
      */
     chan += var->data.location_frac;
     comps[loc] = MAX2(comps[loc], chan);
   }

   nir_foreach_shader_in_variable(var, nir) {
     unsigned loc = var->data.driver_location;
     unsigned sz = glsl_count_attribute_slots(var->type, FALSE);
     unsigned channels = comps[loc];

     assert(var->data.driver_location <= AGX_MAX_VARYINGS);
     remap[var->data.driver_location] = base;

     for (int c = 0; c < sz; ++c) {
        agx_pack(packed, VARYING, cfg) {
           cfg.type = (var->data.location == VARYING_SLOT_PNTC) ?
              AGX_VARYING_TYPE_POINT_COORDINATES :
              (var->data.interpolation == INTERP_MODE_FLAT) ?
                 AGX_VARYING_TYPE_FLAT_LAST :
                 AGX_VARYING_TYPE_SMOOTH;

           cfg.components = channels;
           cfg.triangle_slot = cfg.point_slot = base;
        }

        base += channels;
        packed++;
     }
   }

   varyings->nr_descs = (packed - varyings->packed);
   varyings->nr_slots = base;
}

void
agx_compile_shader_nir(nir_shader *nir,
      struct agx_shader_key *key,
      struct util_dynarray *binary,
      struct agx_shader_info *out)
{
   agx_debug = debug_get_option_agx_debug();

   agx_context *ctx = rzalloc(NULL, agx_context);
   ctx->nir = nir;
   ctx->out = out;
   ctx->key = key;
   ctx->stage = nir->info.stage;
   list_inithead(&ctx->blocks);

   if (ctx->stage == MESA_SHADER_VERTEX) {
      out->writes_psiz = nir->info.outputs_written &
         BITFIELD_BIT(VARYING_SLOT_PSIZ);
   }

   NIR_PASS_V(nir, nir_lower_vars_to_ssa);

   /* Lower large arrays to scratch and small arrays to csel */
   NIR_PASS_V(nir, nir_lower_vars_to_scratch, nir_var_function_temp, 16,
         glsl_get_natural_size_align_bytes);
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_function_temp, ~0);

   if (ctx->stage == MESA_SHADER_VERTEX) {
      /* Lower from OpenGL [-1, 1] to [0, 1] if half-z is not set */
      if (!key->vs.clip_halfz)
         NIR_PASS_V(nir, nir_lower_clip_halfz);
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      /* Flip point coordinate since OpenGL and Metal disagree */
      NIR_PASS_V(nir, nir_shader_instructions_pass,
            agx_lower_point_coord,
            nir_metadata_block_index | nir_metadata_dominance, NULL);
   }

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
         glsl_type_size, 0);
   if (ctx->stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_mediump_io,
            nir_var_shader_in | nir_var_shader_out, ~0, false);
   }
   NIR_PASS_V(nir, nir_lower_ssbo);

   /* Varying output is scalar, other I/O is vector */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      NIR_PASS_V(nir, nir_lower_io_to_scalar, nir_var_shader_out);
   }

   nir_lower_tex_options lower_tex_options = {
      .lower_txs_lod = true,
      .lower_txp = ~0,
   };

   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_lod] = { true, 16 }
   };

   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS_V(nir, nir_legalize_16bit_sampler_srcs, tex_constraints);

   agx_optimize_nir(nir);

   /* Must be last since NIR passes can remap driver_location freely */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      agx_remap_varyings_vs(nir, &out->varyings, ctx->varyings);
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      agx_remap_varyings_fs(nir, &out->varyings, ctx->varyings);
   }

   bool skip_internal = nir->info.internal;
   skip_internal &= !(agx_debug & AGX_DBG_INTERNAL);

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal) {
      nir_print_shader(nir, stdout);
   }

   nir_foreach_function(func, nir) {
      if (!func->impl)
         continue;

      /* TODO: Handle phi nodes instead of just convert_from_ssa and yolo'ing
       * the mapping of nir_register to hardware registers and guaranteeing bad
       * performance and breaking spilling... */
      ctx->nir_regalloc = rzalloc_array(ctx, unsigned, func->impl->reg_alloc);

      /* Leave the last 4 registers for hacky p-copy lowering */
      unsigned nir_regalloc = AGX_NUM_REGS - (4 * 2);

      /* Assign backwards so we don't need to guess a size */
      nir_foreach_register(reg, &func->impl->registers) {
         /* Ensure alignment */
         if (reg->bit_size >= 32 && (nir_regalloc & 1))
            nir_regalloc--;

         unsigned size = DIV_ROUND_UP(reg->bit_size * reg->num_components, 16);
         nir_regalloc -= size;
         ctx->nir_regalloc[reg->index] = nir_regalloc;
      }

      ctx->max_register = nir_regalloc;
      ctx->alloc += func->impl->ssa_alloc;
      emit_cf_list(ctx, &func->impl->body);
      break; /* TODO: Multi-function shaders */
   }

   /* TODO: Actual RA... this way passes don't need to deal nir_register */
   agx_foreach_instr_global(ctx, I) {
      agx_foreach_dest(I, d) {
         if (I->dest[d].type == AGX_INDEX_NIR_REGISTER) {
            I->dest[d].type = AGX_INDEX_REGISTER;
            I->dest[d].value = ctx->nir_regalloc[I->dest[d].value];
         }
      }

      agx_foreach_src(I, s) {
         if (I->src[s].type == AGX_INDEX_NIR_REGISTER) {
            I->src[s].type = AGX_INDEX_REGISTER;
            I->src[s].value = ctx->nir_regalloc[I->src[s].value];
         }
      }
   }

   /* Terminate the shader after the exit block */
   agx_block *last_block = list_last_entry(&ctx->blocks, agx_block, link);
   agx_builder _b = agx_init_builder(ctx, agx_after_block(last_block));
   agx_stop(&_b);

   /* Also add traps to match the blob, unsure what the function is */
   for (unsigned i = 0; i < 8; ++i)
      agx_trap(&_b);

   unsigned block_source_count = 0;

   /* Name blocks now that we're done emitting so the order is consistent */
   agx_foreach_block(ctx, block)
      block->name = block_source_count++;

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
      agx_print_shader(ctx, stdout);

   agx_optimizer(ctx);
   agx_dce(ctx);

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
      agx_print_shader(ctx, stdout);

   agx_ra(ctx);

   if (ctx->stage == MESA_SHADER_VERTEX)
      agx_set_st_vary_final(ctx);

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
      agx_print_shader(ctx, stdout);

   agx_pack_binary(ctx, binary);

   if ((agx_debug & AGX_DBG_SHADERDB) && !skip_internal)
      agx_print_stats(ctx, binary->size, stderr);

   ralloc_free(ctx);
}
