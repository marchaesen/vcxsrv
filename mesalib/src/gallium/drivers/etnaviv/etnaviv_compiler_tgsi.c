/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

/* TGSI->Vivante shader ISA conversion */

/* What does the compiler return (see etna_shader_object)?
 *  1) instruction data
 *  2) input-to-temporary mapping (fixed for ps)
 *      *) in case of ps, semantic -> varying id mapping
 *      *) for each varying: number of components used (r, rg, rgb, rgba)
 *  3) temporary-to-output mapping (in case of vs, fixed for ps)
 *  4) for each input/output: possible semantic (position, color, glpointcoord, ...)
 *  5) immediates base offset, immediates data
 *  6) used texture units (and possibly the TGSI_TEXTURE_* type); not needed to
 *     configure the hw, but useful for error checking
 *  7) enough information to add the z=(z+w)/2.0 necessary for older chips
 *     (output reg id is enough)
 *
 *  Empty shaders are not allowed, should always at least generate a NOP. Also
 *  if there is a label at the end of the shader, an extra NOP should be
 *  generated as jump target.
 *
 * TODO
 * * Use an instruction scheduler
 * * Indirect access to uniforms / temporaries using amode
 */

#include "etnaviv_compiler.h"

#include "etnaviv_asm.h"
#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_uniforms.h"
#include "etnaviv_util.h"

#include "nir/tgsi_to_nir.h"
#include "pipe/p_shader_tokens.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_iterate.h"
#include "tgsi/tgsi_lowering.h"
#include "tgsi/tgsi_strings.h"
#include "tgsi/tgsi_util.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#define ETNA_MAX_INNER_TEMPS 2

static const float sincos_const[2][4] = {
   {
      2., -1., 4., -4.,
   },
   {
      1. / (2. * M_PI), 0.75, 0.5, 0.0,
   },
};

/* Native register description structure */
struct etna_native_reg {
   unsigned valid : 1;
   unsigned is_tex : 1; /* is texture unit, overrides rgroup */
   unsigned rgroup : 3;
   unsigned id : 9;
};

/* Register description */
struct etna_reg_desc {
   enum tgsi_file_type file; /* IN, OUT, TEMP, ... */
   int idx; /* index into file */
   bool active; /* used in program */
   int first_use; /* instruction id of first use (scope begin) */
   int last_use; /* instruction id of last use (scope end, inclusive) */

   struct etna_native_reg native; /* native register to map to */
   unsigned usage_mask : 4; /* usage, per channel */
   bool has_semantic; /* register has associated TGSI semantic */
   struct tgsi_declaration_semantic semantic; /* TGSI semantic */
   struct tgsi_declaration_interp interp; /* Interpolation type */
};

/* Label information structure */
struct etna_compile_label {
   int inst_idx; /* Instruction id that label points to */
};

enum etna_compile_frame_type {
   ETNA_COMPILE_FRAME_IF, /* IF/ELSE/ENDIF */
   ETNA_COMPILE_FRAME_LOOP,
};

/* nesting scope frame (LOOP, IF, ...) during compilation
 */
struct etna_compile_frame {
   enum etna_compile_frame_type type;
   int lbl_else_idx;
   int lbl_endif_idx;
   int lbl_loop_bgn_idx;
   int lbl_loop_end_idx;
};

struct etna_compile_file {
   /* Number of registers in each TGSI file (max register+1) */
   size_t reg_size;
   /* Register descriptions, per register index */
   struct etna_reg_desc *reg;
};

