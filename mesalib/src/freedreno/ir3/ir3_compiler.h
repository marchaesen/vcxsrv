/*
 * Copyright (C) 2013 Rob Clark <robclark@freedesktop.org>
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_COMPILER_H_
#define IR3_COMPILER_H_

#include "util/disk_cache.h"
#include "util/log.h"

#include "freedreno_dev_info.h"

#include "ir3.h"

struct ir3_ra_reg_set;
struct ir3_shader;

struct ir3_compiler {
   struct fd_device *dev;
   const struct fd_dev_id *dev_id;
   uint8_t gen;
   uint32_t shader_count;

   struct disk_cache *disk_cache;

   /* If true, UBO accesses are assumed to be bounds-checked as defined by
    * VK_EXT_robustness2 and optimizations may have to be more conservative.
    */
   bool robust_ubo_access;

   /*
    * Configuration options for things that are handled differently on
    * different generations:
    */

   /* a4xx (and later) drops SP_FS_FLAT_SHAD_MODE_REG_* for flat-interpolate
    * so we need to use ldlv.u32 to load the varying directly:
    */
   bool flat_bypass;

   /* on a3xx, we need to add one to # of array levels:
    */
   bool levels_add_one;

   /* on a3xx, we need to scale up integer coords for isaml based
    * on LoD:
    */
   bool unminify_coords;

   /* on a3xx do txf_ms w/ isaml and scaled coords: */
   bool txf_ms_with_isaml;

   /* on a4xx, for array textures we need to add 0.5 to the array
    * index coordinate:
    */
   bool array_index_add_half;

   /* on a6xx, rewrite samgp to sequence of samgq0-3 in vertex shaders:
    */
   bool samgq_workaround;

   /* on a650, vertex shader <-> tess control io uses LDL/STL */
   bool tess_use_shared;

   /* The maximum number of constants, in vec4's, across the entire graphics
    * pipeline.
    */
   uint16_t max_const_pipeline;

   /* The maximum number of constants, in vec4's, for VS+HS+DS+GS. */
   uint16_t max_const_geom;

   /* The maximum number of constants, in vec4's, for FS. */
   uint16_t max_const_frag;

   /* A "safe" max constlen that can be applied to each shader in the
    * pipeline which we guarantee will never exceed any combined limits.
    */
   uint16_t max_const_safe;

   /* The maximum number of constants, in vec4's, for compute shaders. */
   uint16_t max_const_compute;

   /* Number of instructions that the shader's base address and length
    * (instrlen divides instruction count by this) must be aligned to.
    */
   uint32_t instr_align;

   /* on a3xx, the unit of indirect const load is higher than later gens (in
    * vec4 units):
    */
   uint32_t const_upload_unit;

   /* The base number of threads per wave. Some stages may be able to double
    * this.
    */
   uint32_t threadsize_base;

   /* On at least a6xx, waves are always launched in pairs. In calculations
    * about occupancy, we pretend that each wave pair is actually one wave,
    * which simplifies many of the calculations, but means we have to
    * multiply threadsize_base by this number.
    */
   uint32_t wave_granularity;

   /* The maximum number of simultaneous waves per core. */
   uint32_t max_waves;

   /* This is theoretical maximum number of vec4 registers that one wave of
    * the base threadsize could use. To get the actual size of the register
    * file in bytes one would need to compute:
    *
    * reg_size_vec4 * threadsize_base * wave_granularity * 16 (bytes per vec4)
    *
    * However this number is more often what we actually need. For example, a
    * max_reg more than half of this will result in a doubled threadsize
    * being impossible (because double-sized waves take up twice as many
    * registers). Also, the formula for the occupancy given a particular
    * register footprint is simpler.
    *
    * It is in vec4 units because the register file is allocated
    * with vec4 granularity, so it's in the same units as max_reg.
    */
   uint32_t reg_size_vec4;

   /* The size of local memory in bytes */
   uint32_t local_mem_size;

   /* The number of total branch stack entries, divided by wave_granularity. */
   uint32_t branchstack_size;

   /* Whether clip+cull distances are supported */
   bool has_clip_cull;

   /* Whether private memory is supported */
   bool has_pvtmem;

   /* True if 16-bit descriptors are used for both 16-bit and 32-bit access. */
   bool storage_16bit;
};

void ir3_compiler_destroy(struct ir3_compiler *compiler);
struct ir3_compiler *ir3_compiler_create(struct fd_device *dev,
                                         const struct fd_dev_id *dev_id,
                                         bool robust_ubo_access);

void ir3_disk_cache_init(struct ir3_compiler *compiler);
void ir3_disk_cache_init_shader_key(struct ir3_compiler *compiler,
                                    struct ir3_shader *shader);
bool ir3_disk_cache_retrieve(struct ir3_compiler *compiler,
                             struct ir3_shader_variant *v);
void ir3_disk_cache_store(struct ir3_compiler *compiler,
                          struct ir3_shader_variant *v);

int ir3_compile_shader_nir(struct ir3_compiler *compiler,
                           struct ir3_shader_variant *so);

/* gpu pointer size in units of 32bit registers/slots */
static inline unsigned
ir3_pointer_size(struct ir3_compiler *compiler)
{
   return fd_dev_64b(compiler->dev_id) ? 2 : 1;
}

enum ir3_shader_debug {
   IR3_DBG_SHADER_VS = BITFIELD_BIT(0),
   IR3_DBG_SHADER_TCS = BITFIELD_BIT(1),
   IR3_DBG_SHADER_TES = BITFIELD_BIT(2),
   IR3_DBG_SHADER_GS = BITFIELD_BIT(3),
   IR3_DBG_SHADER_FS = BITFIELD_BIT(4),
   IR3_DBG_SHADER_CS = BITFIELD_BIT(5),
   IR3_DBG_DISASM = BITFIELD_BIT(6),
   IR3_DBG_OPTMSGS = BITFIELD_BIT(7),
   IR3_DBG_FORCES2EN = BITFIELD_BIT(8),
   IR3_DBG_NOUBOOPT = BITFIELD_BIT(9),
   IR3_DBG_NOFP16 = BITFIELD_BIT(10),
   IR3_DBG_NOCACHE = BITFIELD_BIT(11),
   IR3_DBG_SPILLALL = BITFIELD_BIT(12),

   /* DEBUG-only options: */
   IR3_DBG_SCHEDMSGS = BITFIELD_BIT(20),
   IR3_DBG_RAMSGS = BITFIELD_BIT(21),

   /* Only used for the disk-caching logic: */
   IR3_DBG_ROBUST_UBO_ACCESS = BITFIELD_BIT(30),
};

extern enum ir3_shader_debug ir3_shader_debug;
extern const char *ir3_shader_override_path;

static inline bool
shader_debug_enabled(gl_shader_stage type)
{
   if (ir3_shader_debug & IR3_DBG_DISASM)
      return true;

   switch (type) {
   case MESA_SHADER_VERTEX:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_VS);
   case MESA_SHADER_TESS_CTRL:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_TCS);
   case MESA_SHADER_TESS_EVAL:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_TES);
   case MESA_SHADER_GEOMETRY:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_GS);
   case MESA_SHADER_FRAGMENT:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_FS);
   case MESA_SHADER_COMPUTE:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_CS);
   default:
      debug_assert(0);
      return false;
   }
}

static inline void
ir3_debug_print(struct ir3 *ir, const char *when)
{
   if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
      mesa_logi("%s:", when);
      ir3_print(ir);
   }
}

#endif /* IR3_COMPILER_H_ */
