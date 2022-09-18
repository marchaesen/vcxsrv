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

#include "main/glheader.h"
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
   {"novalidate",AGX_DBG_NOVALIDATE,"Skip IR validation in debug builds"},
   {"noopt",     AGX_DBG_NOOPT,     "Disable backend optimizations"},
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(agx_debug, "AGX_MESA_DEBUG", agx_debug_options, 0)

int agx_debug = 0;

#define DBG(fmt, ...) \
   do { if (agx_debug & AGX_DBG_MSGS) \
      fprintf(stderr, "%s:%d: "fmt, \
            __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

static agx_index
agx_get_cf(agx_context *ctx, bool smooth, bool perspective,
           gl_varying_slot slot, unsigned offset, unsigned count)
{
   struct agx_varyings_fs *varyings = &ctx->out->varyings.fs;
   unsigned cf_base = varyings->nr_cf;

   if (slot == VARYING_SLOT_POS) {
      assert(offset == 2 || offset == 3);
      varyings->reads_z |= (offset == 2);
   }

   /* First, search for an appropriate binding. This is O(n) to the number of
    * bindings, which isn't great, but n should be small in practice.
    */
   for (unsigned b = 0; b < varyings->nr_bindings; ++b) {
      if ((varyings->bindings[b].slot == slot) &&
          (varyings->bindings[b].offset == offset) &&
          (varyings->bindings[b].count == count) &&
          (varyings->bindings[b].smooth == smooth) &&
          (varyings->bindings[b].perspective == perspective)) {

         return agx_immediate(varyings->bindings[b].cf_base);
      }
   }

   /* If we didn't find one, make one */
   unsigned b = varyings->nr_bindings++;
   varyings->bindings[b].cf_base = varyings->nr_cf;
   varyings->bindings[b].slot = slot;
   varyings->bindings[b].offset = offset;
   varyings->bindings[b].count = count;
   varyings->bindings[b].smooth = smooth;
   varyings->bindings[b].perspective = perspective;
   varyings->nr_cf += count;

   return agx_immediate(cf_base);
}

/* Builds a 64-bit hash table key for an index */
static uint64_t
agx_index_to_key(agx_index idx)
{
   STATIC_ASSERT(sizeof(idx) <= sizeof(uint64_t));

   uint64_t key = 0;
   memcpy(&key, &idx, sizeof(idx));
   return key;
}

/*
 * Extract a single channel out of a vector source. We split vectors with
 * p_split so we can use the split components directly, without emitting a
 * machine instruction. This has advantages of RA, as the split can usually be
 * optimized away.
 */
static agx_index
agx_emit_extract(agx_builder *b, agx_index vec, unsigned channel)
{
   agx_index *components = _mesa_hash_table_u64_search(b->shader->allocated_vec,
                                                       agx_index_to_key(vec));

   assert(components != NULL && "missing agx_emit_combine_to");

   return components[channel];
}

static void
agx_cache_combine(agx_builder *b, agx_index dst, unsigned nr_srcs,
                  agx_index *srcs)
{
   /* Lifetime of a hash table entry has to be at least as long as the table */
   agx_index *channels = ralloc_array(b->shader, agx_index, nr_srcs);

   for (unsigned i = 0; i < nr_srcs; ++i)
      channels[i] = srcs[i];

   _mesa_hash_table_u64_insert(b->shader->allocated_vec, agx_index_to_key(dst),
                               channels);
}

/*
 * Combine multiple scalars into a vector destination. This corresponds to
 * p_combine, lowered to moves (a shuffle in general) after register allocation.
 *
 * To optimize vector extractions, we record the individual channels
 */
static agx_instr *
agx_emit_combine_to(agx_builder *b, agx_index dst, unsigned nr_srcs,
                    agx_index *srcs)
{
   agx_cache_combine(b, dst, 4, srcs);
   agx_instr *I = agx_p_combine_to(b, dst, nr_srcs);

   agx_foreach_src(I, s)
      I->src[s] = srcs[s];

   return I;
}

static agx_index
agx_vec4(agx_builder *b, agx_index s0, agx_index s1, agx_index s2, agx_index s3)
{
      agx_index dst = agx_temp(b->shader, s0.size);
      agx_index idx[4] = { s0, s1, s2, s3 };
      agx_emit_combine_to(b, dst, 4, idx);
      return dst;
}

static agx_index
agx_vec2(agx_builder *b, agx_index s0, agx_index s1)
{
   agx_index dst = agx_temp(b->shader, s0.size);
   agx_index idx[2] = { s0, s1 };
   agx_emit_combine_to(b, dst, 2, idx);
   return dst;
}

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
      util_dynarray_append(&successor->predecessors, agx_block *, block);
      return;
   }

   unreachable("Too many successors");
}