#define array_insert(arr, val)                          \
   do {                                                 \
      if (arr##_count == arr##_sz) {                    \
         arr##_sz = MAX2(2 * arr##_sz, 16);             \
         arr = realloc(arr, arr##_sz * sizeof(arr[0])); \
      }                                                 \
      arr[arr##_count++] = val;                         \
   } while (0)


/* scratch area for compiling shader, freed after compilation finishes */
struct etna_compile {
   const struct tgsi_token *tokens;
   bool free_tokens;

   struct tgsi_shader_info info;

   /* Register descriptions, per TGSI file, per register index */
   struct etna_compile_file file[TGSI_FILE_COUNT];

   /* Keep track of TGSI register declarations */
   struct etna_reg_desc decl[ETNA_MAX_DECL];
   uint total_decls;

   /* Bitmap of dead instructions which are removed in a separate pass */
   bool dead_inst[ETNA_MAX_TOKENS];

   /* Immediate data */
   enum etna_uniform_contents imm_contents[ETNA_MAX_IMM];
   uint32_t imm_data[ETNA_MAX_IMM];
   uint32_t imm_base; /* base of immediates (in 32 bit units) */
   uint32_t imm_size; /* size of immediates (in 32 bit units) */

   /* Next free native register, for register allocation */
   uint32_t next_free_native;

   /* Temporary register for use within translated TGSI instruction,
    * only allocated when needed.
    */
   int inner_temps; /* number of inner temps used; only up to one available at
                       this point */
   struct etna_native_reg inner_temp[ETNA_MAX_INNER_TEMPS];

   /* Fields for handling nested conditionals */
   struct etna_compile_frame frame_stack[ETNA_MAX_DEPTH];
   int frame_sp;
   int lbl_usage[ETNA_MAX_INSTRUCTIONS];

   unsigned labels_count, labels_sz;
   struct etna_compile_label *labels;

   unsigned num_loops;

   /* Code generation */
   int inst_ptr; /* current instruction pointer */
   uint32_t code[ETNA_MAX_INSTRUCTIONS * ETNA_INST_SIZE];

   /* I/O */

   /* Number of varyings (PS only) */
   int num_varyings;

   /* GPU hardware specs */
   const struct etna_specs *specs;

   const struct etna_shader_key *key;
};

static struct etna_reg_desc *
etna_get_dst_reg(struct etna_compile *c, struct tgsi_dst_register dst)
{
   return &c->file[dst.File].reg[dst.Index];
}

static struct etna_reg_desc *
etna_get_src_reg(struct etna_compile *c, struct tgsi_src_register src)
{
   return &c->file[src.File].reg[src.Index];
}

static struct etna_native_reg
etna_native_temp(unsigned reg)
{
   return (struct etna_native_reg) {
      .valid = 1,
      .rgroup = INST_RGROUP_TEMP,
      .id = reg
   };
}

static struct etna_native_reg
etna_native_internal(unsigned reg)
{
   return (struct etna_native_reg) {
      .valid = 1,
      .rgroup = INST_RGROUP_INTERNAL,
      .id = reg
   };
}

/** Register allocation **/
enum reg_sort_order {
   FIRST_USE_ASC,
   FIRST_USE_DESC,
   LAST_USE_ASC,
   LAST_USE_DESC
};

/* Augmented register description for sorting */
struct sort_rec {
   struct etna_reg_desc *ptr;
   int key;
};

static int
sort_rec_compar(const struct sort_rec *a, const struct sort_rec *b)
{
   if (a->key < b->key)
      return -1;

   if (a->key > b->key)
      return 1;

   return 0;
}

/* create an index on a register set based on certain criteria. */
static int
sort_registers(struct sort_rec *sorted, struct etna_compile_file *file,
               enum reg_sort_order so)
{
   struct etna_reg_desc *regs = file->reg;
   int ptr = 0;

   /* pre-populate keys from active registers */
   for (int idx = 0; idx < file->reg_size; ++idx) {
      /* only interested in active registers now; will only assign inactive ones
       * if no space in active ones */
      if (regs[idx].active) {
         sorted[ptr].ptr = &regs[idx];

         switch (so) {
         case FIRST_USE_ASC:
            sorted[ptr].key = regs[idx].first_use;
            break;
         case LAST_USE_ASC:
            sorted[ptr].key = regs[idx].last_use;
            break;
         case FIRST_USE_DESC:
            sorted[ptr].key = -regs[idx].first_use;
            break;
         case LAST_USE_DESC:
            sorted[ptr].key = -regs[idx].last_use;
            break;
         }
         ptr++;
      }
   }

   /* sort index by key */
   qsort(sorted, ptr, sizeof(struct sort_rec),
         (int (*)(const void *, const void *))sort_rec_compar);

   return ptr;
}

/* Allocate a new, unused, native temp register */
static struct etna_native_reg
alloc_new_native_reg(struct etna_compile *c)
{
   assert(c->next_free_native < ETNA_MAX_TEMPS);
   return etna_native_temp(c->next_free_native++);
}

/* assign TEMPs to native registers */
static void
assign_temporaries_to_native(struct etna_compile *c,
                             struct etna_compile_file *file)
{
   struct etna_reg_desc *temps = file->reg;

   for (int idx = 0; idx < file->reg_size; ++idx)
      temps[idx].native = alloc_new_native_reg(c);
}

/* assign inputs and outputs to temporaries
 * Gallium assumes that the hardware has separate registers for taking input and
 * output, however Vivante GPUs use temporaries both for passing in inputs and
 * passing back outputs.
 * Try to re-use temporary registers where possible. */
static void
assign_inouts_to_temporaries(struct etna_compile *c, uint file)
{
   bool mode_inputs = (file == TGSI_FILE_INPUT);
   int inout_ptr = 0, num_inouts;
   int temp_ptr = 0, num_temps;
   struct sort_rec inout_order[ETNA_MAX_TEMPS];
   struct sort_rec temps_order[ETNA_MAX_TEMPS];
   num_inouts = sort_registers(inout_order, &c->file[file],
                               mode_inputs ? LAST_USE_ASC : FIRST_USE_ASC);
   num_temps = sort_registers(temps_order, &c->file[TGSI_FILE_TEMPORARY],
                              mode_inputs ? FIRST_USE_ASC : LAST_USE_ASC);

   while (inout_ptr < num_inouts && temp_ptr < num_temps) {
      struct etna_reg_desc *inout = inout_order[inout_ptr].ptr;
      struct etna_reg_desc *temp = temps_order[temp_ptr].ptr;

      if (!inout->active || inout->native.valid) { /* Skip if already a native register assigned */
         inout_ptr++;
         continue;
      }

      /* last usage of this input is before or in same instruction of first use
       * of temporary? */
      if (mode_inputs ? (inout->last_use <= temp->first_use)
                      : (inout->first_use >= temp->last_use)) {
         /* assign it and advance to next input */
         inout->native = temp->native;
         inout_ptr++;
      }

      temp_ptr++;
   }

   /* if we couldn't reuse current ones, allocate new temporaries */
   for (inout_ptr = 0; inout_ptr < num_inouts; ++inout_ptr) {
      struct etna_reg_desc *inout = inout_order[inout_ptr].ptr;

      if (inout->active && !inout->native.valid)
         inout->native = alloc_new_native_reg(c);
   }
}

/* Allocate an immediate with a certain value and return the index. If
 * there is already an immediate with that value, return that.
 */
static struct etna_inst_src
alloc_imm(struct etna_compile *c, enum etna_uniform_contents contents,
          uint32_t value)
{
   int idx;

   /* Could use a hash table to speed this up */
   for (idx = 0; idx < c->imm_size; ++idx) {
      if (c->imm_contents[idx] == contents && c->imm_data[idx] == value)
         break;
   }

   /* look if there is an unused slot */
   if (idx == c->imm_size) {
      for (idx = 0; idx < c->imm_size; ++idx) {
         if (c->imm_contents[idx] == ETNA_UNIFORM_UNUSED)
            break;
      }
   }

   /* allocate new immediate */
   if (idx == c->imm_size) {
      assert(c->imm_size < ETNA_MAX_IMM);
      idx = c->imm_size++;
      c->imm_data[idx] = value;
      c->imm_contents[idx] = contents;
   }

   /* swizzle so that component with value is returned in all components */
   idx += c->imm_base;
   struct etna_inst_src imm_src = {
      .use = 1,
      .rgroup = INST_RGROUP_UNIFORM_0,
      .reg = idx / 4,
      .swiz = INST_SWIZ_BROADCAST(idx & 3)
   };

   return imm_src;
}

static struct etna_inst_src
alloc_imm_u32(struct etna_compile *c, uint32_t value)
{
   return alloc_imm(c, ETNA_UNIFORM_CONSTANT, value);
}

static struct etna_inst_src
alloc_imm_vec4u(struct etna_compile *c, enum etna_uniform_contents contents,
                const uint32_t *values)
{
   struct etna_inst_src imm_src = { };
   int idx, i;

   for (idx = 0; idx + 3 < c->imm_size; idx += 4) {
      /* What if we can use a uniform with a different swizzle? */
      for (i = 0; i < 4; i++)
         if (c->imm_contents[idx + i] != contents || c->imm_data[idx + i] != values[i])
            break;
      if (i == 4)
         break;
   }

   if (idx + 3 >= c->imm_size) {
      idx = align(c->imm_size, 4);
      assert(idx + 4 <= ETNA_MAX_IMM);

      for (i = 0; i < 4; i++) {
         c->imm_data[idx + i] = values[i];
         c->imm_contents[idx + i] = contents;
      }

      c->imm_size = idx + 4;
   }

   assert((c->imm_base & 3) == 0);
   idx += c->imm_base;
   imm_src.use = 1;
   imm_src.rgroup = INST_RGROUP_UNIFORM_0;
   imm_src.reg = idx / 4;
   imm_src.swiz = INST_SWIZ_IDENTITY;

   return imm_src;
}

static uint32_t
get_imm_u32(struct etna_compile *c, const struct etna_inst_src *imm,
            unsigned swiz_idx)
{
   assert(imm->use == 1 && imm->rgroup == INST_RGROUP_UNIFORM_0);
   unsigned int idx = imm->reg * 4 + ((imm->swiz >> (swiz_idx * 2)) & 3);

   return c->imm_data[idx];
}

/* Allocate immediate with a certain float value. If there is already an
 * immediate with that value, return that.
 */
static struct etna_inst_src
alloc_imm_f32(struct etna_compile *c, float value)
{
   return alloc_imm_u32(c, fui(value));
}

static struct etna_inst_src
etna_imm_vec4f(struct etna_compile *c, const float *vec4)
{
   uint32_t val[4];

   for (int i = 0; i < 4; i++)
      val[i] = fui(vec4[i]);

   return alloc_imm_vec4u(c, ETNA_UNIFORM_CONSTANT, val);
}

/* Pass -- check register file declarations and immediates */
static void
etna_compile_parse_declarations(struct etna_compile *c)
{
   struct tgsi_parse_context ctx = { };
   ASSERTED unsigned status = tgsi_parse_init(&ctx, c->tokens);
   assert(status == TGSI_PARSE_OK);

   while (!tgsi_parse_end_of_tokens(&ctx)) {
      tgsi_parse_token(&ctx);

      switch (ctx.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_IMMEDIATE: {
         /* immediates are handled differently from other files; they are
          * not declared explicitly, and always add four components */
         const struct tgsi_full_immediate *imm = &ctx.FullToken.FullImmediate;
         assert(c->imm_size <= (ETNA_MAX_IMM - 4));

         for (int i = 0; i < 4; ++i) {
            unsigned idx = c->imm_size++;

            c->imm_data[idx] = imm->u[i].Uint;
            c->imm_contents[idx] = ETNA_UNIFORM_CONSTANT;
         }
      }
      break;
      }
   }

   tgsi_parse_free(&ctx);
}

/* Allocate register declarations for the registers in all register files */
static void
etna_allocate_decls(struct etna_compile *c)
{
   uint idx = 0;

   for (int x = 0; x < TGSI_FILE_COUNT; ++x) {
      c->file[x].reg = &c->decl[idx];
      c->file[x].reg_size = c->info.file_max[x] + 1;

      for (int sub = 0; sub < c->file[x].reg_size; ++sub) {
         c->decl[idx].file = x;
         c->decl[idx].idx = sub;
         idx++;
      }
   }

   c->total_decls = idx;
}

/* Pass -- check and record usage of temporaries, inputs, outputs */
static void
etna_compile_pass_check_usage(struct etna_compile *c)
{
   struct tgsi_parse_context ctx = { };
   ASSERTED unsigned status = tgsi_parse_init(&ctx, c->tokens);
   assert(status == TGSI_PARSE_OK);

   for (int idx = 0; idx < c->total_decls; ++idx) {
      c->decl[idx].active = false;
      c->decl[idx].first_use = c->decl[idx].last_use = -1;
   }

   int inst_idx = 0;
   while (!tgsi_parse_end_of_tokens(&ctx)) {
      tgsi_parse_token(&ctx);
      /* find out max register #s used
       * For every register mark first and last instruction index where it's
       * used this allows finding ranges where the temporary can be borrowed
       * as input and/or output register
       *
       * XXX in the case of loops this needs special care, or even be completely
       * disabled, as
       * the last usage of a register inside a loop means it can still be used
       * on next loop
       * iteration (execution is no longer * chronological). The register can
       * only be
       * declared "free" after the loop finishes.
       *
       * Same for inputs: the first usage of a register inside a loop doesn't
       * mean that the register
       * won't have been overwritten in previous iteration. The register can
       * only be declared free before the loop
       * starts.
       * The proper way would be to do full dominator / post-dominator analysis
       * (especially with more complicated
       * control flow such as direct branch instructions) but not for now...
       */
      switch (ctx.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_DECLARATION: {
         /* Declaration: fill in file details */
         const struct tgsi_full_declaration *decl = &ctx.FullToken.FullDeclaration;
         struct etna_compile_file *file = &c->file[decl->Declaration.File];

         for (int idx = decl->Range.First; idx <= decl->Range.Last; ++idx) {
            file->reg[idx].usage_mask = 0; // we'll compute this ourselves
            file->reg[idx].has_semantic = decl->Declaration.Semantic;
            file->reg[idx].semantic = decl->Semantic;
            file->reg[idx].interp = decl->Interp;
         }
      } break;
      case TGSI_TOKEN_TYPE_INSTRUCTION: {
         /* Instruction: iterate over operands of instruction */
         const struct tgsi_full_instruction *inst = &ctx.FullToken.FullInstruction;

         /* iterate over destination registers */
         for (int idx = 0; idx < inst->Instruction.NumDstRegs; ++idx) {
            struct etna_reg_desc *reg_desc = &c->file[inst->Dst[idx].Register.File].reg[inst->Dst[idx].Register.Index];

            if (reg_desc->first_use == -1)
               reg_desc->first_use = inst_idx;

            reg_desc->last_use = inst_idx;
            reg_desc->active = true;
         }

         /* iterate over source registers */
         for (int idx = 0; idx < inst->Instruction.NumSrcRegs; ++idx) {
            struct etna_reg_desc *reg_desc = &c->file[inst->Src[idx].Register.File].reg[inst->Src[idx].Register.Index];

            if (reg_desc->first_use == -1)
               reg_desc->first_use = inst_idx;

            reg_desc->last_use = inst_idx;
            reg_desc->active = true;
            /* accumulate usage mask for register, this is used to determine how
             * many slots for varyings
             * should be allocated */
            reg_desc->usage_mask |= tgsi_util_get_inst_usage_mask(inst, idx);
         }
         inst_idx += 1;
      } break;
      default:
         break;
      }
   }

   tgsi_parse_free(&ctx);
}

/* assign inputs that need to be assigned to specific registers */
static void
assign_special_inputs(struct etna_compile *c)
{
   if (c->info.processor == PIPE_SHADER_FRAGMENT) {
      /* never assign t0 as it is the position output, start assigning at t1 */
      c->next_free_native = 1;

      for (int idx = 0; idx < c->total_decls; ++idx) {
         struct etna_reg_desc *reg = &c->decl[idx];

         if (!reg->active)
            continue;

         /* hardwire TGSI_SEMANTIC_POSITION (input and output) to t0 */
         if (reg->semantic.Name == TGSI_SEMANTIC_POSITION)
            reg->native = etna_native_temp(0);

         /* hardwire TGSI_SEMANTIC_FACE to i0 */
         if (reg->semantic.Name == TGSI_SEMANTIC_FACE)
            reg->native = etna_native_internal(0);
      }
   }
}

/* Check that a move instruction does not swizzle any of the components
 * that it writes.
 */
static bool
etna_mov_check_no_swizzle(const struct tgsi_dst_register dst,
                          const struct tgsi_src_register src)
{
   return (!(dst.WriteMask & TGSI_WRITEMASK_X) || src.SwizzleX == TGSI_SWIZZLE_X) &&
          (!(dst.WriteMask & TGSI_WRITEMASK_Y) || src.SwizzleY == TGSI_SWIZZLE_Y) &&
          (!(dst.WriteMask & TGSI_WRITEMASK_Z) || src.SwizzleZ == TGSI_SWIZZLE_Z) &&
          (!(dst.WriteMask & TGSI_WRITEMASK_W) || src.SwizzleW == TGSI_SWIZZLE_W);
}

/* Pass -- optimize outputs
 * Mesa tends to generate code like this at the end if their shaders
 *   MOV OUT[1], TEMP[2]
 *   MOV OUT[0], TEMP[0]
 *   MOV OUT[2], TEMP[1]
 * Recognize if
 * a) there is only a single assignment to an output register and
 * b) the temporary is not used after that
 * Also recognize direct assignment of IN to OUT (passthrough)
 **/
static void
etna_compile_pass_optimize_outputs(struct etna_compile *c)
{
   struct tgsi_parse_context ctx = { };
   int inst_idx = 0;
   ASSERTED unsigned status = tgsi_parse_init(&ctx, c->tokens);
   assert(status == TGSI_PARSE_OK);

   while (!tgsi_parse_end_of_tokens(&ctx)) {
      tgsi_parse_token(&ctx);

      switch (ctx.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_INSTRUCTION: {
         const struct tgsi_full_instruction *inst = &ctx.FullToken.FullInstruction;

         /* iterate over operands */
         switch (inst->Instruction.Opcode) {
         case TGSI_OPCODE_MOV: {
            /* We are only interested in eliminating MOVs which write to
             * the shader outputs. Test for this early. */
            if (inst->Dst[0].Register.File != TGSI_FILE_OUTPUT)
               break;
            /* Elimination of a MOV must have no visible effect on the
             * resulting shader: this means the MOV must not swizzle or
             * saturate, and its source must not have the negate or
             * absolute modifiers. */
            if (!etna_mov_check_no_swizzle(inst->Dst[0].Register, inst->Src[0].Register) ||
                inst->Instruction.Saturate || inst->Src[0].Register.Negate ||
                inst->Src[0].Register.Absolute)
               break;

            uint out_idx = inst->Dst[0].Register.Index;
            uint in_idx = inst->Src[0].Register.Index;
            /* assignment of temporary to output --
             * and the output doesn't yet have a native register assigned
             * and the last use of the temporary is this instruction
             * and the MOV does not do a swizzle
             */
            if (inst->Src[0].Register.File == TGSI_FILE_TEMPORARY &&
                !c->file[TGSI_FILE_OUTPUT].reg[out_idx].native.valid &&
                c->file[TGSI_FILE_TEMPORARY].reg[in_idx].last_use == inst_idx) {
               c->file[TGSI_FILE_OUTPUT].reg[out_idx].native =
                  c->file[TGSI_FILE_TEMPORARY].reg[in_idx].native;
               /* prevent temp from being re-used for the rest of the shader */
               c->file[TGSI_FILE_TEMPORARY].reg[in_idx].last_use = ETNA_MAX_TOKENS;
               /* mark this MOV instruction as a no-op */
               c->dead_inst[inst_idx] = true;
            }
            /* direct assignment of input to output --
             * and the input or output doesn't yet have a native register
             * assigned
             * and the output is only used in this instruction,
             * allocate a new register, and associate both input and output to
             * it
             * and the MOV does not do a swizzle
             */
            if (inst->Src[0].Register.File == TGSI_FILE_INPUT &&
                !c->file[TGSI_FILE_INPUT].reg[in_idx].native.valid &&
                !c->file[TGSI_FILE_OUTPUT].reg[out_idx].native.valid &&
                c->file[TGSI_FILE_OUTPUT].reg[out_idx].last_use == inst_idx &&
                c->file[TGSI_FILE_OUTPUT].reg[out_idx].first_use == inst_idx) {
               c->file[TGSI_FILE_OUTPUT].reg[out_idx].native =
                  c->file[TGSI_FILE_INPUT].reg[in_idx].native =
                     alloc_new_native_reg(c);
               /* mark this MOV instruction as a no-op */
               c->dead_inst[inst_idx] = true;
            }
         } break;
         default:;
         }
         inst_idx += 1;
      } break;
      }
   }

   tgsi_parse_free(&ctx);
}

/* Get a temporary to be used within one TGSI instruction.
 * The first time that this function is called the temporary will be allocated.
 * Each call to this function will return the same temporary.
 */
static struct etna_native_reg
etna_compile_get_inner_temp(struct etna_compile *c)
{
   int inner_temp = c->inner_temps;

   if (inner_temp < ETNA_MAX_INNER_TEMPS) {
      if (!c->inner_temp[inner_temp].valid)
         c->inner_temp[inner_temp] = alloc_new_native_reg(c);

      /* alloc_new_native_reg() handles lack of registers */
      c->inner_temps += 1;
   } else {
      BUG("Too many inner temporaries (%i) requested in one instruction",
          inner_temp + 1);
   }

   return c->inner_temp[inner_temp];
}

static struct etna_inst_dst
etna_native_to_dst(struct etna_native_reg native, unsigned comps)
{
   /* Can only assign to temporaries */
   assert(native.valid && !native.is_tex && native.rgroup == INST_RGROUP_TEMP);

   struct etna_inst_dst rv = {
      .write_mask = comps,
      .use = 1,
      .reg = native.id,
   };

   return rv;
}

static struct etna_inst_src
etna_native_to_src(struct etna_native_reg native, uint32_t swizzle)
{
   assert(native.valid && !native.is_tex);

   struct etna_inst_src rv = {
      .use = 1,
      .swiz = swizzle,
      .rgroup = native.rgroup,
      .reg = native.id,
      .amode = INST_AMODE_DIRECT,
   };

   return rv;
}

static inline struct etna_inst_src
negate(struct etna_inst_src src)
{
   src.neg = !src.neg;

   return src;
}

static inline struct etna_inst_src
absolute(struct etna_inst_src src)
{
   src.abs = 1;

   return src;
}

static inline struct etna_inst_src
swizzle(struct etna_inst_src src, unsigned swizzle)
{
   src.swiz = inst_swiz_compose(src.swiz, swizzle);

   return src;
}

/* Emit instruction and append it to program */
static void
emit_inst(struct etna_compile *c, struct etna_inst *inst)
{
   assert(c->inst_ptr <= ETNA_MAX_INSTRUCTIONS);

   /* Check for uniform conflicts (each instruction can only access one
    * uniform),
    * if detected, use an intermediate temporary */
   unsigned uni_rgroup = -1;
   unsigned uni_reg = -1;

   for (int src = 0; src < ETNA_NUM_SRC; ++src) {
      if (inst->src[src].rgroup == INST_RGROUP_INTERNAL &&
          c->info.processor == PIPE_SHADER_FRAGMENT &&
          c->key->front_ccw) {
         struct etna_native_reg inner_temp = etna_compile_get_inner_temp(c);

         /*
          * Set temporary register to 0.0 or 1.0 based on the gl_FrontFacing
          * configuration (CW or CCW).
          */
         etna_assemble(&c->code[c->inst_ptr * 4], &(struct etna_inst) {
            .opcode = INST_OPCODE_SET,
            .cond = INST_CONDITION_NE,
            .dst = etna_native_to_dst(inner_temp, INST_COMPS_X | INST_COMPS_Y |
                                                  INST_COMPS_Z | INST_COMPS_W),
            .src[0] = inst->src[src],
            .src[1] = alloc_imm_f32(c, 1.0f)
         });
         c->inst_ptr++;

         /* Modify instruction to use temp register instead of uniform */
         inst->src[src].use = 1;
         inst->src[src].rgroup = INST_RGROUP_TEMP;
         inst->src[src].reg = inner_temp.id;
         inst->src[src].swiz = INST_SWIZ_IDENTITY; /* swizzling happens on MOV */
         inst->src[src].neg = 0; /* negation happens on MOV */
         inst->src[src].abs = 0; /* abs happens on MOV */
         inst->src[src].amode = 0; /* amode effects happen on MOV */
      } else if (etna_rgroup_is_uniform(inst->src[src].rgroup)) {
         if (uni_reg == -1) { /* first unique uniform used */
            uni_rgroup = inst->src[src].rgroup;
            uni_reg = inst->src[src].reg;
         } else { /* second or later; check that it is a re-use */
            if (uni_rgroup != inst->src[src].rgroup ||
                uni_reg != inst->src[src].reg) {
               DBG_F(ETNA_DBG_COMPILER_MSGS, "perf warning: instruction that "
                                             "accesses different uniforms, "
                                             "need to generate extra MOV");
               struct etna_native_reg inner_temp = etna_compile_get_inner_temp(c);

               /* Generate move instruction to temporary */
               etna_assemble(&c->code[c->inst_ptr * 4], &(struct etna_inst) {
                  .opcode = INST_OPCODE_MOV,
                  .dst = etna_native_to_dst(inner_temp, INST_COMPS_X | INST_COMPS_Y |
                                                        INST_COMPS_Z | INST_COMPS_W),
                  .src[2] = inst->src[src]
               });

               c->inst_ptr++;

               /* Modify instruction to use temp register instead of uniform */
               inst->src[src].use = 1;
               inst->src[src].rgroup = INST_RGROUP_TEMP;
               inst->src[src].reg = inner_temp.id;
               inst->src[src].swiz = INST_SWIZ_IDENTITY; /* swizzling happens on MOV */
               inst->src[src].neg = 0; /* negation happens on MOV */
               inst->src[src].abs = 0; /* abs happens on MOV */
               inst->src[src].amode = 0; /* amode effects happen on MOV */
            }
         }
      }
   }

   /* Finally assemble the actual instruction */
   etna_assemble(&c->code[c->inst_ptr * 4], inst);
   c->inst_ptr++;
}

static unsigned int
etna_amode(struct tgsi_ind_register indirect)
{
   assert(indirect.File == TGSI_FILE_ADDRESS);
   assert(indirect.Index == 0);

   switch (indirect.Swizzle) {
   case TGSI_SWIZZLE_X:
      return INST_AMODE_ADD_A_X;
   case TGSI_SWIZZLE_Y:
      return INST_AMODE_ADD_A_Y;
   case TGSI_SWIZZLE_Z:
      return INST_AMODE_ADD_A_Z;
   case TGSI_SWIZZLE_W:
      return INST_AMODE_ADD_A_W;
   default:
      assert(!"Invalid swizzle");
   }

   unreachable("bad swizzle");
}

/* convert destination operand */
static struct etna_inst_dst
convert_dst(struct etna_compile *c, const struct tgsi_full_dst_register *in)
{
   struct etna_inst_dst rv = {
      /// XXX .amode
      .write_mask = in->Register.WriteMask,
   };

   if (in->Register.File == TGSI_FILE_ADDRESS) {
      assert(in->Register.Index == 0);
      rv.reg = in->Register.Index;
      rv.use = 0;
   } else {
      rv = etna_native_to_dst(etna_get_dst_reg(c, in->Register)->native,
                              in->Register.WriteMask);
   }

   if (in->Register.Indirect)
      rv.amode = etna_amode(in->Indirect);

   return rv;
}

/* convert texture operand */
static struct etna_inst_tex
convert_tex(struct etna_compile *c, const struct tgsi_full_src_register *in,
            const struct tgsi_instruction_texture *tex)
{
   struct etna_native_reg native_reg = etna_get_src_reg(c, in->Register)->native;
   struct etna_inst_tex rv = {
      // XXX .amode (to allow for an array of samplers?)
      .swiz = INST_SWIZ_IDENTITY
   };

   assert(native_reg.is_tex && native_reg.valid);
   rv.id = native_reg.id;

   return rv;
}

/* convert source operand */
static struct etna_inst_src
etna_create_src(const struct tgsi_full_src_register *tgsi,
                const struct etna_native_reg *native)
{
   const struct tgsi_src_register *reg = &tgsi->Register;
   struct etna_inst_src rv = {
      .use = 1,
      .swiz = INST_SWIZ(reg->SwizzleX, reg->SwizzleY, reg->SwizzleZ, reg->SwizzleW),
      .neg = reg->Negate,
      .abs = reg->Absolute,
      .rgroup = native->rgroup,
      .reg = native->id,
      .amode = INST_AMODE_DIRECT,
   };

   assert(native->valid && !native->is_tex);

   if (reg->Indirect)
      rv.amode = etna_amode(tgsi->Indirect);

   return rv;
}

static struct etna_inst_src
etna_mov_src_to_temp(struct etna_compile *c, struct etna_inst_src src,
                     struct etna_native_reg temp)
{
   struct etna_inst mov = { };

   mov.opcode = INST_OPCODE_MOV;
   mov.sat = 0;
   mov.dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y |
                                      INST_COMPS_Z | INST_COMPS_W);
   mov.src[2] = src;
   emit_inst(c, &mov);

   src.swiz = INST_SWIZ_IDENTITY;
   src.neg = src.abs = 0;
   src.rgroup = temp.rgroup;
   src.reg = temp.id;

   return src;
}

static struct etna_inst_src
etna_mov_src(struct etna_compile *c, struct etna_inst_src src)
{
   struct etna_native_reg temp = etna_compile_get_inner_temp(c);

   return etna_mov_src_to_temp(c, src, temp);
}

static bool
etna_src_uniforms_conflict(struct etna_inst_src a, struct etna_inst_src b)
{
   return etna_rgroup_is_uniform(a.rgroup) &&
          etna_rgroup_is_uniform(b.rgroup) &&
          (a.rgroup != b.rgroup || a.reg != b.reg);
}

/* create a new label */
static unsigned int
alloc_new_label(struct etna_compile *c)
{
   struct etna_compile_label label = {
      .inst_idx = -1, /* start by point to no specific instruction */
   };

   array_insert(c->labels, label);

   return c->labels_count - 1;
}

/* place label at current instruction pointer */
static void
label_place(struct etna_compile *c, struct etna_compile_label *label)
{
   label->inst_idx = c->inst_ptr;
}

/* mark label use at current instruction.
 * target of the label will be filled in in the marked instruction's src2.imm
 * slot as soon
 * as the value becomes known.
 */
static void
label_mark_use(struct etna_compile *c, int lbl_idx)
{
   assert(c->inst_ptr < ETNA_MAX_INSTRUCTIONS);
   c->lbl_usage[c->inst_ptr] = lbl_idx;
}

/* walk the frame stack and return first frame with matching type */
static struct etna_compile_frame *
find_frame(struct etna_compile *c, enum etna_compile_frame_type type)
{
   for (int sp = c->frame_sp; sp >= 0; sp--)
      if (c->frame_stack[sp].type == type)
         return &c->frame_stack[sp];

   assert(0);
   return NULL;
}

struct instr_translater {
   void (*fxn)(const struct instr_translater *t, struct etna_compile *c,
               const struct tgsi_full_instruction *inst,
               struct etna_inst_src *src);
   unsigned tgsi_opc;
   uint8_t opc;

   /* tgsi src -> etna src swizzle */
   int src[3];

   unsigned cond;
};

static void
trans_instr(const struct instr_translater *t, struct etna_compile *c,
            const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   const struct tgsi_opcode_info *info = tgsi_get_opcode_info(inst->Instruction.Opcode);
   struct etna_inst instr = { };

   instr.opcode = t->opc;
   instr.cond = t->cond;
   instr.sat = inst->Instruction.Saturate;

   assert(info->num_dst <= 1);
   if (info->num_dst)
      instr.dst = convert_dst(c, &inst->Dst[0]);

   assert(info->num_src <= ETNA_NUM_SRC);

   for (unsigned i = 0; i < info->num_src; i++) {
      int swizzle = t->src[i];

      assert(swizzle != -1);
      instr.src[swizzle] = src[i];
   }

   emit_inst(c, &instr);
}

static void
trans_min_max(const struct instr_translater *t, struct etna_compile *c,
              const struct tgsi_full_instruction *inst,
              struct etna_inst_src *src)
{
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_SELECT,
       .cond = t->cond,
       .sat = inst->Instruction.Saturate,
       .dst = convert_dst(c, &inst->Dst[0]),
       .src[0] = src[0],
       .src[1] = src[1],
       .src[2] = src[0],
    });
}

static void
trans_if(const struct instr_translater *t, struct etna_compile *c,
         const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   struct etna_compile_frame *f = &c->frame_stack[c->frame_sp++];
   struct etna_inst_src imm_0 = alloc_imm_f32(c, 0.0f);

   /* push IF to stack */
   f->type = ETNA_COMPILE_FRAME_IF;
   /* create "else" label */
   f->lbl_else_idx = alloc_new_label(c);
   f->lbl_endif_idx = -1;

   /* We need to avoid the emit_inst() below becoming two instructions */
   if (etna_src_uniforms_conflict(src[0], imm_0))
      src[0] = etna_mov_src(c, src[0]);

   /* mark position in instruction stream of label reference so that it can be
    * filled in in next pass */
   label_mark_use(c, f->lbl_else_idx);

   /* create conditional branch to label if src0 EQ 0 */
   emit_inst(c, &(struct etna_inst){
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_EQ,
      .src[0] = src[0],
      .src[1] = imm_0,
    /* imm is filled in later */
   });
}

static void
trans_else(const struct instr_translater *t, struct etna_compile *c,
           const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   assert(c->frame_sp > 0);
   struct etna_compile_frame *f = &c->frame_stack[c->frame_sp - 1];
   assert(f->type == ETNA_COMPILE_FRAME_IF);

   /* create "endif" label, and branch to endif label */
   f->lbl_endif_idx = alloc_new_label(c);
   label_mark_use(c, f->lbl_endif_idx);
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_TRUE,
      /* imm is filled in later */
   });

   /* mark "else" label at this position in instruction stream */
   label_place(c, &c->labels[f->lbl_else_idx]);
}