/*
 * Splits an n-component vector (vec) into n scalar destinations (dests) using a
 * split pseudo-instruction.
 *
 * Pre-condition: dests is filled with agx_null().
 */
static void
agx_emit_split(agx_builder *b, agx_index *dests, agx_index vec, unsigned n)
{
   /* Setup the destinations */
   for (unsigned i = 0; i < n; ++i) {
      dests[i] = agx_temp(b->shader, vec.size);
   }

   /* Emit the split */
   agx_p_split_to(b, dests[0], dests[1], dests[2], dests[3], vec);
}

static void
agx_emit_cached_split(agx_builder *b, agx_index vec, unsigned n)
{
   agx_index dests[4] = { agx_null(), agx_null(), agx_null(), agx_null() };
   agx_emit_split(b, dests, vec, n);
   agx_cache_combine(b, vec, n, dests);
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

/*
 * Implement umul_high of 32-bit sources by doing a 32x32->64-bit multiply and
 * extracting only the high word.
 */
static agx_instr *
agx_umul_high_to(agx_builder *b, agx_index dst, agx_index P, agx_index Q)
{
   assert(P.size == Q.size && "source sizes must match");
   assert(P.size == dst.size && "dest size must match");
   assert(P.size != AGX_SIZE_64 && "64x64 multiply should have been lowered");

   static_assert(AGX_SIZE_64 == (AGX_SIZE_32 + 1), "enum wrong");
   static_assert(AGX_SIZE_32 == (AGX_SIZE_16 + 1), "enum wrong");

   agx_index product = agx_temp(b->shader, P.size + 1);
   agx_imad_to(b, product, agx_abs(P), agx_abs(Q), agx_zero(), 0);
   return agx_p_split_to(b, agx_null(), dst, agx_null(), agx_null(), product);
}

static agx_index
agx_umul_high(agx_builder *b, agx_index P, agx_index Q)
{
   agx_index dst = agx_temp(b->shader, P.size);
   agx_umul_high_to(b, dst, P, Q);
   return dst;
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
   agx_index n = P;

   if (info.pre_shift != 0) n = agx_ushr(b, n, preshift);
   if (info.increment != 0) n = agx_iadd(b, n, increment, 0);

   n = agx_umul_high(b, n, multiplier);

   if (info.post_shift != 0) n = agx_ushr(b, n, postshift);

   return n;
}

/* AGX appears to lack support for vertex attributes. Lower to global loads. */
static void
agx_emit_load_attr(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
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
   agx_index base = agx_vbo_base(b->shader, buf);

   /* Load the data */
   assert(instr->num_components <= 4);

   unsigned actual_comps = (attrib.nr_comps_minus_1 + 1);
   agx_index vec = agx_vec_for_dest(b->shader, &instr->dest);
   agx_device_load_to(b, vec, base, offset, attrib.format,
                      BITFIELD_MASK(attrib.nr_comps_minus_1 + 1), 0);
   agx_wait(b, 0);

   agx_emit_split(b, dests, vec, actual_comps);

   agx_index one = agx_mov_imm(b, 32, fui(1.0));
   agx_index zero = agx_mov_imm(b, 32, 0);
   agx_index default_value[4] = { zero, zero, zero, one };

   for (unsigned i = actual_comps; i < instr->num_components; ++i)
      dests[i] = default_value[i];
}

static void
agx_emit_load_vary_flat(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
{
   unsigned components = instr->num_components;
   assert(components >= 1 && components <= 4);

   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");
   assert(nir_dest_bit_size(instr->dest) == 32 && "no 16-bit flat shading");

   /* Get all coefficient registers up front. This ensures the driver emits a
    * single vectorized binding.
    */
   agx_index cf = agx_get_cf(b->shader, false, false,
                             sem.location + nir_src_as_uint(*offset), 0,
                             components);

   for (unsigned i = 0; i < components; ++i) {
      /* vec3 for each vertex, unknown what first 2 channels are for */
      agx_index d[3] = { agx_null() };
      agx_emit_split(b, d, agx_ldcf(b, cf, 1), 3);
      dests[i] = d[2];

      /* Each component accesses a sequential coefficient register */
      cf.value++;
   }
}

static void
agx_emit_load_vary(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
{
   ASSERTED unsigned components = instr->num_components;
   nir_intrinsic_instr *bary = nir_src_as_intrinsic(instr->src[0]);

   assert(components >= 1 && components <= 4);

   /* TODO: Interpolation modes */
   assert(bary != NULL);
   assert(bary->intrinsic == nir_intrinsic_load_barycentric_pixel);

   bool perspective =
      nir_intrinsic_interp_mode(bary) != INTERP_MODE_NOPERSPECTIVE;

   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");

   /* For perspective interpolation, we need W */
   agx_index J = !perspective ? agx_zero() :
                  agx_get_cf(b->shader, true, false, VARYING_SLOT_POS, 3, 1);

   agx_index I = agx_get_cf(b->shader, true, perspective,
                           sem.location + nir_src_as_uint(*offset), 0,
                           components);

   agx_index vec = agx_vec_for_intr(b->shader, instr);
   agx_iter_to(b, vec, I, J, components, perspective);
   agx_emit_split(b, dests, vec, components);
}

static agx_instr *
agx_emit_store_vary(agx_builder *b, nir_intrinsic_instr *instr)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "todo: indirects");

   unsigned imm_index = b->shader->out->varyings.vs.slots[sem.location];
   assert(imm_index < ~0);
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
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   unsigned loc = sem.location;
   assert(sem.dual_source_blend_index == 0 && "todo: dual-source blending");
   assert(loc == FRAG_RESULT_DATA0 && "todo: MRT");
   unsigned rt = (loc - FRAG_RESULT_DATA0);

   /* TODO: Reverse-engineer interactions with MRT */
   if (b->shader->key->fs.ignore_tib_dependencies) {
      assert(b->shader->nir->info.internal && "only for clear shaders");
   } else if (b->shader->did_writeout) {
	   agx_writeout(b, 0x0004);
   } else {
	   agx_writeout(b, 0xC200);
	   agx_writeout(b, 0x000C);
   }

   if (b->shader->nir->info.fs.uses_discard) {
      /* If the shader uses discard, the sample mask must be written by the
       * shader on all exeuction paths. If we've reached the end of the shader,
       * we are therefore still active and need to write a full sample mask.
       * TODO: interactions with MSAA and gl_SampleMask writes
       */
      agx_sample_mask(b, agx_immediate(1));
   }

   b->shader->did_writeout = true;
   return agx_st_tile(b, agx_src_index(&instr->src[0]),
             b->shader->key->fs.tib_formats[rt]);
}

static void
agx_emit_load_tile(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   unsigned loc = sem.location;
   assert(sem.dual_source_blend_index == 0 && "dual src ld_tile is nonsense");
   assert(loc == FRAG_RESULT_DATA0 && "todo: MRT");
   unsigned rt = (loc - FRAG_RESULT_DATA0);

   /* TODO: Reverse-engineer interactions with MRT */
   assert(!b->shader->key->fs.ignore_tib_dependencies && "invalid usage");
   agx_writeout(b, 0xC200);
   agx_writeout(b, 0x0008);
   b->shader->did_writeout = true;
   b->shader->out->reads_tib = true;

   agx_index vec = agx_vec_for_dest(b->shader, &instr->dest);
   agx_ld_tile_to(b, vec, b->shader->key->fs.tib_formats[rt]);
   agx_emit_split(b, dests, vec, 4);
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

static void
agx_emit_load_global(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
{
   agx_index addr = agx_src_index(&instr->src[0]);
   agx_index offset = agx_immediate(0);
   enum agx_format fmt = agx_format_for_bits(nir_dest_bit_size(instr->dest));

   agx_index vec = agx_vec_for_intr(b->shader, instr);
   agx_device_load_to(b, vec, addr, offset, fmt,
                      BITFIELD_MASK(nir_dest_num_components(instr->dest)), 0);
   agx_wait(b, 0);

   agx_emit_split(b, dests, vec, 4);
}

static agx_instr *
agx_emit_load_ubo(agx_builder *b, agx_index dst, nir_intrinsic_instr *instr)
{
   bool kernel_input = (instr->intrinsic == nir_intrinsic_load_kernel_input);
   nir_src *offset = nir_get_io_offset_src(instr);

   if (!kernel_input && !nir_src_is_const(instr->src[0]))
      unreachable("todo: indirect UBO access");

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

   agx_device_load_to(b, dst, base, agx_src_index(offset),
                      agx_format_for_bits(nir_dest_bit_size(instr->dest)),
                      BITFIELD_MASK(instr->num_components), 0);
   agx_wait(b, 0);
   agx_emit_cached_split(b, dst, instr->num_components);

   return NULL;
}

/*
 * Emit code to generate gl_FragCoord. The xy components are calculated from
 * special registers, whereas the zw components are interpolated varyings.
 * Because interpolating varyings requires allocating coefficient registers that
 * might not be used, we only emit code for components that are actually used.
 */
static void
agx_emit_load_frag_coord(agx_builder *b, agx_index *dests, nir_intrinsic_instr *instr)
{
   u_foreach_bit(i, nir_ssa_def_components_read(&instr->dest.ssa)) {
      if (i < 2) {
         dests[i] = agx_fadd(b, agx_convert(b, agx_immediate(AGX_CONVERT_U32_TO_F),
                  agx_get_sr(b, 32, AGX_SR_THREAD_POSITION_IN_GRID_X + i),
                  AGX_ROUND_RTE), agx_immediate_f(0.5f));
      } else {
         agx_index cf = agx_get_cf(b->shader, true, false, VARYING_SLOT_POS, i, 1);
         dests[i] = agx_iter(b, cf, agx_null(), 1, false);
      }
   }
}

static agx_instr *
agx_blend_const(agx_builder *b, agx_index dst, unsigned comp)
{
     agx_index val = agx_indexed_sysval(b->shader,
           AGX_PUSH_BLEND_CONST, AGX_SIZE_32, comp * 2, 4 * 2);

     return agx_mov_to(b, dst, val);
}

/*
 * Demoting a helper invocation is logically equivalent to zeroing the sample
 * mask. Metal implement discard as such.
 *
 * XXX: Actually, Metal's "discard" is a demote, and what is implemented here
 * is a demote. There might be a better way to implement this to get correct
 * helper invocation semantics. For now, I'm kicking the can down the road.
 */
static agx_instr *
agx_emit_discard(agx_builder *b, nir_intrinsic_instr *instr)
{
   assert(!b->shader->key->fs.ignore_tib_dependencies && "invalid usage");
   agx_writeout(b, 0xC200);
   agx_writeout(b, 0x0001);
   b->shader->did_writeout = true;

   b->shader->out->writes_sample_mask = true;
   return agx_sample_mask(b, agx_immediate(0));
}

static agx_instr *
agx_emit_intrinsic(agx_builder *b, nir_intrinsic_instr *instr)
{
  agx_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest ?
     agx_dest_index(&instr->dest) : agx_null();
  gl_shader_stage stage = b->shader->stage;
  agx_index dests[4] = { agx_null() };

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
     agx_emit_load_vary(b, dests, instr);
     break;

  case nir_intrinsic_load_input:
     if (stage == MESA_SHADER_FRAGMENT)
        agx_emit_load_vary_flat(b, dests, instr);
     else if (stage == MESA_SHADER_VERTEX)
        agx_emit_load_attr(b, dests, instr);
     else
        unreachable("Unsupported shader stage");

     break;

  case nir_intrinsic_load_global:
  case nir_intrinsic_load_global_constant:
        agx_emit_load_global(b, dests, instr);
        break;

  case nir_intrinsic_store_output:
     if (stage == MESA_SHADER_FRAGMENT)
        return agx_emit_fragment_out(b, instr);
     else if (stage == MESA_SHADER_VERTEX)
        return agx_emit_store_vary(b, instr);
     else
        unreachable("Unsupported shader stage");

  case nir_intrinsic_load_output:
     assert(stage == MESA_SHADER_FRAGMENT);
     agx_emit_load_tile(b, dests, instr);
     break;

  case nir_intrinsic_load_ubo:
  case nir_intrinsic_load_kernel_input:
     return agx_emit_load_ubo(b, dst, instr);

  case nir_intrinsic_load_frag_coord:
     agx_emit_load_frag_coord(b, dests, instr);
     break;

  case nir_intrinsic_discard:
     return agx_emit_discard(b, instr);

  case nir_intrinsic_load_back_face_agx:
     return agx_get_sr_to(b, dst, AGX_SR_BACKFACING);

  case nir_intrinsic_load_texture_base_agx:
     return agx_mov_to(b, dst, agx_indexed_sysval(b->shader,
              AGX_PUSH_TEXTURE_BASE, AGX_SIZE_64, 0, 4));

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

  /* If we got here, there is a vector destination for the intrinsic composed
   * of separate scalars. Its components are specified separately in the dests
   * array. We need to combine them so the vector destination itself is valid.
   * If only individual components are accessed, this combine will be dead code
   * eliminated.
   */
  return agx_emit_combine_to(b, dst, 4, dests);
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

   /* We only deal with scalars, extract a single scalar if needed */
   if (comps > 1)
      return agx_emit_extract(b, idx, channel);
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
   case nir_op_umul_high: return agx_umul_high_to(b, dst, s0, s1);

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
   {
      agx_index idx[] = { s0, s1, s2, s3 };
      return agx_emit_combine_to(b, dst, 4, idx);
   }

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

static enum agx_lod_mode
agx_lod_mode_for_nir(nir_texop op)
{
   switch (op) {
   case nir_texop_tex: return AGX_LOD_MODE_AUTO_LOD;
   case nir_texop_txb: return AGX_LOD_MODE_AUTO_LOD_BIAS;
   case nir_texop_txd: return AGX_LOD_MODE_LOD_GRAD;
   case nir_texop_txl: return AGX_LOD_MODE_LOD_MIN;
   case nir_texop_txf: return AGX_LOD_MODE_LOD_MIN;
   default: unreachable("Unhandled texture op");
   }
}

static void
agx_emit_tex(agx_builder *b, nir_tex_instr *instr)
{
   switch (instr->op) {
   case nir_texop_tex:
   case nir_texop_txf:
   case nir_texop_txl:
   case nir_texop_txb:
   case nir_texop_txd:
      break;
   default:
      unreachable("Unhandled texture op");
   }

   agx_index coords = agx_null(),
             texture = agx_immediate(instr->texture_index),
             sampler = agx_immediate(instr->sampler_index),
             lod = agx_immediate(0),
             compare = agx_null(),
             packed_offset = agx_null();

   bool txf = instr->op == nir_texop_txf;

   for (unsigned i = 0; i < instr->num_srcs; ++i) {
      agx_index index = agx_src_index(&instr->src[i].src);

      switch (instr->src[i].src_type) {
      case nir_tex_src_coord:
         coords = index;

         /* Array textures are indexed by a floating-point in NIR, but by an
          * integer in AGX. Convert the array index from float-to-int for array
          * textures. The array index is the last source in NIR. The conversion
          * is according to the rule from 8.9 ("Texture Functions") of the GLSL
          * ES 3.20 specification:
          *
          *     max(0, min(d - 1, floor(layer + 0.5))) =
          *     max(0, min(d - 1, f32_to_u32(layer + 0.5))) =
          *     min(d - 1, f32_to_u32(layer + 0.5))
          *
          * For txf, the coordinates are already integers, so we only need to
          * clamp (not convert).
          */
         if (instr->is_array) {
            unsigned nr = nir_src_num_components(instr->src[i].src);
            agx_index channels[4] = {};

            for (unsigned i = 0; i < nr; ++i)
               channels[i] = agx_emit_extract(b, index, i);

            agx_index d1 = agx_indexed_sysval(b->shader,
                  AGX_PUSH_ARRAY_SIZE_MINUS_1, AGX_SIZE_16,
                  instr->texture_index, 1);

            agx_index layer = channels[nr - 1];

            if (!txf) {
               layer = agx_fadd(b, channels[nr - 1], agx_immediate_f(0.5f));

               layer = agx_convert(b, agx_immediate(AGX_CONVERT_F_TO_U32), layer,
                                      AGX_ROUND_RTZ);
            }

            agx_index layer16 = agx_temp(b->shader, AGX_SIZE_16);
            agx_mov_to(b, layer16, layer);

            layer = agx_icmpsel(b, layer16, d1, layer16, d1, AGX_ICOND_ULT);

            agx_index layer32 = agx_temp(b->shader, AGX_SIZE_32);
            agx_mov_to(b, layer32, layer);

            channels[nr - 1] = layer32;
            coords = agx_vec4(b, channels[0], channels[1], channels[2], channels[3]);
         } else {
            coords = index;
         }

         break;

      case nir_tex_src_lod:
      case nir_tex_src_bias:
         lod = index;
         break;

      case nir_tex_src_comparator:
         assert(index.size == AGX_SIZE_32);
         compare = index;
         break;

      case nir_tex_src_offset:
      {
         assert(instr->src[i].src.is_ssa);
         nir_ssa_def *def = instr->src[i].src.ssa;
         uint32_t packed = 0;

         for (unsigned c = 0; c < def->num_components; ++c) {
            nir_ssa_scalar s = nir_ssa_scalar_resolved(def, c);
            assert(nir_ssa_scalar_is_const(s) && "no nonconstant offsets");

            int32_t val = nir_ssa_scalar_as_uint(s);
            assert((val >= -8 && val <= 7) && "out of bounds offset");

            packed |= (val & 0xF) << (4 * c);
         }

         packed_offset = agx_mov_imm(b, 32, packed);
         break;
      }

      case nir_tex_src_ddx:
      {
         int y_idx = nir_tex_instr_src_index(instr, nir_tex_src_ddy);
         assert(y_idx >= 0 && "we only handle gradients");

         unsigned n = nir_tex_instr_src_size(instr, y_idx);
         assert((n == 2 || n == 3) && "other sizes not supported");

         agx_index index2 = agx_src_index(&instr->src[y_idx].src);

         /* We explicitly don't cache about the split cache for this */
         lod = agx_temp(b->shader, AGX_SIZE_32);
         agx_instr *I = agx_p_combine_to(b, lod, 2 * n);

         for (unsigned i = 0; i < n; ++i) {
            I->src[(2 * i) + 0] = agx_emit_extract(b, index, i);
            I->src[(2 * i) + 1] = agx_emit_extract(b, index2, i);
         }

         break;
      }

      case nir_tex_src_ddy:
         /* handled above */
         break;

      case nir_tex_src_ms_index:
      case nir_tex_src_texture_offset:
      case nir_tex_src_sampler_offset:
      default:
         unreachable("todo");
      }
   }

   agx_index dst = agx_dest_index(&instr->dest);

   /* Pack shadow reference value (compare) and packed offset together */
   agx_index compare_offset = agx_null();

   if (!agx_is_null(compare) && !agx_is_null(packed_offset))
      compare_offset = agx_vec2(b, compare, packed_offset);
   else if (!agx_is_null(packed_offset))
      compare_offset = packed_offset;
   else if (!agx_is_null(compare))
      compare_offset = compare;

   agx_instr *I = agx_texture_sample_to(b, dst, coords, lod, texture, sampler,
         compare_offset,
         agx_tex_dim(instr->sampler_dim, instr->is_array),
         agx_lod_mode_for_nir(instr->op),
         0xF, /* TODO: wrmask */
         0, !agx_is_null(packed_offset), !agx_is_null(compare));

   if (txf)
      I->op = AGX_OPCODE_TEXTURE_LOAD;

   agx_wait(b, 0);
   agx_emit_cached_split(b, dst, 4);
}

/*
 * Mark the logical end of the current block by emitting a p_logical_end marker.
 * Note if an unconditional jump is emitted (for instance, to break out of a
 * loop from inside an if), the block has already reached its logical end so we
 * don't re-emit p_logical_end. The validator checks this, and correct register
 * allocation depends on it.
 */
static void
agx_emit_logical_end(agx_builder *b)
{
   if (!b->shader->current_block->unconditional_jumps)
      agx_p_logical_end(b);
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

   /* Jumps must come at the end of a block */
   agx_emit_logical_end(b);
   agx_pop_exec(b, 0);

   ctx->current_block->unconditional_jumps = true;
}

static void
agx_emit_phi(agx_builder *b, nir_phi_instr *instr)
{
   agx_instr *I = agx_phi_to(b, agx_dest_index(&instr->dest));

   /* Deferred */
   I->phi = instr;
}

/* Look up the AGX block corresponding to a given NIR block. Used when
 * translating phi nodes after emitting all blocks.
 */
static agx_block *
agx_from_nir_block(agx_context *ctx, nir_block *block)
{
   return ctx->indexed_nir_blocks[block->index];
}

static void
agx_emit_phi_deferred(agx_context *ctx, agx_block *block, agx_instr *I)
{
   nir_phi_instr *phi = I->phi;

   /* Guaranteed by lower_phis_to_scalar */
   assert(phi->dest.ssa.num_components == 1);

   I->nr_srcs = exec_list_length(&phi->srcs);
   I->src = rzalloc_array(I, agx_index, I->nr_srcs);

   nir_foreach_phi_src(src, phi) {
      agx_block *pred = agx_from_nir_block(ctx, src->pred);
      unsigned i = agx_predecessor_index(block, pred);
      assert(i < I->nr_srcs);

      I->src[i] = agx_src_index(&src->src);
   }
}

static void
agx_emit_phis_deferred(agx_context *ctx)
{
   agx_foreach_block(ctx, block) {
      agx_foreach_instr_in_block(block, I) {
         if (I->op == AGX_OPCODE_PHI)
            agx_emit_phi_deferred(ctx, block, I);
      }
   }
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

   case nir_instr_type_phi:
      agx_emit_phi(b, nir_instr_as_phi(instr));
      break;

   default:
      unreachable("should've been lowered");
   }
}

static agx_block *
agx_create_block(agx_context *ctx)
{
   agx_block *blk = rzalloc(ctx, agx_block);

   util_dynarray_init(&blk->predecessors, blk);

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

   ctx->indexed_nir_blocks[block->index] = blk;

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
 * If the else is empty, we can omit the else_icmp. This happens elsewhere, as
 * an empty else block can become nonempty after RA due to phi lowering. This is
 * not usually optimal, but it's a start.
 */

static void
emit_if(agx_context *ctx, nir_if *nif)
{
   agx_block *first_block = ctx->current_block;
   agx_builder _b = agx_init_builder(ctx, agx_after_block(first_block));
   agx_index cond = agx_src_index(&nif->condition);

   agx_emit_logical_end(&_b);
   agx_if_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, true);
   ctx->loop_nesting++;

   /* Emit the two subblocks. */
   agx_block *if_block = emit_cf_list(ctx, &nif->then_list);
   agx_block *end_then = ctx->current_block;

   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
   agx_else_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, false);

   agx_block *else_block = emit_cf_list(ctx, &nif->else_list);
   agx_block *end_else = ctx->current_block;

   ctx->after_block = agx_create_block(ctx);

   agx_block_add_successor(first_block, if_block);
   agx_block_add_successor(first_block, else_block);
   agx_block_add_successor(end_then, ctx->after_block);
   agx_block_add_successor(end_else, ctx->after_block);

   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
   agx_pop_exec(&_b, 1);
   ctx->loop_nesting--;
}

static void
emit_loop(agx_context *ctx, nir_loop *nloop)
{
   /* We only track nesting within the innermost loop, so push and reset */
   unsigned pushed_nesting = ctx->loop_nesting;
   ctx->loop_nesting = 0;

   agx_block *popped_break = ctx->break_block;
   agx_block *popped_continue = ctx->continue_block;

   ctx->break_block = agx_create_block(ctx);
   ctx->continue_block = agx_create_block(ctx);

   /* Make room for break/continue nesting (TODO: skip if no divergent CF) */
   agx_builder _b = agx_init_builder(ctx, agx_after_block(ctx->current_block));
   agx_emit_logical_end(&_b);
   agx_push_exec(&_b, 2);

   /* Fallthrough to body */
   agx_block_add_successor(ctx->current_block, ctx->continue_block);

   /* Emit the body */
   ctx->after_block = ctx->continue_block;
   agx_block *start_block = emit_cf_list(ctx, &nloop->body);

   /* Fix up the nesting counter via an always true while_icmp, and branch back
    * to start of loop if any lanes are active */
   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
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

   /* Restore loop nesting (we might be inside an if inside an outer loop) */
   ctx->loop_nesting = pushed_nesting;
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
   unsigned nr_ins = 0, max_reg = 0;

   agx_foreach_instr_global(ctx, I) {
      /* Count instructions */
      nr_ins++;

      /* Count registers */
      agx_foreach_dest(I, d) {
         if (I->dest[d].type == AGX_INDEX_REGISTER) {
            max_reg = MAX2(max_reg,
                           I->dest[d].value + agx_write_registers(I, d) - 1);
         }
      }
   }

   /* TODO: Pipe through occupancy */
   unsigned nr_threads = 1;

   fprintf(stderr, "%s - %s shader: %u inst, %u bytes, %u halfregs, %u threads, "
           "%u loops, %u:%u spills:fills\n",
           ctx->nir->info.label ?: "",
           gl_shader_stage_name(ctx->stage),
           nr_ins, size, max_reg, nr_threads, ctx->loop_count,
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
agx_lower_aligned_offsets(struct nir_builder *b,
                          nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_ubo)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   unsigned bytes = nir_dest_bit_size(intr->dest) / 8;
   assert(util_is_power_of_two_or_zero(bytes) && bytes != 0);

   nir_src *offset = &intr->src[1];

   unsigned shift = util_logbase2(bytes);

   nir_ssa_def *old = nir_ssa_for_src(b, *offset, 1);
   nir_ssa_def *new = nir_ishr_imm(b, old, shift);

   nir_instr_rewrite_src_ssa(instr, offset, new);
   return true;
}

static void
agx_optimize_nir(nir_shader *nir)
{
   bool progress;

   nir_lower_idiv_options idiv_options = {
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
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, true);
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
   NIR_PASS_V(nir, nir_lower_phis_to_scalar, true);
}

/* ABI: position first, then user, then psiz */
static void
agx_remap_varyings_vs(nir_shader *nir, struct agx_varyings_vs *varyings)
{
   unsigned base = 0;

   /* Initalize to "nothing is written" */
   for (unsigned i = 0; i < ARRAY_SIZE(varyings->slots); ++i)
      varyings->slots[i] = ~0;

   assert(nir->info.outputs_written & VARYING_BIT_POS);
   varyings->slots[VARYING_SLOT_POS] = base;
   base += 4;

   nir_foreach_shader_out_variable(var, nir) {
      unsigned loc = var->data.location;

      if(loc == VARYING_SLOT_POS || loc == VARYING_SLOT_PSIZ)
         continue;

      varyings->slots[loc] = base;
      base += 4;
   }

   /* TODO: Link FP16 varyings */
   varyings->base_index_fp16 = base;

   if (nir->info.outputs_written & VARYING_BIT_PSIZ) {
      varyings->slots[VARYING_SLOT_PSIZ] = base;
      base += 1;
   }

   /* All varyings linked now */
   varyings->nr_index = base;
}

/*
 * Build a bit mask of varyings (by location) that are flatshaded. This
 * information is needed by lower_mediump_io.
 */
static uint64_t
agx_flat_varying_mask(nir_shader *nir)
{
   uint64_t mask = 0;

   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   nir_foreach_shader_in_variable(var, nir) {
      if (var->data.interpolation == INTERP_MODE_FLAT)
         mask |= BITFIELD64_BIT(var->data.location);
   }

   return mask;
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

   memset(out, 0, sizeof *out);

   if (ctx->stage == MESA_SHADER_VERTEX) {
      out->writes_psiz = nir->info.outputs_written &
         BITFIELD_BIT(VARYING_SLOT_PSIZ);
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      out->no_colour_output = !(nir->info.outputs_written >> FRAG_RESULT_DATA0);
   }

   NIR_PASS_V(nir, nir_lower_vars_to_ssa);

   /* Lower large arrays to scratch and small arrays to csel */
   NIR_PASS_V(nir, nir_lower_vars_to_scratch, nir_var_function_temp, 16,
         glsl_get_natural_size_align_bytes);
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_function_temp, ~0);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
         glsl_type_size, 0);
   if (ctx->stage == MESA_SHADER_FRAGMENT) {
      /* Interpolate varyings at fp16 and write to the tilebuffer at fp16. As an
       * exception, interpolate flat shaded at fp32. This works around a
       * hardware limitation. The resulting code (with an extra f2f16 at the end
       * if needed) matches what Metal produces.
       */
      NIR_PASS_V(nir, nir_lower_mediump_io,
            nir_var_shader_in | nir_var_shader_out,
            ~agx_flat_varying_mask(nir), false);
   }
   NIR_PASS_V(nir, nir_shader_instructions_pass,
         agx_lower_aligned_offsets,
         nir_metadata_block_index | nir_metadata_dominance, NULL);

   NIR_PASS_V(nir, nir_lower_ssbo);

   /* Varying output is scalar, other I/O is vector */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      NIR_PASS_V(nir, nir_lower_io_to_scalar, nir_var_shader_out);
   }

   nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0,
      .lower_invalid_implicit_lod = true,

      /* XXX: Metal seems to handle just like 3D txd, so why doesn't it work?
       * TODO: Stop using this lowering
       */
      .lower_txd_cube_map = true,
   };

   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_lod] = { true, 16 },
      [nir_tex_src_bias] = { true, 16 },
   };

   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS_V(nir, agx_lower_resinfo);
   NIR_PASS_V(nir, nir_legalize_16bit_sampler_srcs, tex_constraints);

   agx_optimize_nir(nir);

   /* Implement conditional discard with real control flow like Metal */
   NIR_PASS_V(nir, nir_lower_discard_if, (nir_lower_discard_if_to_cf |
                                          nir_lower_demote_if_to_cf |
                                          nir_lower_terminate_if_to_cf));

   /* Must be last since NIR passes can remap driver_location freely */
   if (ctx->stage == MESA_SHADER_VERTEX)
      agx_remap_varyings_vs(nir, &out->varyings.vs);

   bool skip_internal = nir->info.internal;
   skip_internal &= !(agx_debug & AGX_DBG_INTERNAL);

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal) {
      nir_print_shader(nir, stdout);
   }

   ctx->allocated_vec = _mesa_hash_table_u64_create(ctx);

   nir_foreach_function(func, nir) {
      if (!func->impl)
         continue;

      nir_index_blocks(func->impl);

      ctx->indexed_nir_blocks =
         rzalloc_array(ctx, agx_block *, func->impl->num_blocks);

      ctx->alloc += func->impl->ssa_alloc;
      emit_cf_list(ctx, &func->impl->body);
      agx_emit_phis_deferred(ctx);
      break; /* TODO: Multi-function shaders */
   }

   /* Terminate the shader after the exit block */
   agx_block *last_block = list_last_entry(&ctx->blocks, agx_block, link);
   agx_builder _b = agx_init_builder(ctx, agx_after_block(last_block));
   agx_stop(&_b);

   /* Also add traps to match the blob, unsure what the function is */
   for (unsigned i = 0; i < 8; ++i)
      agx_trap(&_b);

   /* Index blocks now that we're done emitting so the order is consistent */
   agx_foreach_block(ctx, block)
      block->index = ctx->num_blocks++;

   agx_validate(ctx, "IR translation");

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
      agx_print_shader(ctx, stdout);

   if (likely(!(agx_debug & AGX_DBG_NOOPT))) {
      agx_optimizer(ctx);
      agx_dce(ctx);
      agx_validate(ctx, "Optimization");

      if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
         agx_print_shader(ctx, stdout);
   }

   agx_ra(ctx);

   if (ctx->stage == MESA_SHADER_VERTEX)
      agx_set_st_vary_final(ctx);

   if (agx_debug & AGX_DBG_SHADERS && !skip_internal)
      agx_print_shader(ctx, stdout);

   agx_lower_pseudo(ctx);

   agx_pack_binary(ctx, binary);

   if ((agx_debug & AGX_DBG_SHADERDB) && !skip_internal)
      agx_print_stats(ctx, binary->size, stderr);

   ralloc_free(ctx);
}