static void
trans_endif(const struct instr_translater *t, struct etna_compile *c,
            const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   assert(c->frame_sp > 0);
   struct etna_compile_frame *f = &c->frame_stack[--c->frame_sp];
   assert(f->type == ETNA_COMPILE_FRAME_IF);

   /* assign "endif" or "else" (if no ELSE) label to current position in
    * instruction stream, pop IF */
   if (f->lbl_endif_idx != -1)
      label_place(c, &c->labels[f->lbl_endif_idx]);
   else
      label_place(c, &c->labels[f->lbl_else_idx]);
}

static void
trans_loop_bgn(const struct instr_translater *t, struct etna_compile *c,
               const struct tgsi_full_instruction *inst,
               struct etna_inst_src *src)
{
   struct etna_compile_frame *f = &c->frame_stack[c->frame_sp++];

   /* push LOOP to stack */
   f->type = ETNA_COMPILE_FRAME_LOOP;
   f->lbl_loop_bgn_idx = alloc_new_label(c);
   f->lbl_loop_end_idx = alloc_new_label(c);

   label_place(c, &c->labels[f->lbl_loop_bgn_idx]);

   c->num_loops++;
}

static void
trans_loop_end(const struct instr_translater *t, struct etna_compile *c,
               const struct tgsi_full_instruction *inst,
               struct etna_inst_src *src)
{
   assert(c->frame_sp > 0);
   struct etna_compile_frame *f = &c->frame_stack[--c->frame_sp];
   assert(f->type == ETNA_COMPILE_FRAME_LOOP);

   /* mark position in instruction stream of label reference so that it can be
    * filled in in next pass */
   label_mark_use(c, f->lbl_loop_bgn_idx);

   /* create branch to loop_bgn label */
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_TRUE,
      .src[0] = src[0],
      /* imm is filled in later */
   });

   label_place(c, &c->labels[f->lbl_loop_end_idx]);
}

static void
trans_brk(const struct instr_translater *t, struct etna_compile *c,
          const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   assert(c->frame_sp > 0);
   struct etna_compile_frame *f = find_frame(c, ETNA_COMPILE_FRAME_LOOP);

   /* mark position in instruction stream of label reference so that it can be
    * filled in in next pass */
   label_mark_use(c, f->lbl_loop_end_idx);

   /* create branch to loop_end label */
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_TRUE,
      .src[0] = src[0],
      /* imm is filled in later */
   });
}

static void
trans_cont(const struct instr_translater *t, struct etna_compile *c,
           const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   assert(c->frame_sp > 0);
   struct etna_compile_frame *f = find_frame(c, ETNA_COMPILE_FRAME_LOOP);

   /* mark position in instruction stream of label reference so that it can be
    * filled in in next pass */
   label_mark_use(c, f->lbl_loop_bgn_idx);

   /* create branch to loop_end label */
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_TRUE,
      .src[0] = src[0],
      /* imm is filled in later */
   });
}

static void
trans_deriv(const struct instr_translater *t, struct etna_compile *c,
            const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   emit_inst(c, &(struct etna_inst) {
      .opcode = t->opc,
      .sat = inst->Instruction.Saturate,
      .dst = convert_dst(c, &inst->Dst[0]),
      .src[0] = src[0],
      .src[2] = src[0],
   });
}

static void
trans_arl(const struct instr_translater *t, struct etna_compile *c,
          const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   struct etna_native_reg temp = etna_compile_get_inner_temp(c);
   struct etna_inst arl = { };
   struct etna_inst_dst dst;

   dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y | INST_COMPS_Z |
                                  INST_COMPS_W);

   if (c->specs->has_sign_floor_ceil) {
      struct etna_inst floor = { };

      floor.opcode = INST_OPCODE_FLOOR;
      floor.src[2] = src[0];
      floor.dst = dst;

      emit_inst(c, &floor);
   } else {
      struct etna_inst floor[2] = { };

      floor[0].opcode = INST_OPCODE_FRC;
      floor[0].sat = inst->Instruction.Saturate;
      floor[0].dst = dst;
      floor[0].src[2] = src[0];

      floor[1].opcode = INST_OPCODE_ADD;
      floor[1].sat = inst->Instruction.Saturate;
      floor[1].dst = dst;
      floor[1].src[0] = src[0];
      floor[1].src[2].use = 1;
      floor[1].src[2].swiz = INST_SWIZ_IDENTITY;
      floor[1].src[2].neg = 1;
      floor[1].src[2].rgroup = temp.rgroup;
      floor[1].src[2].reg = temp.id;

      emit_inst(c, &floor[0]);
      emit_inst(c, &floor[1]);
   }

   arl.opcode = INST_OPCODE_MOVAR;
   arl.sat = inst->Instruction.Saturate;
   arl.dst = convert_dst(c, &inst->Dst[0]);
   arl.src[2] = etna_native_to_src(temp, INST_SWIZ_IDENTITY);

   emit_inst(c, &arl);
}

static void
trans_lrp(const struct instr_translater *t, struct etna_compile *c,
          const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   /* dst = src0 * src1 + (1 - src0) * src2
    *     => src0 * src1 - (src0 - 1) * src2
    *     => src0 * src1 - (src0 * src2 - src2)
    * MAD tTEMP.xyzw, tSRC0.xyzw, tSRC2.xyzw, -tSRC2.xyzw
    * MAD tDST.xyzw, tSRC0.xyzw, tSRC1.xyzw, -tTEMP.xyzw
    */
   struct etna_native_reg temp = etna_compile_get_inner_temp(c);
   if (etna_src_uniforms_conflict(src[0], src[1]) ||
       etna_src_uniforms_conflict(src[0], src[2])) {
      src[0] = etna_mov_src(c, src[0]);
   }

   struct etna_inst mad[2] = { };
   mad[0].opcode = INST_OPCODE_MAD;
   mad[0].sat = 0;
   mad[0].dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y |
                                         INST_COMPS_Z | INST_COMPS_W);
   mad[0].src[0] = src[0];
   mad[0].src[1] = src[2];
   mad[0].src[2] = negate(src[2]);
   mad[1].opcode = INST_OPCODE_MAD;
   mad[1].sat = inst->Instruction.Saturate;
   mad[1].dst = convert_dst(c, &inst->Dst[0]), mad[1].src[0] = src[0];
   mad[1].src[1] = src[1];
   mad[1].src[2] = negate(etna_native_to_src(temp, INST_SWIZ_IDENTITY));

   emit_inst(c, &mad[0]);
   emit_inst(c, &mad[1]);
}

static void
trans_lit(const struct instr_translater *t, struct etna_compile *c,
          const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   /* SELECT.LT tmp._y__, 0, src.yyyy, 0
    *  - can be eliminated if src.y is a uniform and >= 0
    * SELECT.GT tmp.___w, 128, src.wwww, 128
    * SELECT.LT tmp.___w, -128, tmp.wwww, -128
    *  - can be eliminated if src.w is a uniform and fits clamp
    * LOG tmp.x, void, void, tmp.yyyy
    * MUL tmp.x, tmp.xxxx, tmp.wwww, void
    * LITP dst, undef, src.xxxx, tmp.xxxx
    */
   struct etna_native_reg inner_temp = etna_compile_get_inner_temp(c);
   struct etna_inst_src src_y = { };

   if (!etna_rgroup_is_uniform(src[0].rgroup)) {
      src_y = etna_native_to_src(inner_temp, SWIZZLE(Y, Y, Y, Y));

      struct etna_inst ins = { };
      ins.opcode = INST_OPCODE_SELECT;
      ins.cond = INST_CONDITION_LT;
      ins.dst = etna_native_to_dst(inner_temp, INST_COMPS_Y);
      ins.src[0] = ins.src[2] = alloc_imm_f32(c, 0.0);
      ins.src[1] = swizzle(src[0], SWIZZLE(Y, Y, Y, Y));
      emit_inst(c, &ins);
   } else if (uif(get_imm_u32(c, &src[0], 1)) < 0)
      src_y = alloc_imm_f32(c, 0.0);
   else
      src_y = swizzle(src[0], SWIZZLE(Y, Y, Y, Y));

   struct etna_inst_src src_w = { };

   if (!etna_rgroup_is_uniform(src[0].rgroup)) {
      src_w = etna_native_to_src(inner_temp, SWIZZLE(W, W, W, W));

      struct etna_inst ins = { };
      ins.opcode = INST_OPCODE_SELECT;
      ins.cond = INST_CONDITION_GT;
      ins.dst = etna_native_to_dst(inner_temp, INST_COMPS_W);
      ins.src[0] = ins.src[2] = alloc_imm_f32(c, 128.);
      ins.src[1] = swizzle(src[0], SWIZZLE(W, W, W, W));
      emit_inst(c, &ins);
      ins.cond = INST_CONDITION_LT;
      ins.src[0].neg = !ins.src[0].neg;
      ins.src[2].neg = !ins.src[2].neg;
      ins.src[1] = src_w;
      emit_inst(c, &ins);
   } else if (uif(get_imm_u32(c, &src[0], 3)) < -128.)
      src_w = alloc_imm_f32(c, -128.);
   else if (uif(get_imm_u32(c, &src[0], 3)) > 128.)
      src_w = alloc_imm_f32(c, 128.);
   else
      src_w = swizzle(src[0], SWIZZLE(W, W, W, W));

   if (c->specs->has_new_transcendentals) { /* Alternative LOG sequence */
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_LOG,
         .dst = etna_native_to_dst(inner_temp, INST_COMPS_X | INST_COMPS_Y),
         .src[2] = src_y,
         .tex = { .amode=1 }, /* Unknown bit needs to be set */
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .dst = etna_native_to_dst(inner_temp, INST_COMPS_X),
         .src[0] = etna_native_to_src(inner_temp, SWIZZLE(X, X, X, X)),
         .src[1] = etna_native_to_src(inner_temp, SWIZZLE(Y, Y, Y, Y)),
      });
   } else {
      struct etna_inst ins[3] = { };
      ins[0].opcode = INST_OPCODE_LOG;
      ins[0].dst = etna_native_to_dst(inner_temp, INST_COMPS_X);
      ins[0].src[2] = src_y;

      emit_inst(c, &ins[0]);
   }
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_MUL,
      .sat = 0,
      .dst = etna_native_to_dst(inner_temp, INST_COMPS_X),
      .src[0] = etna_native_to_src(inner_temp, SWIZZLE(X, X, X, X)),
      .src[1] = src_w,
   });
   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_LITP,
      .sat = 0,
      .dst = convert_dst(c, &inst->Dst[0]),
      .src[0] = swizzle(src[0], SWIZZLE(X, X, X, X)),
      .src[1] = swizzle(src[0], SWIZZLE(X, X, X, X)),
      .src[2] = etna_native_to_src(inner_temp, SWIZZLE(X, X, X, X)),
   });
}

static void
trans_ssg(const struct instr_translater *t, struct etna_compile *c,
          const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   if (c->specs->has_sign_floor_ceil) {
      emit_inst(c, &(struct etna_inst){
         .opcode = INST_OPCODE_SIGN,
         .sat = inst->Instruction.Saturate,
         .dst = convert_dst(c, &inst->Dst[0]),
         .src[2] = src[0],
      });
   } else {
      struct etna_native_reg temp = etna_compile_get_inner_temp(c);
      struct etna_inst ins[2] = { };

      ins[0].opcode = INST_OPCODE_SET;
      ins[0].cond = INST_CONDITION_NZ;
      ins[0].dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y |
                                            INST_COMPS_Z | INST_COMPS_W);
      ins[0].src[0] = src[0];

      ins[1].opcode = INST_OPCODE_SELECT;
      ins[1].cond = INST_CONDITION_LZ;
      ins[1].sat = inst->Instruction.Saturate;
      ins[1].dst = convert_dst(c, &inst->Dst[0]);
      ins[1].src[0] = src[0];
      ins[1].src[2] = etna_native_to_src(temp, INST_SWIZ_IDENTITY);
      ins[1].src[1] = negate(ins[1].src[2]);

      emit_inst(c, &ins[0]);
      emit_inst(c, &ins[1]);
   }
}

static void
trans_trig(const struct instr_translater *t, struct etna_compile *c,
           const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   if (c->specs->has_new_transcendentals) { /* Alternative SIN/COS */
      /* On newer chips alternative SIN/COS instructions are implemented,
       * which:
       * - Need their input scaled by 1/pi instead of 2/pi
       * - Output an x and y component, which need to be multiplied to
       *   get the result
       */
      struct etna_native_reg temp = etna_compile_get_inner_temp(c); /* only using .xyz */
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_Z),
         .src[0] = src[0], /* any swizzling happens here */
         .src[1] = alloc_imm_f32(c, 1.0f / M_PI),
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = inst->Instruction.Opcode == TGSI_OPCODE_COS
                    ? INST_OPCODE_COS
                    : INST_OPCODE_SIN,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y),
         .src[2] = etna_native_to_src(temp, SWIZZLE(Z, Z, Z, Z)),
         .tex = { .amode=1 }, /* Unknown bit needs to be set */
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .sat = inst->Instruction.Saturate,
         .dst = convert_dst(c, &inst->Dst[0]),
         .src[0] = etna_native_to_src(temp, SWIZZLE(X, X, X, X)),
         .src[1] = etna_native_to_src(temp, SWIZZLE(Y, Y, Y, Y)),
      });

   } else if (c->specs->has_sin_cos_sqrt) {
      struct etna_native_reg temp = etna_compile_get_inner_temp(c);
      /* add divide by PI/2, using a temp register. GC2000
       * fails with src==dst for the trig instruction. */
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y |
                                         INST_COMPS_Z | INST_COMPS_W),
         .src[0] = src[0], /* any swizzling happens here */
         .src[1] = alloc_imm_f32(c, 2.0f / M_PI),
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = inst->Instruction.Opcode == TGSI_OPCODE_COS
                    ? INST_OPCODE_COS
                    : INST_OPCODE_SIN,
         .sat = inst->Instruction.Saturate,
         .dst = convert_dst(c, &inst->Dst[0]),
         .src[2] = etna_native_to_src(temp, INST_SWIZ_IDENTITY),
      });
   } else {
      /* Implement Nick's fast sine/cosine. Taken from:
       * http://forum.devmaster.net/t/fast-and-accurate-sine-cosine/9648
       * A=(1/2*PI 0 1/2*PI 0) B=(0.75 0 0.5 0) C=(-4 4 X X)
       *  MAD t.x_zw, src.xxxx, A, B
       *  FRC t.x_z_, void, void, t.xwzw
       *  MAD t.x_z_, t.xwzw, 2, -1
       *  MUL t._y__, t.wzww, |t.wzww|, void  (for sin/scs)
       *  DP3 t.x_z_, t.zyww, C, void         (for sin)
       *  DP3 t.__z_, t.zyww, C, void         (for scs)
       *  MUL t._y__, t.wxww, |t.wxww|, void  (for cos/scs)
       *  DP3 t.x_z_, t.xyww, C, void         (for cos)
       *  DP3 t.x___, t.xyww, C, void         (for scs)
       *  MAD t._y_w, t,xxzz, |t.xxzz|, -t.xxzz
       *  MAD dst, t.ywyw, .2225, t.xzxz
       */
      struct etna_inst *p, ins[9] = { };
      struct etna_native_reg t0 = etna_compile_get_inner_temp(c);
      struct etna_inst_src t0s = etna_native_to_src(t0, INST_SWIZ_IDENTITY);
      struct etna_inst_src sincos[3], in = src[0];
      sincos[0] = etna_imm_vec4f(c, sincos_const[0]);
      sincos[1] = etna_imm_vec4f(c, sincos_const[1]);

      /* A uniform source will cause the inner temp limit to
       * be exceeded.  Explicitly deal with that scenario.
       */
      if (etna_rgroup_is_uniform(src[0].rgroup)) {
         struct etna_inst ins = { };
         ins.opcode = INST_OPCODE_MOV;
         ins.dst = etna_native_to_dst(t0, INST_COMPS_X);
         ins.src[2] = in;
         emit_inst(c, &ins);
         in = t0s;
      }

      ins[0].opcode = INST_OPCODE_MAD;
      ins[0].dst = etna_native_to_dst(t0, INST_COMPS_X | INST_COMPS_Z | INST_COMPS_W);
      ins[0].src[0] = swizzle(in, SWIZZLE(X, X, X, X));
      ins[0].src[1] = swizzle(sincos[1], SWIZZLE(X, W, X, W)); /* 1/2*PI */
      ins[0].src[2] = swizzle(sincos[1], SWIZZLE(Y, W, Z, W)); /* 0.75, 0, 0.5, 0 */

      ins[1].opcode = INST_OPCODE_FRC;
      ins[1].dst = etna_native_to_dst(t0, INST_COMPS_X | INST_COMPS_Z);
      ins[1].src[2] = swizzle(t0s, SWIZZLE(X, W, Z, W));

      ins[2].opcode = INST_OPCODE_MAD;
      ins[2].dst = etna_native_to_dst(t0, INST_COMPS_X | INST_COMPS_Z);
      ins[2].src[0] = swizzle(t0s, SWIZZLE(X, W, Z, W));
      ins[2].src[1] = swizzle(sincos[0], SWIZZLE(X, X, X, X)); /* 2 */
      ins[2].src[2] = swizzle(sincos[0], SWIZZLE(Y, Y, Y, Y)); /* -1 */

      unsigned mul_swiz, dp3_swiz;
      if (inst->Instruction.Opcode == TGSI_OPCODE_SIN) {
         mul_swiz = SWIZZLE(W, Z, W, W);
         dp3_swiz = SWIZZLE(Z, Y, W, W);
      } else {
         mul_swiz = SWIZZLE(W, X, W, W);
         dp3_swiz = SWIZZLE(X, Y, W, W);
      }

      ins[3].opcode = INST_OPCODE_MUL;
      ins[3].dst = etna_native_to_dst(t0, INST_COMPS_Y);
      ins[3].src[0] = swizzle(t0s, mul_swiz);
      ins[3].src[1] = absolute(ins[3].src[0]);

      ins[4].opcode = INST_OPCODE_DP3;
      ins[4].dst = etna_native_to_dst(t0, INST_COMPS_X | INST_COMPS_Z);
      ins[4].src[0] = swizzle(t0s, dp3_swiz);
      ins[4].src[1] = swizzle(sincos[0], SWIZZLE(Z, W, W, W));

      p = &ins[5];
      p->opcode = INST_OPCODE_MAD;
      p->dst = etna_native_to_dst(t0, INST_COMPS_Y | INST_COMPS_W);
      p->src[0] = swizzle(t0s, SWIZZLE(X, X, Z, Z));
      p->src[1] = absolute(p->src[0]);
      p->src[2] = negate(p->src[0]);

      p++;
      p->opcode = INST_OPCODE_MAD;
      p->sat = inst->Instruction.Saturate;
      p->dst = convert_dst(c, &inst->Dst[0]),
      p->src[0] = swizzle(t0s, SWIZZLE(Y, W, Y, W));
      p->src[1] = alloc_imm_f32(c, 0.2225);
      p->src[2] = swizzle(t0s, SWIZZLE(X, Z, X, Z));

      for (int i = 0; &ins[i] <= p; i++)
         emit_inst(c, &ins[i]);
   }
}

static void
trans_lg2(const struct instr_translater *t, struct etna_compile *c,
            const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   if (c->specs->has_new_transcendentals) {
      /* On newer chips alternative LOG instruction is implemented,
       * which outputs an x and y component, which need to be multiplied to
       * get the result.
       */
      struct etna_native_reg temp = etna_compile_get_inner_temp(c); /* only using .xy */
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_LOG,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y),
         .src[2] = src[0],
         .tex = { .amode=1 }, /* Unknown bit needs to be set */
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .sat = inst->Instruction.Saturate,
         .dst = convert_dst(c, &inst->Dst[0]),
         .src[0] = etna_native_to_src(temp, SWIZZLE(X, X, X, X)),
         .src[1] = etna_native_to_src(temp, SWIZZLE(Y, Y, Y, Y)),
      });
   } else {
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_LOG,
         .sat = inst->Instruction.Saturate,
         .dst = convert_dst(c, &inst->Dst[0]),
         .src[2] = src[0],
      });
   }
}

static void
trans_sampler(const struct instr_translater *t, struct etna_compile *c,
              const struct tgsi_full_instruction *inst,
              struct etna_inst_src *src)
{
   /* There is no native support for GL texture rectangle coordinates, so
    * we have to rescale from ([0, width], [0, height]) to ([0, 1], [0, 1]). */
   if (inst->Texture.Texture == TGSI_TEXTURE_RECT) {
      uint32_t unit = inst->Src[1].Register.Index;
      struct etna_inst ins[2] = { };
      struct etna_native_reg temp = etna_compile_get_inner_temp(c);

      ins[0].opcode = INST_OPCODE_MUL;
      ins[0].dst = etna_native_to_dst(temp, INST_COMPS_X);
      ins[0].src[0] = src[0];
      ins[0].src[1] = alloc_imm(c, ETNA_UNIFORM_TEXRECT_SCALE_X, unit);

      ins[1].opcode = INST_OPCODE_MUL;
      ins[1].dst = etna_native_to_dst(temp, INST_COMPS_Y);
      ins[1].src[0] = src[0];
      ins[1].src[1] = alloc_imm(c, ETNA_UNIFORM_TEXRECT_SCALE_Y, unit);

      emit_inst(c, &ins[0]);
      emit_inst(c, &ins[1]);

      src[0] = etna_native_to_src(temp, INST_SWIZ_IDENTITY); /* temp.xyzw */
   }

   switch (inst->Instruction.Opcode) {
   case TGSI_OPCODE_TEX:
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_TEXLD,
         .sat = 0,
         .dst = convert_dst(c, &inst->Dst[0]),
         .tex = convert_tex(c, &inst->Src[1], &inst->Texture),
         .src[0] = src[0],
      });
      break;

   case TGSI_OPCODE_TXB:
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_TEXLDB,
         .sat = 0,
         .dst = convert_dst(c, &inst->Dst[0]),
         .tex = convert_tex(c, &inst->Src[1], &inst->Texture),
         .src[0] = src[0],
      });
      break;

   case TGSI_OPCODE_TXL:
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_TEXLDL,
         .sat = 0,
         .dst = convert_dst(c, &inst->Dst[0]),
         .tex = convert_tex(c, &inst->Src[1], &inst->Texture),
         .src[0] = src[0],
      });
      break;

   case TGSI_OPCODE_TXP: { /* divide src.xyz by src.w */
      struct etna_native_reg temp = etna_compile_get_inner_temp(c);

      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_RCP,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_W), /* tmp.w */
         .src[2] = swizzle(src[0], SWIZZLE(W, W, W, W)),
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MUL,
         .sat = 0,
         .dst = etna_native_to_dst(temp, INST_COMPS_X | INST_COMPS_Y |
                                         INST_COMPS_Z), /* tmp.xyz */
         .src[0] = etna_native_to_src(temp, SWIZZLE(W, W, W, W)),
         .src[1] = src[0], /* src.xyzw */
      });
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_TEXLD,
         .sat = 0,
         .dst = convert_dst(c, &inst->Dst[0]),
         .tex = convert_tex(c, &inst->Src[1], &inst->Texture),
         .src[0] = etna_native_to_src(temp, INST_SWIZ_IDENTITY), /* tmp.xyzw */
      });
   } break;

   default:
      BUG("Unhandled instruction %s",
          tgsi_get_opcode_name(inst->Instruction.Opcode));
      assert(0);
      break;
   }
}

static void
trans_dummy(const struct instr_translater *t, struct etna_compile *c,
            const struct tgsi_full_instruction *inst, struct etna_inst_src *src)
{
   /* nothing to do */
}

static const struct instr_translater translaters[TGSI_OPCODE_LAST] = {
#define INSTR(n, f, ...) \
   [TGSI_OPCODE_##n] = {.fxn = (f), .tgsi_opc = TGSI_OPCODE_##n, ##__VA_ARGS__}

   INSTR(MOV, trans_instr, .opc = INST_OPCODE_MOV, .src = {2, -1, -1}),
   INSTR(RCP, trans_instr, .opc = INST_OPCODE_RCP, .src = {2, -1, -1}),
   INSTR(RSQ, trans_instr, .opc = INST_OPCODE_RSQ, .src = {2, -1, -1}),
   INSTR(MUL, trans_instr, .opc = INST_OPCODE_MUL, .src = {0, 1, -1}),
   INSTR(ADD, trans_instr, .opc = INST_OPCODE_ADD, .src = {0, 2, -1}),
   INSTR(DP2, trans_instr, .opc = INST_OPCODE_DP2, .src = {0, 1, -1}),
   INSTR(DP3, trans_instr, .opc = INST_OPCODE_DP3, .src = {0, 1, -1}),
   INSTR(DP4, trans_instr, .opc = INST_OPCODE_DP4, .src = {0, 1, -1}),
   INSTR(DST, trans_instr, .opc = INST_OPCODE_DST, .src = {0, 1, -1}),
   INSTR(MAD, trans_instr, .opc = INST_OPCODE_MAD, .src = {0, 1, 2}),
   INSTR(EX2, trans_instr, .opc = INST_OPCODE_EXP, .src = {2, -1, -1}),
   INSTR(LG2, trans_lg2),
   INSTR(SQRT, trans_instr, .opc = INST_OPCODE_SQRT, .src = {2, -1, -1}),
   INSTR(FRC, trans_instr, .opc = INST_OPCODE_FRC, .src = {2, -1, -1}),
   INSTR(CEIL, trans_instr, .opc = INST_OPCODE_CEIL, .src = {2, -1, -1}),
   INSTR(FLR, trans_instr, .opc = INST_OPCODE_FLOOR, .src = {2, -1, -1}),
   INSTR(CMP, trans_instr, .opc = INST_OPCODE_SELECT, .src = {0, 1, 2}, .cond = INST_CONDITION_LZ),

   INSTR(KILL, trans_instr, .opc = INST_OPCODE_TEXKILL),
   INSTR(KILL_IF, trans_instr, .opc = INST_OPCODE_TEXKILL, .src = {0, -1, -1}, .cond = INST_CONDITION_LZ),

   INSTR(DDX, trans_deriv, .opc = INST_OPCODE_DSX),
   INSTR(DDY, trans_deriv, .opc = INST_OPCODE_DSY),

   INSTR(IF, trans_if),
   INSTR(ELSE, trans_else),
   INSTR(ENDIF, trans_endif),

   INSTR(BGNLOOP, trans_loop_bgn),
   INSTR(ENDLOOP, trans_loop_end),
   INSTR(BRK, trans_brk),
   INSTR(CONT, trans_cont),

   INSTR(MIN, trans_min_max, .opc = INST_OPCODE_SELECT, .cond = INST_CONDITION_GT),
   INSTR(MAX, trans_min_max, .opc = INST_OPCODE_SELECT, .cond = INST_CONDITION_LT),

   INSTR(ARL, trans_arl),
   INSTR(LRP, trans_lrp),
   INSTR(LIT, trans_lit),
   INSTR(SSG, trans_ssg),

   INSTR(SIN, trans_trig),
   INSTR(COS, trans_trig),

   INSTR(SLT, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_LT),
   INSTR(SGE, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_GE),
   INSTR(SEQ, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_EQ),
   INSTR(SGT, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_GT),
   INSTR(SLE, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_LE),
   INSTR(SNE, trans_instr, .opc = INST_OPCODE_SET, .src = {0, 1, -1}, .cond = INST_CONDITION_NE),

   INSTR(TEX, trans_sampler),
   INSTR(TXB, trans_sampler),
   INSTR(TXL, trans_sampler),
   INSTR(TXP, trans_sampler),

   INSTR(NOP, trans_dummy),
   INSTR(END, trans_dummy),
};

/* Pass -- compile instructions */
static void
etna_compile_pass_generate_code(struct etna_compile *c)
{
   struct tgsi_parse_context ctx = { };
   ASSERTED unsigned status = tgsi_parse_init(&ctx, c->tokens);
   assert(status == TGSI_PARSE_OK);

   int inst_idx = 0;
   while (!tgsi_parse_end_of_tokens(&ctx)) {
      const struct tgsi_full_instruction *inst = 0;

      /* No inner temps used yet for this instruction, clear counter */
      c->inner_temps = 0;

      tgsi_parse_token(&ctx);

      switch (ctx.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         /* iterate over operands */
         inst = &ctx.FullToken.FullInstruction;
         if (c->dead_inst[inst_idx]) { /* skip dead instructions */
            inst_idx++;
            continue;
         }

         /* Lookup the TGSI information and generate the source arguments */
         struct etna_inst_src src[ETNA_NUM_SRC];
         memset(src, 0, sizeof(src));

         const struct tgsi_opcode_info *tgsi = tgsi_get_opcode_info(inst->Instruction.Opcode);

         for (int i = 0; i < tgsi->num_src && i < ETNA_NUM_SRC; i++) {
            const struct tgsi_full_src_register *reg = &inst->Src[i];
            const struct etna_native_reg *n = &etna_get_src_reg(c, reg->Register)->native;

            if (!n->valid || n->is_tex)
               continue;

            src[i] = etna_create_src(reg, n);
         }

         const unsigned opc = inst->Instruction.Opcode;
         const struct instr_translater *t = &translaters[opc];

         if (t->fxn) {
            t->fxn(t, c, inst, src);

            inst_idx += 1;
         } else {
            BUG("Unhandled instruction %s", tgsi_get_opcode_name(opc));
            assert(0);
         }
         break;
      }
   }
   tgsi_parse_free(&ctx);
}

/* Look up register by semantic */
static struct etna_reg_desc *
find_decl_by_semantic(struct etna_compile *c, uint file, uint name, uint index)
{
   for (int idx = 0; idx < c->file[file].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[file].reg[idx];

      if (reg->semantic.Name == name && reg->semantic.Index == index)
         return reg;
   }

   return NULL; /* not found */
}

/** Add ADD and MUL instruction to bring Z/W to 0..1 if -1..1 if needed:
 * - this is a vertex shader
 * - and this is an older GPU
 */
static void
etna_compile_add_z_div_if_needed(struct etna_compile *c)
{
   if (c->info.processor == PIPE_SHADER_VERTEX && c->specs->vs_need_z_div) {
      /* find position out */
      struct etna_reg_desc *pos_reg =
         find_decl_by_semantic(c, TGSI_FILE_OUTPUT, TGSI_SEMANTIC_POSITION, 0);

      if (pos_reg != NULL) {
         /*
          * ADD tX.__z_, tX.zzzz, void, tX.wwww
          * MUL tX.__z_, tX.zzzz, 0.5, void
         */
         emit_inst(c, &(struct etna_inst) {
            .opcode = INST_OPCODE_ADD,
            .dst = etna_native_to_dst(pos_reg->native, INST_COMPS_Z),
            .src[0] = etna_native_to_src(pos_reg->native, SWIZZLE(Z, Z, Z, Z)),
            .src[2] = etna_native_to_src(pos_reg->native, SWIZZLE(W, W, W, W)),
         });
         emit_inst(c, &(struct etna_inst) {
            .opcode = INST_OPCODE_MUL,
            .dst = etna_native_to_dst(pos_reg->native, INST_COMPS_Z),
            .src[0] = etna_native_to_src(pos_reg->native, SWIZZLE(Z, Z, Z, Z)),
            .src[1] = alloc_imm_f32(c, 0.5f),
         });
      }
   }
}

static void
etna_compile_frag_rb_swap(struct etna_compile *c)
{
   if (c->info.processor == PIPE_SHADER_FRAGMENT && c->key->frag_rb_swap) {
      /* find color out */
      struct etna_reg_desc *color_reg =
         find_decl_by_semantic(c, TGSI_FILE_OUTPUT, TGSI_SEMANTIC_COLOR, 0);

      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_MOV,
         .dst = etna_native_to_dst(color_reg->native, INST_COMPS_X | INST_COMPS_Y | INST_COMPS_Z | INST_COMPS_W),
         .src[2] = etna_native_to_src(color_reg->native, SWIZZLE(Z, Y, X, W)),
      });
   }
}

/** add a NOP to the shader if
 * a) the shader is empty
 * or
 * b) there is a label at the end of the shader
 */
static void
etna_compile_add_nop_if_needed(struct etna_compile *c)
{
   bool label_at_last_inst = false;

   for (int idx = 0; idx < c->labels_count; ++idx) {
      if (c->labels[idx].inst_idx == c->inst_ptr)
         label_at_last_inst = true;

   }

   if (c->inst_ptr == 0 || label_at_last_inst)
      emit_inst(c, &(struct etna_inst){.opcode = INST_OPCODE_NOP});
}

static void
assign_uniforms(struct etna_compile_file *file, unsigned base)
{
   for (int idx = 0; idx < file->reg_size; ++idx) {
      file->reg[idx].native.valid = 1;
      file->reg[idx].native.rgroup = INST_RGROUP_UNIFORM_0;
      file->reg[idx].native.id = base + idx;
   }
}

/* Allocate CONST and IMM to native ETNA_RGROUP_UNIFORM(x).
 * CONST must be consecutive as const buffers are supposed to be consecutive,
 * and before IMM, as this is
 * more convenient because is possible for the compilation process itself to
 * generate extra
 * immediates for constants such as pi, one, zero.
 */
static void
assign_constants_and_immediates(struct etna_compile *c)
{
   assign_uniforms(&c->file[TGSI_FILE_CONSTANT], 0);
   /* immediates start after the constants */
   c->imm_base = c->file[TGSI_FILE_CONSTANT].reg_size * 4;
   assign_uniforms(&c->file[TGSI_FILE_IMMEDIATE], c->imm_base / 4);
   DBG_F(ETNA_DBG_COMPILER_MSGS, "imm base: %i size: %i", c->imm_base,
         c->imm_size);
}

/* Assign declared samplers to native texture units */
static void
assign_texture_units(struct etna_compile *c)
{
   uint tex_base = 0;

   if (c->info.processor == PIPE_SHADER_VERTEX)
      tex_base = c->specs->vertex_sampler_offset;

   for (int idx = 0; idx < c->file[TGSI_FILE_SAMPLER].reg_size; ++idx) {
      c->file[TGSI_FILE_SAMPLER].reg[idx].native.valid = 1;
      c->file[TGSI_FILE_SAMPLER].reg[idx].native.is_tex = 1; // overrides rgroup
      c->file[TGSI_FILE_SAMPLER].reg[idx].native.id = tex_base + idx;
   }
}

/* Additional pass to fill in branch targets. This pass should be last
 * as no instruction reordering or removing/addition can be done anymore
 * once the branch targets are computed.
 */
static void
etna_compile_fill_in_labels(struct etna_compile *c)
{
   for (int idx = 0; idx < c->inst_ptr; ++idx) {
      if (c->lbl_usage[idx] != -1)
         etna_assemble_set_imm(&c->code[idx * 4],
                               c->labels[c->lbl_usage[idx]].inst_idx);
   }
}

/* compare two etna_native_reg structures, return true if equal */
static bool
cmp_etna_native_reg(const struct etna_native_reg to,
                    const struct etna_native_reg from)
{
   return to.valid == from.valid && to.is_tex == from.is_tex &&
          to.rgroup == from.rgroup && to.id == from.id;
}

/* go through all declarations and swap native registers *to* and *from* */
static void
swap_native_registers(struct etna_compile *c, const struct etna_native_reg to,
                      const struct etna_native_reg from)
{
   if (cmp_etna_native_reg(from, to))
      return; /* Nothing to do */

   for (int idx = 0; idx < c->total_decls; ++idx) {
      if (cmp_etna_native_reg(c->decl[idx].native, from)) {
         c->decl[idx].native = to;
      } else if (cmp_etna_native_reg(c->decl[idx].native, to)) {
         c->decl[idx].native = from;
      }
   }
}

/* For PS we need to permute so that inputs are always in temporary 0..N-1.
 * Semantic POS is always t0. If that semantic is not used, avoid t0.
 */
static void
permute_ps_inputs(struct etna_compile *c)
{
   /* Special inputs:
    * gl_FragCoord   VARYING_SLOT_POS   TGSI_SEMANTIC_POSITION
    * gl_FrontFacing VARYING_SLOT_FACE  TGSI_SEMANTIC_FACE
    * gl_PointCoord  VARYING_SLOT_PNTC  TGSI_SEMANTIC_PCOORD
    */
   uint native_idx = 1;

   for (int idx = 0; idx < c->file[TGSI_FILE_INPUT].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[TGSI_FILE_INPUT].reg[idx];
      uint input_id;
      assert(reg->has_semantic);

      if (!reg->active ||
          reg->semantic.Name == TGSI_SEMANTIC_POSITION ||
          reg->semantic.Name == TGSI_SEMANTIC_FACE)
         continue;

      input_id = native_idx++;
      swap_native_registers(c, etna_native_temp(input_id),
                            c->file[TGSI_FILE_INPUT].reg[idx].native);
   }

   c->num_varyings = native_idx - 1;

   if (native_idx > c->next_free_native)
      c->next_free_native = native_idx;
}

static inline int sem2slot(const struct tgsi_declaration_semantic *semantic)
{
   return tgsi_varying_semantic_to_slot(semantic->Name, semantic->Index);
}

/* fill in ps inputs into shader object */
static void
fill_in_ps_inputs(struct etna_shader_variant *sobj, struct etna_compile *c)
{
   struct etna_shader_io_file *sf = &sobj->infile;

   sf->num_reg = 0;

   for (int idx = 0; idx < c->file[TGSI_FILE_INPUT].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[TGSI_FILE_INPUT].reg[idx];

      if (reg->native.id > 0) {
         assert(sf->num_reg < ETNA_NUM_INPUTS);
         sf->reg[sf->num_reg].reg = reg->native.id;
         sf->reg[sf->num_reg].slot = sem2slot(&reg->semantic);
         /* convert usage mask to number of components (*=wildcard)
          *   .r    (0..1)  -> 1 component
          *   .*g   (2..3)  -> 2 component
          *   .**b  (4..7)  -> 3 components
          *   .***a (8..15) -> 4 components
          */
         sf->reg[sf->num_reg].num_components = util_last_bit(reg->usage_mask);
         sf->num_reg++;
      }
   }

   assert(sf->num_reg == c->num_varyings);
   sobj->input_count_unk8 = 31; /* XXX what is this */
}

/* fill in output mapping for ps into shader object */
static void
fill_in_ps_outputs(struct etna_shader_variant *sobj, struct etna_compile *c)
{
   sobj->outfile.num_reg = 0;

   for (int idx = 0; idx < c->file[TGSI_FILE_OUTPUT].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[TGSI_FILE_OUTPUT].reg[idx];

      switch (reg->semantic.Name) {
      case TGSI_SEMANTIC_COLOR: /* FRAG_RESULT_COLOR */
         sobj->ps_color_out_reg = reg->native.id;
         break;
      case TGSI_SEMANTIC_POSITION: /* FRAG_RESULT_DEPTH */
         sobj->ps_depth_out_reg = reg->native.id; /* =always native reg 0, only z component should be assigned */
         break;
      default:
         assert(0); /* only outputs supported are COLOR and POSITION at the moment */
      }
   }
}

/* fill in inputs for vs into shader object */
static void
fill_in_vs_inputs(struct etna_shader_variant *sobj, struct etna_compile *c)
{
   struct etna_shader_io_file *sf = &sobj->infile;

   sf->num_reg = 0;
   for (int idx = 0; idx < c->file[TGSI_FILE_INPUT].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[TGSI_FILE_INPUT].reg[idx];
      assert(sf->num_reg < ETNA_NUM_INPUTS);

      if (!reg->native.valid)
         continue;

      /* XXX exclude inputs with special semantics such as gl_frontFacing */
      sf->reg[sf->num_reg].reg = reg->native.id;
      sf->reg[sf->num_reg].slot = sem2slot(&reg->semantic);
      sf->reg[sf->num_reg].num_components = util_last_bit(reg->usage_mask);
      sf->num_reg++;
   }

   sobj->input_count_unk8 = (sf->num_reg + 19) / 16; /* XXX what is this */
}

/* fill in outputs for vs into shader object */
static void
fill_in_vs_outputs(struct etna_shader_variant *sobj, struct etna_compile *c)
{
   struct etna_shader_io_file *sf = &sobj->outfile;

   sf->num_reg = 0;
   for (int idx = 0; idx < c->file[TGSI_FILE_OUTPUT].reg_size; ++idx) {
      struct etna_reg_desc *reg = &c->file[TGSI_FILE_OUTPUT].reg[idx];
      assert(sf->num_reg < ETNA_NUM_INPUTS);

      switch (reg->semantic.Name) {
      case TGSI_SEMANTIC_POSITION:
         sobj->vs_pos_out_reg = reg->native.id;
         break;
      case TGSI_SEMANTIC_PSIZE:
         sobj->vs_pointsize_out_reg = reg->native.id;
         break;
      default:
         sf->reg[sf->num_reg].reg = reg->native.id;
         sf->reg[sf->num_reg].slot = sem2slot(&reg->semantic);
         sf->reg[sf->num_reg].num_components = 4; // XXX reg->num_components;
         sf->num_reg++;
      }
   }

   /* fill in "mystery meat" load balancing value. This value determines how
    * work is scheduled between VS and PS
    * in the unified shader architecture. More precisely, it is determined from
    * the number of VS outputs, as well as chip-specific
    * vertex output buffer size, vertex cache size, and the number of shader
    * cores.
    *
    * XXX this is a conservative estimate, the "optimal" value is only known for
    * sure at link time because some
    * outputs may be unused and thus unmapped. Then again, in the general use
    * case with GLSL the vertex and fragment
    * shaders are linked already before submitting to Gallium, thus all outputs
    * are used.
    */
   int half_out = (c->file[TGSI_FILE_OUTPUT].reg_size + 1) / 2;
   assert(half_out);

   uint32_t b = ((20480 / (c->specs->vertex_output_buffer_size -
                           2 * half_out * c->specs->vertex_cache_size)) +
                 9) /
                10;
   uint32_t a = (b + 256 / (c->specs->shader_core_count * half_out)) / 2;
   sobj->vs_load_balancing = VIVS_VS_LOAD_BALANCING_A(MIN2(a, 255)) |
                             VIVS_VS_LOAD_BALANCING_B(MIN2(b, 255)) |
                             VIVS_VS_LOAD_BALANCING_C(0x3f) |
                             VIVS_VS_LOAD_BALANCING_D(0x0f);
}

static bool
etna_compile_check_limits(struct etna_compile *c)
{
   int max_uniforms = (c->info.processor == PIPE_SHADER_VERTEX)
                         ? c->specs->max_vs_uniforms
                         : c->specs->max_ps_uniforms;
   /* round up number of uniforms, including immediates, in units of four */
   int num_uniforms = c->imm_base / 4 + (c->imm_size + 3) / 4;

   if (!c->specs->has_icache && c->inst_ptr > c->specs->max_instructions) {
      DBG("Number of instructions (%d) exceeds maximum %d", c->inst_ptr,
          c->specs->max_instructions);
      return false;
   }

   if (c->next_free_native > c->specs->max_registers) {
      DBG("Number of registers (%d) exceeds maximum %d", c->next_free_native,
          c->specs->max_registers);
      return false;
   }

   if (num_uniforms > max_uniforms) {
      DBG("Number of uniforms (%d) exceeds maximum %d", num_uniforms,
          max_uniforms);
      return false;
   }

   if (c->num_varyings > c->specs->max_varyings) {
      DBG("Number of varyings (%d) exceeds maximum %d", c->num_varyings,
          c->specs->max_varyings);
      return false;
   }

   if (c->imm_base > c->specs->num_constants) {
      DBG("Number of constants (%d) exceeds maximum %d", c->imm_base,
          c->specs->num_constants);
   }

   return true;
}

static void
copy_uniform_state_to_shader(struct etna_compile *c, struct etna_shader_variant *sobj)
{
   uint32_t count = c->imm_base + c->imm_size;
   struct etna_shader_uniform_info *uinfo = &sobj->uniforms;

   uinfo->count = count;

   uinfo->data = malloc(count * sizeof(*c->imm_data));
   for (unsigned i = 0; i < c->imm_base; i++)
      uinfo->data[i] = i;
   memcpy(&uinfo->data[c->imm_base], c->imm_data, c->imm_size * sizeof(*c->imm_data));

   uinfo->contents = malloc(count * sizeof(*c->imm_contents));
   for (unsigned i = 0; i < c->imm_base; i++)
      uinfo->contents[i] = ETNA_UNIFORM_UNIFORM;
   memcpy(&uinfo->contents[c->imm_base], c->imm_contents, c->imm_size * sizeof(*c->imm_contents));

   etna_set_shader_uniforms_dirty_flags(sobj);
}

bool
etna_compile_shader(struct etna_shader_variant *v)
{
   if (DBG_ENABLED(ETNA_DBG_NIR))
      return etna_compile_shader_nir(v);

   /* Create scratch space that may be too large to fit on stack
    */
   bool ret;
   struct etna_compile *c;

   if (unlikely(!v))
      return false;

   const struct etna_specs *specs = v->shader->specs;

   struct tgsi_lowering_config lconfig = {
      .lower_FLR = !specs->has_sign_floor_ceil,
      .lower_CEIL = !specs->has_sign_floor_ceil,
      .lower_POW = true,
      .lower_EXP = true,
      .lower_LOG = true,
      .lower_DP2 = !specs->has_halti2_instructions,
      .lower_TRUNC = true,
   };

   c = CALLOC_STRUCT(etna_compile);
   if (!c)
      return false;

   memset(&c->lbl_usage, -1, sizeof(c->lbl_usage));

   const struct tgsi_token *tokens = v->shader->tokens;

   c->specs = specs;
   c->key = &v->key;
   c->tokens = tgsi_transform_lowering(&lconfig, tokens, &c->info);
   c->free_tokens = !!c->tokens;
   if (!c->tokens) {
      /* no lowering */
      c->tokens = tokens;
   }

   /* Build a map from gallium register to native registers for files
    * CONST, SAMP, IMM, OUT, IN, TEMP.
    * SAMP will map as-is for fragment shaders, there will be a +8 offset for
    * vertex shaders.
    */
   /* Pass one -- check register file declarations and immediates */
   etna_compile_parse_declarations(c);

   etna_allocate_decls(c);

   /* Pass two -- check usage of temporaries, inputs, outputs */
   etna_compile_pass_check_usage(c);

   assign_special_inputs(c);

   /* Assign native temp register to TEMPs */
   assign_temporaries_to_native(c, &c->file[TGSI_FILE_TEMPORARY]);

   /* optimize outputs */
   etna_compile_pass_optimize_outputs(c);

   /* assign inputs: last usage of input should be <= first usage of temp */
   /*   potential optimization case:
    *     if single MOV TEMP[y], IN[x] before which temp y is not used, and
    * after which IN[x]
    *     is not read, temp[y] can be used as input register as-is
    */
   /*   sort temporaries by first use
    *   sort inputs by last usage
    *   iterate over inputs, temporaries
    *     if last usage of input <= first usage of temp:
    *       assign input to temp
    *       advance input, temporary pointer
    *     else
    *       advance temporary pointer
    *
    *   potential problem: instruction with multiple inputs of which one is the
    * temp and the other is the input;
    *      however, as the temp is not used before this, how would this make
    * sense? uninitialized temporaries have an undefined
    *      value, so this would be ok
    */
   assign_inouts_to_temporaries(c, TGSI_FILE_INPUT);

   /* assign outputs: first usage of output should be >= last usage of temp */
   /*   potential optimization case:
    *      if single MOV OUT[x], TEMP[y] (with full write mask, or at least
    * writing all components that are used in
    *        the shader) after which temp y is no longer used temp[y] can be
    * used as output register as-is
    *
    *   potential problem: instruction with multiple outputs of which one is the
    * temp and the other is the output;
    *      however, as the temp is not used after this, how would this make
    * sense? could just discard the output value
    */
   /*   sort temporaries by last use
    *   sort outputs by first usage
    *   iterate over outputs, temporaries
    *     if first usage of output >= last usage of temp:
    *       assign output to temp
    *       advance output, temporary pointer
    *     else
    *       advance temporary pointer
    */
   assign_inouts_to_temporaries(c, TGSI_FILE_OUTPUT);

   assign_constants_and_immediates(c);
   assign_texture_units(c);

   /* list declarations */
   for (int x = 0; x < c->total_decls; ++x) {
      DBG_F(ETNA_DBG_COMPILER_MSGS, "%i: %s,%d active=%i first_use=%i "
                                    "last_use=%i native=%i usage_mask=%x "
                                    "has_semantic=%i",
            x, tgsi_file_name(c->decl[x].file), c->decl[x].idx,
            c->decl[x].active, c->decl[x].first_use, c->decl[x].last_use,
            c->decl[x].native.valid ? c->decl[x].native.id : -1,
            c->decl[x].usage_mask, c->decl[x].has_semantic);
      if (c->decl[x].has_semantic)
         DBG_F(ETNA_DBG_COMPILER_MSGS, " semantic_name=%s semantic_idx=%i",
               tgsi_semantic_names[c->decl[x].semantic.Name],
               c->decl[x].semantic.Index);
   }
   /* XXX for PS we need to permute so that inputs are always in temporary
    * 0..N-1.
    * There is no "switchboard" for varyings (AFAIK!). The output color,
    * however, can be routed
    * from an arbitrary temporary.
    */
   if (c->info.processor == PIPE_SHADER_FRAGMENT)
      permute_ps_inputs(c);


   /* list declarations */
   for (int x = 0; x < c->total_decls; ++x) {
      DBG_F(ETNA_DBG_COMPILER_MSGS, "%i: %s,%d active=%i first_use=%i "
                                    "last_use=%i native=%i usage_mask=%x "
                                    "has_semantic=%i",
            x, tgsi_file_name(c->decl[x].file), c->decl[x].idx,
            c->decl[x].active, c->decl[x].first_use, c->decl[x].last_use,
            c->decl[x].native.valid ? c->decl[x].native.id : -1,
            c->decl[x].usage_mask, c->decl[x].has_semantic);
      if (c->decl[x].has_semantic)
         DBG_F(ETNA_DBG_COMPILER_MSGS, " semantic_name=%s semantic_idx=%i",
               tgsi_semantic_names[c->decl[x].semantic.Name],
               c->decl[x].semantic.Index);
   }

   /* pass 3: generate instructions */
   etna_compile_pass_generate_code(c);
   etna_compile_add_z_div_if_needed(c);
   etna_compile_frag_rb_swap(c);
   etna_compile_add_nop_if_needed(c);

   ret = etna_compile_check_limits(c);
   if (!ret)
      goto out;

   etna_compile_fill_in_labels(c);

   /* fill in output structure */
   v->stage = c->info.processor == PIPE_SHADER_FRAGMENT ? MESA_SHADER_FRAGMENT : MESA_SHADER_VERTEX;
   v->uses_discard = c->info.uses_kill;
   v->code_size = c->inst_ptr * 4;
   v->code = mem_dup(c->code, c->inst_ptr * 16);
   v->num_loops = c->num_loops;
   v->num_temps = c->next_free_native;
   v->vs_id_in_reg = -1;
   v->vs_pos_out_reg = -1;
   v->vs_pointsize_out_reg = -1;
   v->ps_color_out_reg = -1;
   v->ps_depth_out_reg = -1;
   v->needs_icache = c->inst_ptr > c->specs->max_instructions;
   copy_uniform_state_to_shader(c, v);

   if (c->info.processor == PIPE_SHADER_VERTEX) {
      fill_in_vs_inputs(v, c);
      fill_in_vs_outputs(v, c);
   } else if (c->info.processor == PIPE_SHADER_FRAGMENT) {
      fill_in_ps_inputs(v, c);
      fill_in_ps_outputs(v, c);
   }

out:
   if (c->free_tokens)
      FREE((void *)c->tokens);

   FREE(c->labels);
   FREE(c);

   return ret;
}

static const struct etna_shader_inout *
etna_shader_vs_lookup(const struct etna_shader_variant *sobj,
                      const struct etna_shader_inout *in)
{
   for (int i = 0; i < sobj->outfile.num_reg; i++)
      if (sobj->outfile.reg[i].slot == in->slot)
         return &sobj->outfile.reg[i];

   return NULL;
}

bool
etna_link_shader(struct etna_shader_link_info *info,
                 const struct etna_shader_variant *vs, const struct etna_shader_variant *fs)
{
   int comp_ofs = 0;
   /* For each fragment input we need to find the associated vertex shader
    * output, which can be found by matching on semantic name and index. A
    * binary search could be used because the vs outputs are sorted by their
    * semantic index and grouped by semantic type by fill_in_vs_outputs.
    */
   assert(fs->infile.num_reg < ETNA_NUM_INPUTS);
   info->pcoord_varying_comp_ofs = -1;

   for (int idx = 0; idx < fs->infile.num_reg; ++idx) {
      const struct etna_shader_inout *fsio = &fs->infile.reg[idx];
      const struct etna_shader_inout *vsio = etna_shader_vs_lookup(vs, fsio);
      struct etna_varying *varying;
      bool interpolate_always = ((fsio->slot != VARYING_SLOT_COL0) &&
                                 (fsio->slot != VARYING_SLOT_COL1));

      assert(fsio->reg > 0 && fsio->reg <= ARRAY_SIZE(info->varyings));

      if (fsio->reg > info->num_varyings)
         info->num_varyings = fsio->reg;

      varying = &info->varyings[fsio->reg - 1];
      varying->num_components = fsio->num_components;

      if (!interpolate_always) /* colors affected by flat shading */
         varying->pa_attributes = 0x200;
      else /* texture coord or other bypasses flat shading */
         varying->pa_attributes = 0x2f1;

      varying->use[0] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[1] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[2] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[3] = VARYING_COMPONENT_USE_UNUSED;

      /* point coord is an input to the PS without matching VS output,
       * so it gets a varying slot without being assigned a VS register.
       */
      if (fsio->slot == VARYING_SLOT_PNTC) {
         varying->use[0] = VARYING_COMPONENT_USE_POINTCOORD_X;
         varying->use[1] = VARYING_COMPONENT_USE_POINTCOORD_Y;

         info->pcoord_varying_comp_ofs = comp_ofs;
      } else {
         if (vsio == NULL) { /* not found -- link error */
            BUG("Semantic value not found in vertex shader outputs\n");
            return true;
         }

         varying->reg = vsio->reg;
      }

      comp_ofs += varying->num_components;
   }

   assert(info->num_varyings == fs->infile.num_reg);

   return false;
}
