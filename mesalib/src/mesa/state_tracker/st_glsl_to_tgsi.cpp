/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file glsl_to_tgsi.cpp
 *
 * Translate GLSL IR to TGSI.
 */

#include "st_glsl_to_tgsi.h"

#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/ir_optimization.h"
#include "compiler/glsl/program.h"

#include "main/errors.h"
#include "main/shaderobj.h"
#include "main/uniforms.h"
#include "main/shaderapi.h"
#include "main/shaderimage.h"
#include "program/prog_instruction.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_info.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "st_glsl_types.h"
#include "st_program.h"
#include "st_mesa_to_tgsi.h"
#include "st_format.h"
#include "st_nir.h"
#include "st_shader_cache.h"
#include "st_glsl_to_tgsi_temprename.h"

#include "util/hash_table.h"
#include <algorithm>

#define PROGRAM_ANY_CONST ((1 << PROGRAM_STATE_VAR) |    \
                           (1 << PROGRAM_CONSTANT) |     \
                           (1 << PROGRAM_UNIFORM))

#define MAX_GLSL_TEXTURE_OFFSET 4

static unsigned is_precise(const ir_variable *ir)
{
   if (!ir)
      return 0;
   return ir->data.precise || ir->data.invariant;
}

class variable_storage {
   DECLARE_RZALLOC_CXX_OPERATORS(variable_storage)

public:
   variable_storage(ir_variable *var, gl_register_file file, int index,
                    unsigned array_id = 0)
      : file(file), index(index), component(0), var(var), array_id(array_id)
   {
      assert(file != PROGRAM_ARRAY || array_id != 0);
   }

   gl_register_file file;
   int index;

   /* Explicit component location. This is given in terms of the GLSL-style
    * swizzles where each double is a single component, i.e. for 64-bit types
    * it can only be 0 or 1.
    */
   int component;
   ir_variable *var; /* variable that maps to this, if any */
   unsigned array_id;
};

class immediate_storage : public exec_node {
public:
   immediate_storage(gl_constant_value *values, int size32, GLenum type)
   {
      memcpy(this->values, values, size32 * sizeof(gl_constant_value));
      this->size32 = size32;
      this->type = type;
   }

   /* doubles are stored across 2 gl_constant_values */
   gl_constant_value values[4];
   int size32; /**< Number of 32-bit components (1-4) */
   GLenum type; /**< GL_DOUBLE, GL_FLOAT, GL_INT, GL_BOOL, or GL_UNSIGNED_INT */
};

static const st_src_reg undef_src = st_src_reg(PROGRAM_UNDEFINED, 0, GLSL_TYPE_ERROR);
static const st_dst_reg undef_dst = st_dst_reg(PROGRAM_UNDEFINED, SWIZZLE_NOOP, GLSL_TYPE_ERROR);

struct inout_decl {
   unsigned mesa_index;
   unsigned array_id; /* TGSI ArrayID; 1-based: 0 means not an array */
   unsigned size;
   unsigned interp_loc;
   unsigned gs_out_streams;
   enum glsl_interp_mode interp;
   enum glsl_base_type base_type;
   ubyte usage_mask; /* GLSL-style usage-mask,  i.e. single bit per double */
};

static struct inout_decl *
find_inout_array(struct inout_decl *decls, unsigned count, unsigned array_id)
{
   assert(array_id != 0);

   for (unsigned i = 0; i < count; i++) {
      struct inout_decl *decl = &decls[i];

      if (array_id == decl->array_id) {
         return decl;
      }
   }

   return NULL;
}

static enum glsl_base_type
find_array_type(struct inout_decl *decls, unsigned count, unsigned array_id)
{
   if (!array_id)
      return GLSL_TYPE_ERROR;
   struct inout_decl *decl = find_inout_array(decls, count, array_id);
   if (decl)
      return decl->base_type;
   return GLSL_TYPE_ERROR;
}

struct hwatomic_decl {
   unsigned location;
   unsigned binding;
   unsigned size;
   unsigned array_id;
};

struct glsl_to_tgsi_visitor : public ir_visitor {
public:
   glsl_to_tgsi_visitor();
   ~glsl_to_tgsi_visitor();

   struct gl_context *ctx;
   struct gl_program *prog;
   struct gl_shader_program *shader_program;
   struct gl_linked_shader *shader;
   struct gl_shader_compiler_options *options;

   int next_temp;

   unsigned *array_sizes;
   unsigned max_num_arrays;
   unsigned next_array;

   struct inout_decl inputs[4 * PIPE_MAX_SHADER_INPUTS];
   unsigned num_inputs;
   unsigned num_input_arrays;
   struct inout_decl outputs[4 * PIPE_MAX_SHADER_OUTPUTS];
   unsigned num_outputs;
   unsigned num_output_arrays;

   struct hwatomic_decl atomic_info[PIPE_MAX_HW_ATOMIC_BUFFERS];
   unsigned num_atomics;
   unsigned num_atomic_arrays;
   int num_address_regs;
   uint32_t samplers_used;
   glsl_base_type sampler_types[PIPE_MAX_SAMPLERS];
   enum tgsi_texture_type sampler_targets[PIPE_MAX_SAMPLERS];
   int images_used;
   enum tgsi_texture_type image_targets[PIPE_MAX_SHADER_IMAGES];
   enum pipe_format image_formats[PIPE_MAX_SHADER_IMAGES];
   bool indirect_addr_consts;
   int wpos_transform_const;

   bool native_integers;
   bool have_sqrt;
   bool have_fma;
   bool use_shared_memory;
   bool has_tex_txf_lz;
   bool precise;
   bool need_uarl;

   variable_storage *find_variable_storage(ir_variable *var);

   int add_constant(gl_register_file file, gl_constant_value values[8],
                    int size, GLenum datatype, uint16_t *swizzle_out);

   st_src_reg get_temp(const glsl_type *type);
   void reladdr_to_temp(ir_instruction *ir, st_src_reg *reg, int *num_reladdr);

   st_src_reg st_src_reg_for_double(double val);
   st_src_reg st_src_reg_for_float(float val);
   st_src_reg st_src_reg_for_int(int val);
   st_src_reg st_src_reg_for_int64(int64_t val);
   st_src_reg st_src_reg_for_type(enum glsl_base_type type, int val);

   /**
    * \name Visit methods
    *
    * As typical for the visitor pattern, there must be one \c visit method for
    * each concrete subclass of \c ir_instruction.  Virtual base classes within
    * the hierarchy should not have \c visit methods.
    */
   /*@{*/
   virtual void visit(ir_variable *);
   virtual void visit(ir_loop *);
   virtual void visit(ir_loop_jump *);
   virtual void visit(ir_function_signature *);
   virtual void visit(ir_function *);
   virtual void visit(ir_expression *);
   virtual void visit(ir_swizzle *);
   virtual void visit(ir_dereference_variable  *);
   virtual void visit(ir_dereference_array *);
   virtual void visit(ir_dereference_record *);
   virtual void visit(ir_assignment *);
   virtual void visit(ir_constant *);
   virtual void visit(ir_call *);
   virtual void visit(ir_return *);
   virtual void visit(ir_discard *);
   virtual void visit(ir_texture *);
   virtual void visit(ir_if *);
   virtual void visit(ir_emit_vertex *);
   virtual void visit(ir_end_primitive *);
   virtual void visit(ir_barrier *);
   /*@}*/

   void visit_expression(ir_expression *, st_src_reg *) ATTRIBUTE_NOINLINE;

   void visit_atomic_counter_intrinsic(ir_call *);
   void visit_ssbo_intrinsic(ir_call *);
   void visit_membar_intrinsic(ir_call *);
   void visit_shared_intrinsic(ir_call *);
   void visit_image_intrinsic(ir_call *);
   void visit_generic_intrinsic(ir_call *, enum tgsi_opcode op);

   st_src_reg result;

   /** List of variable_storage */
   struct hash_table *variables;

   /** List of immediate_storage */
   exec_list immediates;
   unsigned num_immediates;

   /** List of glsl_to_tgsi_instruction */
   exec_list instructions;

   glsl_to_tgsi_instruction *emit_asm(ir_instruction *ir, enum tgsi_opcode op,
                                      st_dst_reg dst = undef_dst,
                                      st_src_reg src0 = undef_src,
                                      st_src_reg src1 = undef_src,
                                      st_src_reg src2 = undef_src,
                                      st_src_reg src3 = undef_src);

   glsl_to_tgsi_instruction *emit_asm(ir_instruction *ir, enum tgsi_opcode op,
                                      st_dst_reg dst, st_dst_reg dst1,
                                      st_src_reg src0 = undef_src,
                                      st_src_reg src1 = undef_src,
                                      st_src_reg src2 = undef_src,
                                      st_src_reg src3 = undef_src);

   enum tgsi_opcode get_opcode(enum tgsi_opcode op,
                               st_dst_reg dst,
                               st_src_reg src0, st_src_reg src1);

   /**
    * Emit the correct dot-product instruction for the type of arguments
    */
   glsl_to_tgsi_instruction *emit_dp(ir_instruction *ir,
                                     st_dst_reg dst,
                                     st_src_reg src0,
                                     st_src_reg src1,
                                     unsigned elements);

   void emit_scalar(ir_instruction *ir, enum tgsi_opcode op,
                    st_dst_reg dst, st_src_reg src0);

   void emit_scalar(ir_instruction *ir, enum tgsi_opcode op,
                    st_dst_reg dst, st_src_reg src0, st_src_reg src1);

   void emit_arl(ir_instruction *ir, st_dst_reg dst, st_src_reg src0);

   void get_deref_offsets(ir_dereference *ir,
                          unsigned *array_size,
                          unsigned *base,
                          uint16_t *index,
                          st_src_reg *reladdr,
                          bool opaque);
  void calc_deref_offsets(ir_dereference *tail,
                          unsigned *array_elements,
                          uint16_t *index,
                          st_src_reg *indirect,
                          unsigned *location);
   st_src_reg canonicalize_gather_offset(st_src_reg offset);

   bool try_emit_mad(ir_expression *ir,
              int mul_operand);
   bool try_emit_mad_for_and_not(ir_expression *ir,
              int mul_operand);

   void emit_swz(ir_expression *ir);

   bool process_move_condition(ir_rvalue *ir);

   void simplify_cmp(void);

   void rename_temp_registers(struct rename_reg_pair *renames);
   void get_first_temp_read(int *first_reads);
   void get_first_temp_write(int *first_writes);
   void get_last_temp_read_first_temp_write(int *last_reads, int *first_writes);
   void get_last_temp_write(int *last_writes);

   void copy_propagate(void);
   int eliminate_dead_code(void);

   void merge_two_dsts(void);
   void merge_registers(void);
   void renumber_registers(void);

   void emit_block_mov(ir_assignment *ir, const struct glsl_type *type,
                       st_dst_reg *l, st_src_reg *r,
                       st_src_reg *cond, bool cond_swap);

   void *mem_ctx;
};

static st_dst_reg address_reg = st_dst_reg(PROGRAM_ADDRESS, WRITEMASK_X,
                                           GLSL_TYPE_FLOAT, 0);
static st_dst_reg address_reg2 = st_dst_reg(PROGRAM_ADDRESS, WRITEMASK_X,
                                            GLSL_TYPE_FLOAT, 1);
static st_dst_reg sampler_reladdr = st_dst_reg(PROGRAM_ADDRESS, WRITEMASK_X,
                                               GLSL_TYPE_FLOAT, 2);

static void
fail_link(struct gl_shader_program *prog, const char *fmt, ...)
   PRINTFLIKE(2, 3);

static void
fail_link(struct gl_shader_program *prog, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   ralloc_vasprintf_append(&prog->data->InfoLog, fmt, args);
   va_end(args);

   prog->data->LinkStatus = LINKING_FAILURE;
}

int
swizzle_for_size(int size)
{
   static const int size_swizzles[4] = {
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_X),
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Y, SWIZZLE_Y),
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_Z),
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W),
   };

   assert((size >= 1) && (size <= 4));
   return size_swizzles[size - 1];
}


glsl_to_tgsi_instruction *
glsl_to_tgsi_visitor::emit_asm(ir_instruction *ir, enum tgsi_opcode op,
                               st_dst_reg dst, st_dst_reg dst1,
                               st_src_reg src0, st_src_reg src1,
                               st_src_reg src2, st_src_reg src3)
{
   glsl_to_tgsi_instruction *inst = new(mem_ctx) glsl_to_tgsi_instruction();
   int num_reladdr = 0, i, j;
   bool dst_is_64bit[2];

   op = get_opcode(op, dst, src0, src1);

   /* If we have to do relative addressing, we want to load the ARL
    * reg directly for one of the regs, and preload the other reladdr
    * sources into temps.
    */
   num_reladdr += dst.reladdr != NULL || dst.reladdr2;
   assert(!dst1.reladdr); /* should be lowered in earlier passes */
   num_reladdr += src0.reladdr != NULL || src0.reladdr2 != NULL;
   num_reladdr += src1.reladdr != NULL || src1.reladdr2 != NULL;
   num_reladdr += src2.reladdr != NULL || src2.reladdr2 != NULL;
   num_reladdr += src3.reladdr != NULL || src3.reladdr2 != NULL;

   reladdr_to_temp(ir, &src3, &num_reladdr);
   reladdr_to_temp(ir, &src2, &num_reladdr);
   reladdr_to_temp(ir, &src1, &num_reladdr);
   reladdr_to_temp(ir, &src0, &num_reladdr);

   if (dst.reladdr || dst.reladdr2) {
      if (dst.reladdr)
         emit_arl(ir, address_reg, *dst.reladdr);
      if (dst.reladdr2)
         emit_arl(ir, address_reg2, *dst.reladdr2);
      num_reladdr--;
   }

   assert(num_reladdr == 0);

   /* inst->op has only 8 bits. */
   STATIC_ASSERT(TGSI_OPCODE_LAST <= 255);

   inst->op = op;
   inst->precise = this->precise;
   inst->info = tgsi_get_opcode_info(op);
   inst->dst[0] = dst;
   inst->dst[1] = dst1;
   inst->src[0] = src0;
   inst->src[1] = src1;
   inst->src[2] = src2;
   inst->src[3] = src3;
   inst->is_64bit_expanded = false;
   inst->ir = ir;
   inst->dead_mask = 0;
   inst->tex_offsets = NULL;
   inst->tex_offset_num_offset = 0;
   inst->saturate = 0;
   inst->tex_shadow = 0;
   /* default to float, for paths where this is not initialized
    * (since 0==UINT which is likely wrong):
    */
   inst->tex_type = GLSL_TYPE_FLOAT;

   /* Update indirect addressing status used by TGSI */
   if (dst.reladdr || dst.reladdr2) {
      switch (dst.file) {
      case PROGRAM_STATE_VAR:
      case PROGRAM_CONSTANT:
      case PROGRAM_UNIFORM:
         this->indirect_addr_consts = true;
         break;
      case PROGRAM_IMMEDIATE:
         assert(!"immediates should not have indirect addressing");
         break;
      default:
         break;
      }
   }
   else {
      for (i = 0; i < 4; i++) {
         if (inst->src[i].reladdr) {
            switch (inst->src[i].file) {
            case PROGRAM_STATE_VAR:
            case PROGRAM_CONSTANT:
            case PROGRAM_UNIFORM:
               this->indirect_addr_consts = true;
               break;
            case PROGRAM_IMMEDIATE:
               assert(!"immediates should not have indirect addressing");
               break;
            default:
               break;
            }
         }
      }
   }

   /*
    * This section contains the double processing.
    * GLSL just represents doubles as single channel values,
    * however most HW and TGSI represent doubles as pairs of register channels.
    *
    * so we have to fixup destination writemask/index and src swizzle/indexes.
    * dest writemasks need to translate from single channel write mask
    * to a dual-channel writemask, but also need to modify the index,
    * if we are touching the Z,W fields in the pre-translated writemask.
    *
    * src channels have similiar index modifications along with swizzle
    * changes to we pick the XY, ZW pairs from the correct index.
    *
    * GLSL [0].x -> TGSI [0].xy
    * GLSL [0].y -> TGSI [0].zw
    * GLSL [0].z -> TGSI [1].xy
    * GLSL [0].w -> TGSI [1].zw
    */
   for (j = 0; j < 2; j++) {
      dst_is_64bit[j] = glsl_base_type_is_64bit(inst->dst[j].type);
      if (!dst_is_64bit[j] && inst->dst[j].file == PROGRAM_OUTPUT &&
          inst->dst[j].type == GLSL_TYPE_ARRAY) {
         enum glsl_base_type type = find_array_type(this->outputs,
                                                    this->num_outputs,
                                                    inst->dst[j].array_id);
         if (glsl_base_type_is_64bit(type))
            dst_is_64bit[j] = true;
      }
   }

   if (dst_is_64bit[0] || dst_is_64bit[1] ||
       glsl_base_type_is_64bit(inst->src[0].type)) {
      glsl_to_tgsi_instruction *dinst = NULL;
      int initial_src_swz[4], initial_src_idx[4];
      int initial_dst_idx[2], initial_dst_writemask[2];
      /* select the writemask for dst0 or dst1 */
      unsigned writemask = inst->dst[1].file == PROGRAM_UNDEFINED
         ? inst->dst[0].writemask : inst->dst[1].writemask;

      /* copy out the writemask, index and swizzles for all src/dsts. */
      for (j = 0; j < 2; j++) {
         initial_dst_writemask[j] = inst->dst[j].writemask;
         initial_dst_idx[j] = inst->dst[j].index;
      }

      for (j = 0; j < 4; j++) {
         initial_src_swz[j] = inst->src[j].swizzle;
         initial_src_idx[j] = inst->src[j].index;
      }

      /*
       * scan all the components in the dst writemask
       * generate an instruction for each of them if required.
       */
      st_src_reg addr;
      while (writemask) {

         int i = u_bit_scan(&writemask);

         /* before emitting the instruction, see if we have to adjust
          * load / store address */
         if (i > 1 && (inst->op == TGSI_OPCODE_LOAD ||
                       inst->op == TGSI_OPCODE_STORE) &&
             addr.file == PROGRAM_UNDEFINED) {
            /* We have to advance the buffer address by 16 */
            addr = get_temp(glsl_type::uint_type);
            emit_asm(ir, TGSI_OPCODE_UADD, st_dst_reg(addr),
                     inst->src[0], st_src_reg_for_int(16));
         }

         /* first time use previous instruction */
         if (dinst == NULL) {
            dinst = inst;
         } else {
            /* create a new instructions for subsequent attempts */
            dinst = new(mem_ctx) glsl_to_tgsi_instruction();
            *dinst = *inst;
            dinst->next = NULL;
            dinst->prev = NULL;
         }
         this->instructions.push_tail(dinst);
         dinst->is_64bit_expanded = true;

         /* modify the destination if we are splitting */
         for (j = 0; j < 2; j++) {
            if (dst_is_64bit[j]) {
               dinst->dst[j].writemask = (i & 1) ? WRITEMASK_ZW : WRITEMASK_XY;
               dinst->dst[j].index = initial_dst_idx[j];
               if (i > 1) {
                  if (dinst->op == TGSI_OPCODE_LOAD ||
                      dinst->op == TGSI_OPCODE_STORE)
                     dinst->src[0] = addr;
                  if (dinst->op != TGSI_OPCODE_STORE)
                     dinst->dst[j].index++;
               }
            } else {
               /* if we aren't writing to a double, just get the bit of the
                * initial writemask for this channel
                */
               dinst->dst[j].writemask = initial_dst_writemask[j] & (1 << i);
            }
         }

         /* modify the src registers */
         for (j = 0; j < 4; j++) {
            int swz = GET_SWZ(initial_src_swz[j], i);

            if (glsl_base_type_is_64bit(dinst->src[j].type)) {
               dinst->src[j].index = initial_src_idx[j];
               if (swz > 1) {
                  dinst->src[j].double_reg2 = true;
                  dinst->src[j].index++;
               }

               if (swz & 1)
                  dinst->src[j].swizzle = MAKE_SWIZZLE4(SWIZZLE_Z, SWIZZLE_W,
                                                        SWIZZLE_Z, SWIZZLE_W);
               else
                  dinst->src[j].swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y,
                                                        SWIZZLE_X, SWIZZLE_Y);

            } else {
               /* some opcodes are special case in what they use as sources
                * - [FUI]2D/[UI]2I64 is a float/[u]int src0, (D)LDEXP is
                * integer src1
                */
               if (op == TGSI_OPCODE_F2D || op == TGSI_OPCODE_U2D ||
                   op == TGSI_OPCODE_I2D ||
                   op == TGSI_OPCODE_I2I64 || op == TGSI_OPCODE_U2I64 ||
                   op == TGSI_OPCODE_DLDEXP || op == TGSI_OPCODE_LDEXP ||
                   (op == TGSI_OPCODE_UCMP && dst_is_64bit[0])) {
                  dinst->src[j].swizzle = MAKE_SWIZZLE4(swz, swz, swz, swz);
               }
            }
         }
      }
      inst = dinst;
   } else {
      this->instructions.push_tail(inst);
   }


   return inst;
}

glsl_to_tgsi_instruction *
glsl_to_tgsi_visitor::emit_asm(ir_instruction *ir, enum tgsi_opcode op,
                               st_dst_reg dst,
                               st_src_reg src0, st_src_reg src1,
                               st_src_reg src2, st_src_reg src3)
{
   return emit_asm(ir, op, dst, undef_dst, src0, src1, src2, src3);
}

/**
 * Determines whether to use an integer, unsigned integer, or float opcode
 * based on the operands and input opcode, then emits the result.
 */
enum tgsi_opcode
glsl_to_tgsi_visitor::get_opcode(enum tgsi_opcode op,
                                 st_dst_reg dst,
                                 st_src_reg src0, st_src_reg src1)
{
   enum glsl_base_type type = GLSL_TYPE_FLOAT;

   if (op == TGSI_OPCODE_MOV)
       return op;

   assert(src0.type != GLSL_TYPE_ARRAY);
   assert(src0.type != GLSL_TYPE_STRUCT);
   assert(src1.type != GLSL_TYPE_ARRAY);
   assert(src1.type != GLSL_TYPE_STRUCT);

   if (is_resource_instruction(op))
      type = src1.type;
   else if (src0.type == GLSL_TYPE_INT64 || src1.type == GLSL_TYPE_INT64)
      type = GLSL_TYPE_INT64;
   else if (src0.type == GLSL_TYPE_UINT64 || src1.type == GLSL_TYPE_UINT64)
      type = GLSL_TYPE_UINT64;
   else if (src0.type == GLSL_TYPE_DOUBLE || src1.type == GLSL_TYPE_DOUBLE)
      type = GLSL_TYPE_DOUBLE;
   else if (src0.type == GLSL_TYPE_FLOAT || src1.type == GLSL_TYPE_FLOAT)
      type = GLSL_TYPE_FLOAT;
   else if (native_integers)
      type = src0.type == GLSL_TYPE_BOOL ? GLSL_TYPE_INT : src0.type;

#define case7(c, f, i, u, d, i64, ui64)             \
   case TGSI_OPCODE_##c: \
      if (type == GLSL_TYPE_UINT64)           \
         op = TGSI_OPCODE_##ui64; \
      else if (type == GLSL_TYPE_INT64)       \
         op = TGSI_OPCODE_##i64; \
      else if (type == GLSL_TYPE_DOUBLE)       \
         op = TGSI_OPCODE_##d; \
      else if (type == GLSL_TYPE_INT)       \
         op = TGSI_OPCODE_##i; \
      else if (type == GLSL_TYPE_UINT) \
         op = TGSI_OPCODE_##u; \
      else \
         op = TGSI_OPCODE_##f; \
      break;

#define casecomp(c, f, i, u, d, i64, ui64)           \
   case TGSI_OPCODE_##c: \
      if (type == GLSL_TYPE_INT64)             \
         op = TGSI_OPCODE_##i64; \
      else if (type == GLSL_TYPE_UINT64)        \
         op = TGSI_OPCODE_##ui64; \
      else if (type == GLSL_TYPE_DOUBLE)       \
         op = TGSI_OPCODE_##d; \
      else if (type == GLSL_TYPE_INT || type == GLSL_TYPE_SUBROUTINE)       \
         op = TGSI_OPCODE_##i; \
      else if (type == GLSL_TYPE_UINT) \
         op = TGSI_OPCODE_##u; \
      else if (native_integers) \
         op = TGSI_OPCODE_##f; \
      else \
         op = TGSI_OPCODE_##c; \
      break;

   switch (op) {
      /* Some instructions are initially selected without considering the type.
       * This fixes the type:
       *
       *    INIT     FLOAT SINT     UINT     DOUBLE   SINT64   UINT64
       */
      case7(ADD,     ADD,  UADD,    UADD,    DADD,    U64ADD,  U64ADD);
      case7(CEIL,    CEIL, LAST,    LAST,    DCEIL,   LAST,    LAST);
      case7(DIV,     DIV,  IDIV,    UDIV,    DDIV,    I64DIV,  U64DIV);
      case7(FMA,     FMA,  UMAD,    UMAD,    DFMA,    LAST,    LAST);
      case7(FLR,     FLR,  LAST,    LAST,    DFLR,    LAST,    LAST);
      case7(FRC,     FRC,  LAST,    LAST,    DFRAC,   LAST,    LAST);
      case7(MUL,     MUL,  UMUL,    UMUL,    DMUL,    U64MUL,  U64MUL);
      case7(MAD,     MAD,  UMAD,    UMAD,    DMAD,    LAST,    LAST);
      case7(MAX,     MAX,  IMAX,    UMAX,    DMAX,    I64MAX,  U64MAX);
      case7(MIN,     MIN,  IMIN,    UMIN,    DMIN,    I64MIN,  U64MIN);
      case7(RCP,     RCP,  LAST,    LAST,    DRCP,    LAST,    LAST);
      case7(ROUND,   ROUND,LAST,    LAST,    DROUND,  LAST,    LAST);
      case7(RSQ,     RSQ,  LAST,    LAST,    DRSQ,    LAST,    LAST);
      case7(SQRT,    SQRT, LAST,    LAST,    DSQRT,   LAST,    LAST);
      case7(SSG,     SSG,  ISSG,    ISSG,    DSSG,    I64SSG,  I64SSG);
      case7(TRUNC,   TRUNC,LAST,    LAST,    DTRUNC,  LAST,    LAST);

      case7(MOD,     LAST, MOD,     UMOD,    LAST,    I64MOD,  U64MOD);
      case7(SHL,     LAST, SHL,     SHL,     LAST,    U64SHL,  U64SHL);
      case7(IBFE,    LAST, IBFE,    UBFE,    LAST,    LAST,    LAST);
      case7(IMSB,    LAST, IMSB,    UMSB,    LAST,    LAST,    LAST);
      case7(IMUL_HI, LAST, IMUL_HI, UMUL_HI, LAST,    LAST,    LAST);
      case7(ISHR,    LAST, ISHR,    USHR,    LAST,    I64SHR,  U64SHR);
      case7(ATOMIMAX,LAST, ATOMIMAX,ATOMUMAX,LAST,    LAST,    LAST);
      case7(ATOMIMIN,LAST, ATOMIMIN,ATOMUMIN,LAST,    LAST,    LAST);

      casecomp(SEQ, FSEQ, USEQ, USEQ, DSEQ, U64SEQ, U64SEQ);
      casecomp(SNE, FSNE, USNE, USNE, DSNE, U64SNE, U64SNE);
      casecomp(SGE, FSGE, ISGE, USGE, DSGE, I64SGE, U64SGE);
      casecomp(SLT, FSLT, ISLT, USLT, DSLT, I64SLT, U64SLT);

      default:
         break;
   }

   assert(op != TGSI_OPCODE_LAST);
   return op;
}

glsl_to_tgsi_instruction *
glsl_to_tgsi_visitor::emit_dp(ir_instruction *ir,
                              st_dst_reg dst, st_src_reg src0, st_src_reg src1,
                              unsigned elements)
{
   static const enum tgsi_opcode dot_opcodes[] = {
      TGSI_OPCODE_DP2, TGSI_OPCODE_DP3, TGSI_OPCODE_DP4
   };

   return emit_asm(ir, dot_opcodes[elements - 2], dst, src0, src1);
}

/**
 * Emits TGSI scalar opcodes to produce unique answers across channels.
 *
 * Some TGSI opcodes are scalar-only, like ARB_fp/vp.  The src X
 * channel determines the result across all channels.  So to do a vec4
 * of this operation, we want to emit a scalar per source channel used
 * to produce dest channels.
 */
void
glsl_to_tgsi_visitor::emit_scalar(ir_instruction *ir, enum tgsi_opcode op,
                                  st_dst_reg dst,
                                  st_src_reg orig_src0, st_src_reg orig_src1)
{
   int i, j;
   int done_mask = ~dst.writemask;

   /* TGSI RCP is a scalar operation splatting results to all channels,
    * like ARB_fp/vp.  So emit as many RCPs as necessary to cover our
    * dst channels.
    */
   for (i = 0; i < 4; i++) {
      GLuint this_mask = (1 << i);
      st_src_reg src0 = orig_src0;
      st_src_reg src1 = orig_src1;

      if (done_mask & this_mask)
         continue;

      GLuint src0_swiz = GET_SWZ(src0.swizzle, i);
      GLuint src1_swiz = GET_SWZ(src1.swizzle, i);
      for (j = i + 1; j < 4; j++) {
         /* If there is another enabled component in the destination that is
          * derived from the same inputs, generate its value on this pass as
          * well.
          */
         if (!(done_mask & (1 << j)) &&
             GET_SWZ(src0.swizzle, j) == src0_swiz &&
             GET_SWZ(src1.swizzle, j) == src1_swiz) {
            this_mask |= (1 << j);
         }
      }
      src0.swizzle = MAKE_SWIZZLE4(src0_swiz, src0_swiz,
                                   src0_swiz, src0_swiz);
      src1.swizzle = MAKE_SWIZZLE4(src1_swiz, src1_swiz,
                                   src1_swiz, src1_swiz);

      dst.writemask = this_mask;
      emit_asm(ir, op, dst, src0, src1);
      done_mask |= this_mask;
   }
}

void
glsl_to_tgsi_visitor::emit_scalar(ir_instruction *ir, enum tgsi_opcode op,
                                  st_dst_reg dst, st_src_reg src0)
{
   st_src_reg undef = undef_src;

   undef.swizzle = SWIZZLE_XXXX;

   emit_scalar(ir, op, dst, src0, undef);
}

void
glsl_to_tgsi_visitor::emit_arl(ir_instruction *ir,
                               st_dst_reg dst, st_src_reg src0)
{
   enum tgsi_opcode op = TGSI_OPCODE_ARL;

   if (src0.type == GLSL_TYPE_INT || src0.type == GLSL_TYPE_UINT) {
      if (!this->need_uarl && src0.is_legal_tgsi_address_operand())
         return;

      op = TGSI_OPCODE_UARL;
   }

   assert(dst.file == PROGRAM_ADDRESS);
   if (dst.index >= this->num_address_regs)
      this->num_address_regs = dst.index + 1;

   emit_asm(NULL, op, dst, src0);
}

int
glsl_to_tgsi_visitor::add_constant(gl_register_file file,
                                   gl_constant_value values[8], int size,
                                   GLenum datatype,
                                   uint16_t *swizzle_out)
{
   if (file == PROGRAM_CONSTANT) {
      GLuint swizzle = swizzle_out ? *swizzle_out : 0;
      int result = _mesa_add_typed_unnamed_constant(this->prog->Parameters,
                                                    values, size, datatype,
                                                    &swizzle);
      if (swizzle_out)
         *swizzle_out = swizzle;
      return result;
   }

   assert(file == PROGRAM_IMMEDIATE);

   int index = 0;
   immediate_storage *entry;
   int size32 = size * ((datatype == GL_DOUBLE ||
                         datatype == GL_INT64_ARB ||
                         datatype == GL_UNSIGNED_INT64_ARB) ? 2 : 1);
   int i;

   /* Search immediate storage to see if we already have an identical
    * immediate that we can use instead of adding a duplicate entry.
    */
   foreach_in_list(immediate_storage, entry, &this->immediates) {
      immediate_storage *tmp = entry;

      for (i = 0; i * 4 < size32; i++) {
         int slot_size = MIN2(size32 - (i * 4), 4);
         if (tmp->type != datatype || tmp->size32 != slot_size)
            break;
         if (memcmp(tmp->values, &values[i * 4],
                    slot_size * sizeof(gl_constant_value)))
            break;

         /* Everything matches, keep going until the full size is matched */
         tmp = (immediate_storage *)tmp->next;
      }

      /* The full value matched */
      if (i * 4 >= size32)
         return index;

      index++;
   }

   for (i = 0; i * 4 < size32; i++) {
      int slot_size = MIN2(size32 - (i * 4), 4);
      /* Add this immediate to the list. */
      entry = new(mem_ctx) immediate_storage(&values[i * 4],
                                             slot_size, datatype);
      this->immediates.push_tail(entry);
      this->num_immediates++;
   }
   return index;
}

st_src_reg
glsl_to_tgsi_visitor::st_src_reg_for_float(float val)
{
   st_src_reg src(PROGRAM_IMMEDIATE, -1, GLSL_TYPE_FLOAT);
   union gl_constant_value uval;

   uval.f = val;
   src.index = add_constant(src.file, &uval, 1, GL_FLOAT, &src.swizzle);

   return src;
}

st_src_reg
glsl_to_tgsi_visitor::st_src_reg_for_double(double val)
{
   st_src_reg src(PROGRAM_IMMEDIATE, -1, GLSL_TYPE_DOUBLE);
   union gl_constant_value uval[2];

   memcpy(uval, &val, sizeof(uval));
   src.index = add_constant(src.file, uval, 1, GL_DOUBLE, &src.swizzle);
   src.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_X, SWIZZLE_Y);
   return src;
}

st_src_reg
glsl_to_tgsi_visitor::st_src_reg_for_int(int val)
{
   st_src_reg src(PROGRAM_IMMEDIATE, -1, GLSL_TYPE_INT);
   union gl_constant_value uval;

   assert(native_integers);

   uval.i = val;
   src.index = add_constant(src.file, &uval, 1, GL_INT, &src.swizzle);

   return src;
}

st_src_reg
glsl_to_tgsi_visitor::st_src_reg_for_int64(int64_t val)
{
   st_src_reg src(PROGRAM_IMMEDIATE, -1, GLSL_TYPE_INT64);
   union gl_constant_value uval[2];

   memcpy(uval, &val, sizeof(uval));
   src.index = add_constant(src.file, uval, 1, GL_DOUBLE, &src.swizzle);
   src.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_X, SWIZZLE_Y);

   return src;
}

st_src_reg
glsl_to_tgsi_visitor::st_src_reg_for_type(enum glsl_base_type type, int val)
{
   if (native_integers)
      return type == GLSL_TYPE_FLOAT ? st_src_reg_for_float(val) :
                                       st_src_reg_for_int(val);
   else
      return st_src_reg_for_float(val);
}

static int
attrib_type_size(const struct glsl_type *type, bool is_vs_input)
{
   return type->count_attribute_slots(is_vs_input);
}

static int
type_size(const struct glsl_type *type)
{
   return type->count_attribute_slots(false);
}

static void
add_buffer_to_load_and_stores(glsl_to_tgsi_instruction *inst, st_src_reg *buf,
                              exec_list *instructions, ir_constant *access)
{
   /**
    * emit_asm() might have actually split the op into pieces, e.g. for
    * double stores. We have to go back and fix up all the generated ops.
    */
   enum tgsi_opcode op = inst->op;
   do {
      inst->resource = *buf;
      if (access)
         inst->buffer_access = access->value.u[0];

      if (inst == instructions->get_head_raw())
         break;
      inst = (glsl_to_tgsi_instruction *)inst->get_prev();

      if (inst->op == TGSI_OPCODE_UADD) {
         if (inst == instructions->get_head_raw())
            break;
         inst = (glsl_to_tgsi_instruction *)inst->get_prev();
      }
   } while (inst->op == op && inst->resource.file == PROGRAM_UNDEFINED);
}

/**
 * If the given GLSL type is an array or matrix or a structure containing
 * an array/matrix member, return true.  Else return false.
 *
 * This is used to determine which kind of temp storage (PROGRAM_TEMPORARY
 * or PROGRAM_ARRAY) should be used for variables of this type.  Anytime
 * we have an array that might be indexed with a variable, we need to use
 * the later storage type.
 */
static bool
type_has_array_or_matrix(const glsl_type *type)
{
   if (type->is_array() || type->is_matrix())
      return true;

   if (type->is_record()) {
      for (unsigned i = 0; i < type->length; i++) {
         if (type_has_array_or_matrix(type->fields.structure[i].type)) {
            return true;
         }
      }
   }

   return false;
}


/**
 * In the initial pass of codegen, we assign temporary numbers to
 * intermediate results.  (not SSA -- variable assignments will reuse
 * storage).
 */
st_src_reg
glsl_to_tgsi_visitor::get_temp(const glsl_type *type)
{
   st_src_reg src;

   src.type = native_integers ? type->base_type : GLSL_TYPE_FLOAT;
   src.reladdr = NULL;
   src.negate = 0;
   src.abs = 0;

   if (!options->EmitNoIndirectTemp && type_has_array_or_matrix(type)) {
      if (next_array >= max_num_arrays) {
         max_num_arrays += 32;
         array_sizes = (unsigned*)
            realloc(array_sizes, sizeof(array_sizes[0]) * max_num_arrays);
      }

      src.file = PROGRAM_ARRAY;
      src.index = 0;
      src.array_id = next_array + 1;
      array_sizes[next_array] = type_size(type);
      ++next_array;

   } else {
      src.file = PROGRAM_TEMPORARY;
      src.index = next_temp;
      next_temp += type_size(type);
   }

   if (type->is_array() || type->is_record()) {
      src.swizzle = SWIZZLE_NOOP;
   } else {
      src.swizzle = swizzle_for_size(type->vector_elements);
   }

   return src;
}

variable_storage *
glsl_to_tgsi_visitor::find_variable_storage(ir_variable *var)
{
   struct hash_entry *entry;

   entry = _mesa_hash_table_search(this->variables, var);
   if (!entry)
      return NULL;

   return (variable_storage *)entry->data;
}

void
glsl_to_tgsi_visitor::visit(ir_variable *ir)
{
   if (strcmp(ir->name, "gl_FragCoord") == 0) {
      this->prog->OriginUpperLeft = ir->data.origin_upper_left;
      this->prog->PixelCenterInteger = ir->data.pixel_center_integer;
   }

   if (ir->data.mode == ir_var_uniform && strncmp(ir->name, "gl_", 3) == 0) {
      unsigned int i;
      const ir_state_slot *const slots = ir->get_state_slots();
      assert(slots != NULL);

      /* Check if this statevar's setup in the STATE file exactly
       * matches how we'll want to reference it as a
       * struct/array/whatever.  If not, then we need to move it into
       * temporary storage and hope that it'll get copy-propagated
       * out.
       */
      for (i = 0; i < ir->get_num_state_slots(); i++) {
         if (slots[i].swizzle != SWIZZLE_XYZW) {
            break;
         }
      }

      variable_storage *storage;
      st_dst_reg dst;
      if (i == ir->get_num_state_slots()) {
         /* We'll set the index later. */
         storage = new(mem_ctx) variable_storage(ir, PROGRAM_STATE_VAR, -1);

         _mesa_hash_table_insert(this->variables, ir, storage);

         dst = undef_dst;
      } else {
         /* The variable_storage constructor allocates slots based on the size
          * of the type.  However, this had better match the number of state
          * elements that we're going to copy into the new temporary.
          */
         assert((int) ir->get_num_state_slots() == type_size(ir->type));

         dst = st_dst_reg(get_temp(ir->type));

         storage = new(mem_ctx) variable_storage(ir, dst.file, dst.index,
                                                 dst.array_id);

         _mesa_hash_table_insert(this->variables, ir, storage);
      }


      for (unsigned int i = 0; i < ir->get_num_state_slots(); i++) {
         int index = _mesa_add_state_reference(this->prog->Parameters,
                                               slots[i].tokens);

         if (storage->file == PROGRAM_STATE_VAR) {
            if (storage->index == -1) {
               storage->index = index;
            } else {
               assert(index == storage->index + (int)i);
            }
         } else {
            /* We use GLSL_TYPE_FLOAT here regardless of the actual type of
             * the data being moved since MOV does not care about the type of
             * data it is moving, and we don't want to declare registers with
             * array or struct types.
             */
            st_src_reg src(PROGRAM_STATE_VAR, index, GLSL_TYPE_FLOAT);
            src.swizzle = slots[i].swizzle;
            emit_asm(ir, TGSI_OPCODE_MOV, dst, src);
            /* even a float takes up a whole vec4 reg in a struct/array. */
            dst.index++;
         }
      }

      if (storage->file == PROGRAM_TEMPORARY &&
          dst.index != storage->index + (int) ir->get_num_state_slots()) {
         fail_link(this->shader_program,
                  "failed to load builtin uniform `%s'  (%d/%d regs loaded)\n",
                  ir->name, dst.index - storage->index,
                  type_size(ir->type));
      }
   }
}

void
glsl_to_tgsi_visitor::visit(ir_loop *ir)
{
   emit_asm(NULL, TGSI_OPCODE_BGNLOOP);

   visit_exec_list(&ir->body_instructions, this);

   emit_asm(NULL, TGSI_OPCODE_ENDLOOP);
}

void
glsl_to_tgsi_visitor::visit(ir_loop_jump *ir)
{
   switch (ir->mode) {
   case ir_loop_jump::jump_break:
      emit_asm(NULL, TGSI_OPCODE_BRK);
      break;
   case ir_loop_jump::jump_continue:
      emit_asm(NULL, TGSI_OPCODE_CONT);
      break;
   }
}


void
glsl_to_tgsi_visitor::visit(ir_function_signature *ir)
{
   assert(0);
   (void)ir;
}

void
glsl_to_tgsi_visitor::visit(ir_function *ir)
{
   /* Ignore function bodies other than main() -- we shouldn't see calls to
    * them since they should all be inlined before we get to glsl_to_tgsi.
    */
   if (strcmp(ir->name, "main") == 0) {
      const ir_function_signature *sig;
      exec_list empty;

      sig = ir->matching_signature(NULL, &empty, false);

      assert(sig);

      foreach_in_list(ir_instruction, ir, &sig->body) {
         ir->accept(this);
      }
   }
}

bool
glsl_to_tgsi_visitor::try_emit_mad(ir_expression *ir, int mul_operand)
{
   int nonmul_operand = 1 - mul_operand;
   st_src_reg a, b, c;
   st_dst_reg result_dst;

   ir_expression *expr = ir->operands[mul_operand]->as_expression();
   if (!expr || expr->operation != ir_binop_mul)
      return false;

   expr->operands[0]->accept(this);
   a = this->result;
   expr->operands[1]->accept(this);
   b = this->result;
   ir->operands[nonmul_operand]->accept(this);
   c = this->result;

   this->result = get_temp(ir->type);
   result_dst = st_dst_reg(this->result);
   result_dst.writemask = (1 << ir->type->vector_elements) - 1;
   emit_asm(ir, TGSI_OPCODE_MAD, result_dst, a, b, c);

   return true;
}

/**
 * Emit MAD(a, -b, a) instead of AND(a, NOT(b))
 *
 * The logic values are 1.0 for true and 0.0 for false.  Logical-and is
 * implemented using multiplication, and logical-or is implemented using
 * addition.  Logical-not can be implemented as (true - x), or (1.0 - x).
 * As result, the logical expression (a & !b) can be rewritten as:
 *
 *     - a * !b
 *     - a * (1 - b)
 *     - (a * 1) - (a * b)
 *     - a + -(a * b)
 *     - a + (a * -b)
 *
 * This final expression can be implemented as a single MAD(a, -b, a)
 * instruction.
 */
bool
glsl_to_tgsi_visitor::try_emit_mad_for_and_not(ir_expression *ir,
                                               int try_operand)
{
   const int other_operand = 1 - try_operand;
   st_src_reg a, b;

   ir_expression *expr = ir->operands[try_operand]->as_expression();
   if (!expr || expr->operation != ir_unop_logic_not)
      return false;

   ir->operands[other_operand]->accept(this);
   a = this->result;
   expr->operands[0]->accept(this);
   b = this->result;

   b.negate = ~b.negate;

   this->result = get_temp(ir->type);
   emit_asm(ir, TGSI_OPCODE_MAD, st_dst_reg(this->result), a, b, a);

   return true;
}

void
glsl_to_tgsi_visitor::reladdr_to_temp(ir_instruction *ir,
                                      st_src_reg *reg, int *num_reladdr)
{
   if (!reg->reladdr && !reg->reladdr2)
      return;

   if (reg->reladdr)
      emit_arl(ir, address_reg, *reg->reladdr);
   if (reg->reladdr2)
      emit_arl(ir, address_reg2, *reg->reladdr2);

   if (*num_reladdr != 1) {
      st_src_reg temp = get_temp(glsl_type::get_instance(reg->type, 4, 1));

      emit_asm(ir, TGSI_OPCODE_MOV, st_dst_reg(temp), *reg);
      *reg = temp;
   }

   (*num_reladdr)--;
}

void
glsl_to_tgsi_visitor::visit(ir_expression *ir)
{
   st_src_reg op[ARRAY_SIZE(ir->operands)];

   /* Quick peephole: Emit MAD(a, b, c) instead of ADD(MUL(a, b), c)
    */
   if (!this->precise && ir->operation == ir_binop_add) {
      if (try_emit_mad(ir, 1))
         return;
      if (try_emit_mad(ir, 0))
         return;
   }

   /* Quick peephole: Emit OPCODE_MAD(-a, -b, a) instead of AND(a, NOT(b))
    */
   if (!native_integers && ir->operation == ir_binop_logic_and) {
      if (try_emit_mad_for_and_not(ir, 1))
         return;
      if (try_emit_mad_for_and_not(ir, 0))
         return;
   }

   if (ir->operation == ir_quadop_vector)
      assert(!"ir_quadop_vector should have been lowered");

   for (unsigned int operand = 0; operand < ir->num_operands; operand++) {
      this->result.file = PROGRAM_UNDEFINED;
      ir->operands[operand]->accept(this);
      if (this->result.file == PROGRAM_UNDEFINED) {
         printf("Failed to get tree for expression operand:\n");
         ir->operands[operand]->print();
         printf("\n");
         exit(1);
      }
      op[operand] = this->result;

      /* Matrix expression operands should have been broken down to vector
       * operations already.
       */
      assert(!ir->operands[operand]->type->is_matrix());
   }

   visit_expression(ir, op);
}

/* The non-recursive part of the expression visitor lives in a separate
 * function and should be prevented from being inlined, to avoid a stack
 * explosion when deeply nested expressions are visited.
 */
void
glsl_to_tgsi_visitor::visit_expression(ir_expression* ir, st_src_reg *op)
{
   st_src_reg result_src;
   st_dst_reg result_dst;

   int vector_elements = ir->operands[0]->type->vector_elements;
   if (ir->operands[1] &&
       ir->operation != ir_binop_interpolate_at_offset &&
       ir->operation != ir_binop_interpolate_at_sample) {
      st_src_reg *swz_op = NULL;
      if (vector_elements > ir->operands[1]->type->vector_elements) {
         assert(ir->operands[1]->type->vector_elements == 1);
         swz_op = &op[1];
      } else if (vector_elements < ir->operands[1]->type->vector_elements) {
         assert(ir->operands[0]->type->vector_elements == 1);
         swz_op = &op[0];
      }
      if (swz_op) {
         uint16_t swizzle_x = GET_SWZ(swz_op->swizzle, 0);
         swz_op->swizzle = MAKE_SWIZZLE4(swizzle_x, swizzle_x,
                                         swizzle_x, swizzle_x);
      }
      vector_elements = MAX2(vector_elements,
                             ir->operands[1]->type->vector_elements);
   }
   if (ir->operands[2] &&
       ir->operands[2]->type->vector_elements != vector_elements) {
      /* This can happen with ir_triop_lrp, i.e. glsl mix */
      assert(ir->operands[2]->type->vector_elements == 1);
      uint16_t swizzle_x = GET_SWZ(op[2].swizzle, 0);
      op[2].swizzle = MAKE_SWIZZLE4(swizzle_x, swizzle_x,
                                    swizzle_x, swizzle_x);
   }

   this->result.file = PROGRAM_UNDEFINED;

   /* Storage for our result.  Ideally for an assignment we'd be using
    * the actual storage for the result here, instead.
    */
   result_src = get_temp(ir->type);
   /* convenience for the emit functions below. */
   result_dst = st_dst_reg(result_src);
   /* Limit writes to the channels that will be used by result_src later.
    * This does limit this temp's use as a temporary for multi-instruction
    * sequences.
    */
   result_dst.writemask = (1 << ir->type->vector_elements) - 1;

   switch (ir->operation) {
   case ir_unop_logic_not:
      if (result_dst.type != GLSL_TYPE_FLOAT)
         emit_asm(ir, TGSI_OPCODE_NOT, result_dst, op[0]);
      else {
         /* Previously 'SEQ dst, src, 0.0' was used for this.  However, many
          * older GPUs implement SEQ using multiple instructions (i915 uses two
          * SGE instructions and a MUL instruction).  Since our logic values are
          * 0.0 and 1.0, 1-x also implements !x.
          */
         op[0].negate = ~op[0].negate;
         emit_asm(ir, TGSI_OPCODE_ADD, result_dst, op[0],
                  st_src_reg_for_float(1.0));
      }
      break;
   case ir_unop_neg:
      if (result_dst.type == GLSL_TYPE_INT64 ||
          result_dst.type == GLSL_TYPE_UINT64)
         emit_asm(ir, TGSI_OPCODE_I64NEG, result_dst, op[0]);
      else if (result_dst.type == GLSL_TYPE_INT ||
               result_dst.type == GLSL_TYPE_UINT)
         emit_asm(ir, TGSI_OPCODE_INEG, result_dst, op[0]);
      else if (result_dst.type == GLSL_TYPE_DOUBLE)
         emit_asm(ir, TGSI_OPCODE_DNEG, result_dst, op[0]);
      else {
         op[0].negate = ~op[0].negate;
         result_src = op[0];
      }
      break;
   case ir_unop_subroutine_to_int:
      emit_asm(ir, TGSI_OPCODE_MOV, result_dst, op[0]);
      break;
   case ir_unop_abs:
      if (result_dst.type == GLSL_TYPE_FLOAT)
         emit_asm(ir, TGSI_OPCODE_MOV, result_dst, op[0].get_abs());
      else if (result_dst.type == GLSL_TYPE_DOUBLE)
         emit_asm(ir, TGSI_OPCODE_DABS, result_dst, op[0]);
      else if (result_dst.type == GLSL_TYPE_INT64 ||
               result_dst.type == GLSL_TYPE_UINT64)
         emit_asm(ir, TGSI_OPCODE_I64ABS, result_dst, op[0]);
      else
         emit_asm(ir, TGSI_OPCODE_IABS, result_dst, op[0]);
      break;
   case ir_unop_sign:
      emit_asm(ir, TGSI_OPCODE_SSG, result_dst, op[0]);
      break;
   case ir_unop_rcp:
      emit_scalar(ir, TGSI_OPCODE_RCP, result_dst, op[0]);
      break;

   case ir_unop_exp2:
      emit_scalar(ir, TGSI_OPCODE_EX2, result_dst, op[0]);
      break;
   case ir_unop_exp:
      assert(!"not reached: should be handled by exp_to_exp2");
      break;
   case ir_unop_log:
      assert(!"not reached: should be handled by log_to_log2");
      break;
   case ir_unop_log2:
      emit_scalar(ir, TGSI_OPCODE_LG2, result_dst, op[0]);
      break;
   case ir_unop_sin:
      emit_scalar(ir, TGSI_OPCODE_SIN, result_dst, op[0]);
      break;
   case ir_unop_cos:
      emit_scalar(ir, TGSI_OPCODE_COS, result_dst, op[0]);
      break;
   case ir_unop_saturate: {
      glsl_to_tgsi_instruction *inst;
      inst = emit_asm(ir, TGSI_OPCODE_MOV, result_dst, op[0]);
      inst->saturate = true;
      break;
   }

   case ir_unop_dFdx:
   case ir_unop_dFdx_coarse:
      emit_asm(ir, TGSI_OPCODE_DDX, result_dst, op[0]);
      break;
   case ir_unop_dFdx_fine:
      emit_asm(ir, TGSI_OPCODE_DDX_FINE, result_dst, op[0]);
      break;
   case ir_unop_dFdy:
   case ir_unop_dFdy_coarse:
   case ir_unop_dFdy_fine:
   {
      /* The X component contains 1 or -1 depending on whether the framebuffer
       * is a FBO or the window system buffer, respectively.
       * It is then multiplied with the source operand of DDY.
       */
      static const gl_state_index16 transform_y_state[STATE_LENGTH]
         = { STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM };

      unsigned transform_y_index =
         _mesa_add_state_reference(this->prog->Parameters,
                                   transform_y_state);

      st_src_reg transform_y = st_src_reg(PROGRAM_STATE_VAR,
                                          transform_y_index,
                                          glsl_type::vec4_type);
      transform_y.swizzle = SWIZZLE_XXXX;

      st_src_reg temp = get_temp(glsl_type::vec4_type);

      emit_asm(ir, TGSI_OPCODE_MUL, st_dst_reg(temp), transform_y, op[0]);
      emit_asm(ir, ir->operation == ir_unop_dFdy_fine ?
           TGSI_OPCODE_DDY_FINE : TGSI_OPCODE_DDY, result_dst, temp);
      break;
   }

   case ir_unop_frexp_sig:
      emit_asm(ir, TGSI_OPCODE_DFRACEXP, result_dst, undef_dst, op[0]);
      break;

   case ir_unop_frexp_exp:
      emit_asm(ir, TGSI_OPCODE_DFRACEXP, undef_dst, result_dst, op[0]);
      break;

   case ir_unop_noise: {
      /* At some point, a motivated person could add a better
       * implementation of noise.  Currently not even the nvidia
       * binary drivers do anything more than this.  In any case, the
       * place to do this is in the GL state tracker, not the poor
       * driver.
       */
      emit_asm(ir, TGSI_OPCODE_MOV, result_dst, st_src_reg_for_float(0.5));
      break;
   }

   case ir_binop_add:
      emit_asm(ir, TGSI_OPCODE_ADD, result_dst, op[0], op[1]);
      break;
   case ir_binop_sub:
      op[1].negate = ~op[1].negate;
      emit_asm(ir, TGSI_OPCODE_ADD, result_dst, op[0], op[1]);
      break;

   case ir_binop_mul:
      emit_asm(ir, TGSI_OPCODE_MUL, result_dst, op[0], op[1]);
      break;
   case ir_binop_div:
      emit_asm(ir, TGSI_OPCODE_DIV, result_dst, op[0], op[1]);
      break;
   case ir_binop_mod:
      if (result_dst.type == GLSL_TYPE_FLOAT)
         assert(!"ir_binop_mod should have been converted to b * fract(a/b)");
      else
         emit_asm(ir, TGSI_OPCODE_MOD, result_dst, op[0], op[1]);
      break;

   case ir_binop_less:
      emit_asm(ir, TGSI_OPCODE_SLT, result_dst, op[0], op[1]);
      break;
   case ir_binop_gequal:
      emit_asm(ir, TGSI_OPCODE_SGE, result_dst, op[0], op[1]);
      break;
   case ir_binop_equal:
      emit_asm(ir, TGSI_OPCODE_SEQ, result_dst, op[0], op[1]);
      break;
   case ir_binop_nequal:
      emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0], op[1]);
      break;
   case ir_binop_all_equal:
      /* "==" operator producing a scalar boolean. */
      if (ir->operands[0]->type->is_vector() ||
          ir->operands[1]->type->is_vector()) {
         st_src_reg temp = get_temp(native_integers ?
                                    glsl_type::uvec4_type :
                                    glsl_type::vec4_type);

         if (native_integers) {
            st_dst_reg temp_dst = st_dst_reg(temp);
            st_src_reg temp1 = st_src_reg(temp), temp2 = st_src_reg(temp);

            if (ir->operands[0]->type->is_boolean() &&
                ir->operands[1]->as_constant() &&
                ir->operands[1]->as_constant()->is_one()) {
               emit_asm(ir, TGSI_OPCODE_MOV, st_dst_reg(temp), op[0]);
            } else {
               emit_asm(ir, TGSI_OPCODE_SEQ, st_dst_reg(temp), op[0], op[1]);
            }

            /* Emit 1-3 AND operations to combine the SEQ results. */
            switch (ir->operands[0]->type->vector_elements) {
            case 2:
               break;
            case 3:
               temp_dst.writemask = WRITEMASK_Y;
               temp1.swizzle = SWIZZLE_YYYY;
               temp2.swizzle = SWIZZLE_ZZZZ;
               emit_asm(ir, TGSI_OPCODE_AND, temp_dst, temp1, temp2);
               break;
            case 4:
               temp_dst.writemask = WRITEMASK_X;
               temp1.swizzle = SWIZZLE_XXXX;
               temp2.swizzle = SWIZZLE_YYYY;
               emit_asm(ir, TGSI_OPCODE_AND, temp_dst, temp1, temp2);
               temp_dst.writemask = WRITEMASK_Y;
               temp1.swizzle = SWIZZLE_ZZZZ;
               temp2.swizzle = SWIZZLE_WWWW;
               emit_asm(ir, TGSI_OPCODE_AND, temp_dst, temp1, temp2);
            }

            temp1.swizzle = SWIZZLE_XXXX;
            temp2.swizzle = SWIZZLE_YYYY;
            emit_asm(ir, TGSI_OPCODE_AND, result_dst, temp1, temp2);
         } else {
            emit_asm(ir, TGSI_OPCODE_SNE, st_dst_reg(temp), op[0], op[1]);

            /* After the dot-product, the value will be an integer on the
             * range [0,4].  Zero becomes 1.0, and positive values become zero.
             */
            emit_dp(ir, result_dst, temp, temp, vector_elements);

            /* Negating the result of the dot-product gives values on the range
             * [-4, 0].  Zero becomes 1.0, and negative values become zero.
             * This is achieved using SGE.
             */
            st_src_reg sge_src = result_src;
            sge_src.negate = ~sge_src.negate;
            emit_asm(ir, TGSI_OPCODE_SGE, result_dst, sge_src,
                     st_src_reg_for_float(0.0));
         }
      } else {
         emit_asm(ir, TGSI_OPCODE_SEQ, result_dst, op[0], op[1]);
      }
      break;
   case ir_binop_any_nequal:
      /* "!=" operator producing a scalar boolean. */
      if (ir->operands[0]->type->is_vector() ||
          ir->operands[1]->type->is_vector()) {
         st_src_reg temp = get_temp(native_integers ?
                                    glsl_type::uvec4_type :
                                    glsl_type::vec4_type);
         if (ir->operands[0]->type->is_boolean() &&
             ir->operands[1]->as_constant() &&
             ir->operands[1]->as_constant()->is_zero()) {
            emit_asm(ir, TGSI_OPCODE_MOV, st_dst_reg(temp), op[0]);
         } else {
            emit_asm(ir, TGSI_OPCODE_SNE, st_dst_reg(temp), op[0], op[1]);
         }

         if (native_integers) {
            st_dst_reg temp_dst = st_dst_reg(temp);
            st_src_reg temp1 = st_src_reg(temp), temp2 = st_src_reg(temp);

            /* Emit 1-3 OR operations to combine the SNE results. */
            switch (ir->operands[0]->type->vector_elements) {
            case 2:
               break;
            case 3:
               temp_dst.writemask = WRITEMASK_Y;
               temp1.swizzle = SWIZZLE_YYYY;
               temp2.swizzle = SWIZZLE_ZZZZ;
               emit_asm(ir, TGSI_OPCODE_OR, temp_dst, temp1, temp2);
               break;
            case 4:
               temp_dst.writemask = WRITEMASK_X;
               temp1.swizzle = SWIZZLE_XXXX;
               temp2.swizzle = SWIZZLE_YYYY;
               emit_asm(ir, TGSI_OPCODE_OR, temp_dst, temp1, temp2);
               temp_dst.writemask = WRITEMASK_Y;
               temp1.swizzle = SWIZZLE_ZZZZ;
               temp2.swizzle = SWIZZLE_WWWW;
               emit_asm(ir, TGSI_OPCODE_OR, temp_dst, temp1, temp2);
            }

            temp1.swizzle = SWIZZLE_XXXX;
            temp2.swizzle = SWIZZLE_YYYY;
            emit_asm(ir, TGSI_OPCODE_OR, result_dst, temp1, temp2);
         } else {
            /* After the dot-product, the value will be an integer on the
             * range [0,4].  Zero stays zero, and positive values become 1.0.
             */
            glsl_to_tgsi_instruction *const dp =
                  emit_dp(ir, result_dst, temp, temp, vector_elements);
            if (this->prog->Target == GL_FRAGMENT_PROGRAM_ARB) {
               /* The clamping to [0,1] can be done for free in the fragment
                * shader with a saturate.
                */
               dp->saturate = true;
            } else {
               /* Negating the result of the dot-product gives values on the
                * range [-4, 0].  Zero stays zero, and negative values become
                * 1.0.  This achieved using SLT.
                */
               st_src_reg slt_src = result_src;
               slt_src.negate = ~slt_src.negate;
               emit_asm(ir, TGSI_OPCODE_SLT, result_dst, slt_src,
                        st_src_reg_for_float(0.0));
            }
         }
      } else {
         emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0], op[1]);
      }
      break;

   case ir_binop_logic_xor:
      if (native_integers)
         emit_asm(ir, TGSI_OPCODE_XOR, result_dst, op[0], op[1]);
      else
         emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0], op[1]);
      break;

   case ir_binop_logic_or: {
      if (native_integers) {
         /* If integers are used as booleans, we can use an actual "or"
          * instruction.
          */
         assert(native_integers);
         emit_asm(ir, TGSI_OPCODE_OR, result_dst, op[0], op[1]);
      } else {
         /* After the addition, the value will be an integer on the
          * range [0,2].  Zero stays zero, and positive values become 1.0.
          */
         glsl_to_tgsi_instruction *add =
            emit_asm(ir, TGSI_OPCODE_ADD, result_dst, op[0], op[1]);
         if (this->prog->Target == GL_FRAGMENT_PROGRAM_ARB) {
            /* The clamping to [0,1] can be done for free in the fragment
             * shader with a saturate if floats are being used as boolean
             * values.
             */
            add->saturate = true;
         } else {
            /* Negating the result of the addition gives values on the range
             * [-2, 0].  Zero stays zero, and negative values become 1.0
             * This is achieved using SLT.
             */
            st_src_reg slt_src = result_src;
            slt_src.negate = ~slt_src.negate;
            emit_asm(ir, TGSI_OPCODE_SLT, result_dst, slt_src,
                     st_src_reg_for_float(0.0));
         }
      }
      break;
   }

   case ir_binop_logic_and:
      /* If native integers are disabled, the bool args are stored as float 0.0
       * or 1.0, so "mul" gives us "and".  If they're enabled, just use the
       * actual AND opcode.
       */
      if (native_integers)
         emit_asm(ir, TGSI_OPCODE_AND, result_dst, op[0], op[1]);
      else
         emit_asm(ir, TGSI_OPCODE_MUL, result_dst, op[0], op[1]);
      break;

   case ir_binop_dot:
      assert(ir->operands[0]->type->is_vector());
      assert(ir->operands[0]->type == ir->operands[1]->type);
      emit_dp(ir, result_dst, op[0], op[1],
              ir->operands[0]->type->vector_elements);
      break;

   case ir_unop_sqrt:
      if (have_sqrt) {
         emit_scalar(ir, TGSI_OPCODE_SQRT, result_dst, op[0]);
      } else {
         /* This is the only instruction sequence that makes the game "Risen"
          * render correctly. ABS is not required for the game, but since GLSL
          * declares negative values as "undefined", allowing us to do whatever
          * we want, I choose to use ABS to match DX9 and pre-GLSL RSQ
          * behavior.
          */
         emit_scalar(ir, TGSI_OPCODE_RSQ, result_dst, op[0].get_abs());
         emit_scalar(ir, TGSI_OPCODE_RCP, result_dst, result_src);
      }
      break;
   case ir_unop_rsq:
      emit_scalar(ir, TGSI_OPCODE_RSQ, result_dst, op[0]);
      break;
   case ir_unop_i2f:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_I2F, result_dst, op[0]);
         break;
      }
      /* fallthrough to next case otherwise */
   case ir_unop_b2f:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_AND, result_dst, op[0],
                  st_src_reg_for_float(1.0));
         break;
      }
      /* fallthrough to next case otherwise */
   case ir_unop_i2u:
   case ir_unop_u2i:
   case ir_unop_i642u64:
   case ir_unop_u642i64:
      /* Converting between signed and unsigned integers is a no-op. */
      result_src = op[0];
      result_src.type = result_dst.type;
      break;
   case ir_unop_b2i:
      if (native_integers) {
         /* Booleans are stored as integers using ~0 for true and 0 for false.
          * GLSL requires that int(bool) return 1 for true and 0 for false.
          * This conversion is done with AND, but it could be done with NEG.
          */
         emit_asm(ir, TGSI_OPCODE_AND, result_dst, op[0],
                  st_src_reg_for_int(1));
      } else {
         /* Booleans and integers are both stored as floats when native
          * integers are disabled.
          */
         result_src = op[0];
      }
      break;
   case ir_unop_f2i:
      if (native_integers)
         emit_asm(ir, TGSI_OPCODE_F2I, result_dst, op[0]);
      else
         emit_asm(ir, TGSI_OPCODE_TRUNC, result_dst, op[0]);
      break;
   case ir_unop_f2u:
      if (native_integers)
         emit_asm(ir, TGSI_OPCODE_F2U, result_dst, op[0]);
      else
         emit_asm(ir, TGSI_OPCODE_TRUNC, result_dst, op[0]);
      break;
   case ir_unop_bitcast_f2i:
   case ir_unop_bitcast_f2u:
      /* Make sure we don't propagate the negate modifier to integer opcodes. */
      if (op[0].negate || op[0].abs)
         emit_asm(ir, TGSI_OPCODE_MOV, result_dst, op[0]);
      else
         result_src = op[0];
      result_src.type = ir->operation == ir_unop_bitcast_f2i ? GLSL_TYPE_INT :
                                                               GLSL_TYPE_UINT;
      break;
   case ir_unop_bitcast_i2f:
   case ir_unop_bitcast_u2f:
      result_src = op[0];
      result_src.type = GLSL_TYPE_FLOAT;
      break;
   case ir_unop_f2b:
      emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0],
               st_src_reg_for_float(0.0));
      break;
   case ir_unop_d2b:
      emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0],
               st_src_reg_for_double(0.0));
      break;
   case ir_unop_i2b:
      if (native_integers)
         emit_asm(ir, TGSI_OPCODE_USNE, result_dst, op[0],
                  st_src_reg_for_int(0));
      else
         emit_asm(ir, TGSI_OPCODE_SNE, result_dst, op[0],
                  st_src_reg_for_float(0.0));
      break;
   case ir_unop_bitcast_u642d:
   case ir_unop_bitcast_i642d:
      result_src = op[0];
      result_src.type = GLSL_TYPE_DOUBLE;
      break;
   case ir_unop_bitcast_d2i64:
      result_src = op[0];
      result_src.type = GLSL_TYPE_INT64;
      break;
   case ir_unop_bitcast_d2u64:
      result_src = op[0];
      result_src.type = GLSL_TYPE_UINT64;
      break;
   case ir_unop_trunc:
      emit_asm(ir, TGSI_OPCODE_TRUNC, result_dst, op[0]);
      break;
   case ir_unop_ceil:
      emit_asm(ir, TGSI_OPCODE_CEIL, result_dst, op[0]);
      break;
   case ir_unop_floor:
      emit_asm(ir, TGSI_OPCODE_FLR, result_dst, op[0]);
      break;
   case ir_unop_round_even:
      emit_asm(ir, TGSI_OPCODE_ROUND, result_dst, op[0]);
      break;
   case ir_unop_fract:
      emit_asm(ir, TGSI_OPCODE_FRC, result_dst, op[0]);
      break;

   case ir_binop_min:
      emit_asm(ir, TGSI_OPCODE_MIN, result_dst, op[0], op[1]);
      break;
   case ir_binop_max:
      emit_asm(ir, TGSI_OPCODE_MAX, result_dst, op[0], op[1]);
      break;
   case ir_binop_pow:
      emit_scalar(ir, TGSI_OPCODE_POW, result_dst, op[0], op[1]);
      break;

   case ir_unop_bit_not:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_NOT, result_dst, op[0]);
         break;
      }
   case ir_unop_u2f:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_U2F, result_dst, op[0]);
         break;
      }
   case ir_binop_lshift:
   case ir_binop_rshift:
      if (native_integers) {
         enum tgsi_opcode opcode = ir->operation == ir_binop_lshift
            ? TGSI_OPCODE_SHL : TGSI_OPCODE_ISHR;
         st_src_reg count;

         if (glsl_base_type_is_64bit(op[0].type)) {
            /* GLSL shift operations have 32-bit shift counts, but TGSI uses
             * 64 bits.
             */
            count = get_temp(glsl_type::u64vec(ir->operands[1]
                                               ->type->components()));
            emit_asm(ir, TGSI_OPCODE_U2I64, st_dst_reg(count), op[1]);
         } else {
            count = op[1];
         }

         emit_asm(ir, opcode, result_dst, op[0], count);
         break;
      }
   case ir_binop_bit_and:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_AND, result_dst, op[0], op[1]);
         break;
      }
   case ir_binop_bit_xor:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_XOR, result_dst, op[0], op[1]);
         break;
      }
   case ir_binop_bit_or:
      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_OR, result_dst, op[0], op[1]);
         break;
      }

      assert(!"GLSL 1.30 features unsupported");
      break;

   case ir_binop_ubo_load: {
      if (ctx->Const.UseSTD430AsDefaultPacking) {
         ir_rvalue *block = ir->operands[0];
         ir_rvalue *offset = ir->operands[1];
         ir_constant *const_block = block->as_constant();

         st_src_reg cbuf(PROGRAM_CONSTANT,
            (const_block ? const_block->value.u[0] + 1 : 1),
            ir->type->base_type);

         cbuf.has_index2 = true;

         if (!const_block) {
            block->accept(this);
            cbuf.reladdr = ralloc(mem_ctx, st_src_reg);
            *cbuf.reladdr = this->result;
            emit_arl(ir, sampler_reladdr, this->result);
         }

         /* Calculate the surface offset */
         offset->accept(this);
         st_src_reg off = this->result;

         glsl_to_tgsi_instruction *inst =
            emit_asm(ir, TGSI_OPCODE_LOAD, result_dst, off);

         if (result_dst.type == GLSL_TYPE_BOOL)
            emit_asm(ir, TGSI_OPCODE_USNE, result_dst, st_src_reg(result_dst),
                     st_src_reg_for_int(0));

         add_buffer_to_load_and_stores(inst, &cbuf, &this->instructions,
                                       NULL);
      } else {
         ir_constant *const_uniform_block = ir->operands[0]->as_constant();
         ir_constant *const_offset_ir = ir->operands[1]->as_constant();
         unsigned const_offset = const_offset_ir ?
            const_offset_ir->value.u[0] : 0;
         unsigned const_block = const_uniform_block ?
            const_uniform_block->value.u[0] + 1 : 1;
         st_src_reg index_reg = get_temp(glsl_type::uint_type);
         st_src_reg cbuf;

         cbuf.type = ir->type->base_type;
         cbuf.file = PROGRAM_CONSTANT;
         cbuf.index = 0;
         cbuf.reladdr = NULL;
         cbuf.negate = 0;
         cbuf.abs = 0;
         cbuf.index2D = const_block;

         assert(ir->type->is_vector() || ir->type->is_scalar());

         if (const_offset_ir) {
            /* Constant index into constant buffer */
            cbuf.reladdr = NULL;
            cbuf.index = const_offset / 16;
         } else {
            ir_expression *offset_expr = ir->operands[1]->as_expression();
            st_src_reg offset = op[1];

            /* The OpenGL spec is written in such a way that accesses with
             * non-constant offset are almost always vec4-aligned. The only
             * exception to this are members of structs in arrays of structs:
             * each struct in an array of structs is at least vec4-aligned,
             * but single-element and [ui]vec2 members of the struct may be at
             * an offset that is not a multiple of 16 bytes.
             *
             * Here, we extract that offset, relying on previous passes to
             * always generate offset expressions of the form
             * (+ expr constant_offset).
             *
             * Note that the std430 layout, which allows more cases of
             * alignment less than vec4 in arrays, is not supported for
             * uniform blocks, so we do not have to deal with it here.
             */
            if (offset_expr && offset_expr->operation == ir_binop_add) {
               const_offset_ir = offset_expr->operands[1]->as_constant();
               if (const_offset_ir) {
                  const_offset = const_offset_ir->value.u[0];
                  cbuf.index = const_offset / 16;
                  offset_expr->operands[0]->accept(this);
                  offset = this->result;
               }
            }

            /* Relative/variable index into constant buffer */
            emit_asm(ir, TGSI_OPCODE_USHR, st_dst_reg(index_reg), offset,
                 st_src_reg_for_int(4));
            cbuf.reladdr = ralloc(mem_ctx, st_src_reg);
            memcpy(cbuf.reladdr, &index_reg, sizeof(index_reg));
         }

         if (const_uniform_block) {
            /* Constant constant buffer */
            cbuf.reladdr2 = NULL;
         } else {
            /* Relative/variable constant buffer */
            cbuf.reladdr2 = ralloc(mem_ctx, st_src_reg);
            memcpy(cbuf.reladdr2, &op[0], sizeof(st_src_reg));
         }
         cbuf.has_index2 = true;

         cbuf.swizzle = swizzle_for_size(ir->type->vector_elements);
         if (glsl_base_type_is_64bit(cbuf.type))
            cbuf.swizzle += MAKE_SWIZZLE4(const_offset % 16 / 8,
                                          const_offset % 16 / 8,
                                          const_offset % 16 / 8,
                                          const_offset % 16 / 8);
         else
            cbuf.swizzle += MAKE_SWIZZLE4(const_offset % 16 / 4,
                                          const_offset % 16 / 4,
                                          const_offset % 16 / 4,
                                          const_offset % 16 / 4);

         if (ir->type->is_boolean()) {
            emit_asm(ir, TGSI_OPCODE_USNE, result_dst, cbuf,
                     st_src_reg_for_int(0));
         } else {
            emit_asm(ir, TGSI_OPCODE_MOV, result_dst, cbuf);
         }
      }
      break;
   }
   case ir_triop_lrp:
      /* note: we have to reorder the three args here */
      emit_asm(ir, TGSI_OPCODE_LRP, result_dst, op[2], op[1], op[0]);
      break;
   case ir_triop_csel:
      if (this->ctx->Const.NativeIntegers)
         emit_asm(ir, TGSI_OPCODE_UCMP, result_dst, op[0], op[1], op[2]);
      else {
         op[0].negate = ~op[0].negate;
         emit_asm(ir, TGSI_OPCODE_CMP, result_dst, op[0], op[1], op[2]);
      }
      break;
   case ir_triop_bitfield_extract:
      emit_asm(ir, TGSI_OPCODE_IBFE, result_dst, op[0], op[1], op[2]);
      break;
   case ir_quadop_bitfield_insert:
      emit_asm(ir, TGSI_OPCODE_BFI, result_dst, op[0], op[1], op[2], op[3]);
      break;
   case ir_unop_bitfield_reverse:
      emit_asm(ir, TGSI_OPCODE_BREV, result_dst, op[0]);
      break;
   case ir_unop_bit_count:
      emit_asm(ir, TGSI_OPCODE_POPC, result_dst, op[0]);
      break;
   case ir_unop_find_msb:
      emit_asm(ir, TGSI_OPCODE_IMSB, result_dst, op[0]);
      break;
   case ir_unop_find_lsb:
      emit_asm(ir, TGSI_OPCODE_LSB, result_dst, op[0]);
      break;
   case ir_binop_imul_high:
      emit_asm(ir, TGSI_OPCODE_IMUL_HI, result_dst, op[0], op[1]);
      break;
   case ir_triop_fma:
      /* In theory, MAD is incorrect here. */
      if (have_fma)
         emit_asm(ir, TGSI_OPCODE_FMA, result_dst, op[0], op[1], op[2]);
      else
         emit_asm(ir, TGSI_OPCODE_MAD, result_dst, op[0], op[1], op[2]);
      break;
   case ir_unop_interpolate_at_centroid:
      emit_asm(ir, TGSI_OPCODE_INTERP_CENTROID, result_dst, op[0]);
      break;
   case ir_binop_interpolate_at_offset: {
      /* The y coordinate needs to be flipped for the default fb */
      static const gl_state_index16 transform_y_state[STATE_LENGTH]
         = { STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM };

      unsigned transform_y_index =
         _mesa_add_state_reference(this->prog->Parameters,
                                   transform_y_state);

      st_src_reg transform_y = st_src_reg(PROGRAM_STATE_VAR,
                                          transform_y_index,
                                          glsl_type::vec4_type);
      transform_y.swizzle = SWIZZLE_XXXX;

      st_src_reg temp = get_temp(glsl_type::vec2_type);
      st_dst_reg temp_dst = st_dst_reg(temp);

      emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, op[1]);
      temp_dst.writemask = WRITEMASK_Y;
      emit_asm(ir, TGSI_OPCODE_MUL, temp_dst, transform_y, op[1]);
      emit_asm(ir, TGSI_OPCODE_INTERP_OFFSET, result_dst, op[0], temp);
      break;
   }
   case ir_binop_interpolate_at_sample:
      emit_asm(ir, TGSI_OPCODE_INTERP_SAMPLE, result_dst, op[0], op[1]);
      break;

   case ir_unop_d2f:
      emit_asm(ir, TGSI_OPCODE_D2F, result_dst, op[0]);
      break;
   case ir_unop_f2d:
      emit_asm(ir, TGSI_OPCODE_F2D, result_dst, op[0]);
      break;
   case ir_unop_d2i:
      emit_asm(ir, TGSI_OPCODE_D2I, result_dst, op[0]);
      break;
   case ir_unop_i2d:
      emit_asm(ir, TGSI_OPCODE_I2D, result_dst, op[0]);
      break;
   case ir_unop_d2u:
      emit_asm(ir, TGSI_OPCODE_D2U, result_dst, op[0]);
      break;
   case ir_unop_u2d:
      emit_asm(ir, TGSI_OPCODE_U2D, result_dst, op[0]);
      break;
   case ir_unop_unpack_double_2x32:
   case ir_unop_pack_double_2x32:
   case ir_unop_unpack_int_2x32:
   case ir_unop_pack_int_2x32:
   case ir_unop_unpack_uint_2x32:
   case ir_unop_pack_uint_2x32:
   case ir_unop_unpack_sampler_2x32:
   case ir_unop_pack_sampler_2x32:
   case ir_unop_unpack_image_2x32:
   case ir_unop_pack_image_2x32:
      emit_asm(ir, TGSI_OPCODE_MOV, result_dst, op[0]);
      break;

   case ir_binop_ldexp:
      if (ir->operands[0]->type->is_double()) {
         emit_asm(ir, TGSI_OPCODE_DLDEXP, result_dst, op[0], op[1]);
      } else if (ir->operands[0]->type->is_float()) {
         emit_asm(ir, TGSI_OPCODE_LDEXP, result_dst, op[0], op[1]);
      } else {
         assert(!"Invalid ldexp for non-double opcode in glsl_to_tgsi_visitor::visit()");
      }
      break;

   case ir_unop_pack_half_2x16:
      emit_asm(ir, TGSI_OPCODE_PK2H, result_dst, op[0]);
      break;
   case ir_unop_unpack_half_2x16:
      emit_asm(ir, TGSI_OPCODE_UP2H, result_dst, op[0]);
      break;

   case ir_unop_get_buffer_size: {
      ir_constant *const_offset = ir->operands[0]->as_constant();
      int buf_base = ctx->st->has_hw_atomics
         ? 0 : ctx->Const.Program[shader->Stage].MaxAtomicBuffers;
      st_src_reg buffer(
            PROGRAM_BUFFER,
            buf_base + (const_offset ? const_offset->value.u[0] : 0),
            GLSL_TYPE_UINT);
      if (!const_offset) {
         buffer.reladdr = ralloc(mem_ctx, st_src_reg);
         *buffer.reladdr = op[0];
         emit_arl(ir, sampler_reladdr, op[0]);
      }
      emit_asm(ir, TGSI_OPCODE_RESQ, result_dst)->resource = buffer;
      break;
   }

   case ir_unop_u2i64:
   case ir_unop_u2u64:
   case ir_unop_b2i64: {
      st_src_reg temp = get_temp(glsl_type::uvec4_type);
      st_dst_reg temp_dst = st_dst_reg(temp);
      unsigned orig_swz = op[0].swizzle;
      /*
       * To convert unsigned to 64-bit:
       * zero Y channel, copy X channel.
       */
      temp_dst.writemask = WRITEMASK_Y;
      if (vector_elements > 1)
         temp_dst.writemask |= WRITEMASK_W;
      emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, st_src_reg_for_int(0));
      temp_dst.writemask = WRITEMASK_X;
      if (vector_elements > 1)
          temp_dst.writemask |= WRITEMASK_Z;
      op[0].swizzle = MAKE_SWIZZLE4(GET_SWZ(orig_swz, 0), GET_SWZ(orig_swz, 0),
                                    GET_SWZ(orig_swz, 1), GET_SWZ(orig_swz, 1));
      if (ir->operation == ir_unop_u2i64 || ir->operation == ir_unop_u2u64)
         emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, op[0]);
      else
         emit_asm(ir, TGSI_OPCODE_AND, temp_dst, op[0], st_src_reg_for_int(1));
      result_src = temp;
      result_src.type = GLSL_TYPE_UINT64;
      if (vector_elements > 2) {
         /* Subtle: We rely on the fact that get_temp here returns the next
          * TGSI temporary register directly after the temp register used for
          * the first two components, so that the result gets picked up
          * automatically.
          */
         st_src_reg temp = get_temp(glsl_type::uvec4_type);
         st_dst_reg temp_dst = st_dst_reg(temp);
         temp_dst.writemask = WRITEMASK_Y;
         if (vector_elements > 3)
            temp_dst.writemask |= WRITEMASK_W;
         emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, st_src_reg_for_int(0));

         temp_dst.writemask = WRITEMASK_X;
         if (vector_elements > 3)
            temp_dst.writemask |= WRITEMASK_Z;
         op[0].swizzle = MAKE_SWIZZLE4(GET_SWZ(orig_swz, 2),
                                       GET_SWZ(orig_swz, 2),
                                       GET_SWZ(orig_swz, 3),
                                       GET_SWZ(orig_swz, 3));
         if (ir->operation == ir_unop_u2i64 || ir->operation == ir_unop_u2u64)
            emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, op[0]);
         else
            emit_asm(ir, TGSI_OPCODE_AND, temp_dst, op[0],
                     st_src_reg_for_int(1));
      }
      break;
   }
   case ir_unop_i642i:
   case ir_unop_u642i:
   case ir_unop_u642u:
   case ir_unop_i642u: {
      st_src_reg temp = get_temp(glsl_type::uvec4_type);
      st_dst_reg temp_dst = st_dst_reg(temp);
      unsigned orig_swz = op[0].swizzle;
      unsigned orig_idx = op[0].index;
      int el;
      temp_dst.writemask = WRITEMASK_X;

      for (el = 0; el < vector_elements; el++) {
         unsigned swz = GET_SWZ(orig_swz, el);
         if (swz & 1)
            op[0].swizzle = MAKE_SWIZZLE4(SWIZZLE_Z, SWIZZLE_Z,
                                          SWIZZLE_Z, SWIZZLE_Z);
         else
            op[0].swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X,
                                          SWIZZLE_X, SWIZZLE_X);
         if (swz > 2)
            op[0].index = orig_idx + 1;
         op[0].type = GLSL_TYPE_UINT;
         temp_dst.writemask = WRITEMASK_X << el;
         emit_asm(ir, TGSI_OPCODE_MOV, temp_dst, op[0]);
      }
      result_src = temp;
      if (ir->operation == ir_unop_u642u || ir->operation == ir_unop_i642u)
         result_src.type = GLSL_TYPE_UINT;
      else
         result_src.type = GLSL_TYPE_INT;
      break;
   }
   case ir_unop_i642b:
      emit_asm(ir, TGSI_OPCODE_U64SNE, result_dst, op[0],
               st_src_reg_for_int64(0));
      break;
   case ir_unop_i642f:
      emit_asm(ir, TGSI_OPCODE_I642F, result_dst, op[0]);
      break;
   case ir_unop_u642f:
      emit_asm(ir, TGSI_OPCODE_U642F, result_dst, op[0]);
      break;
   case ir_unop_i642d:
      emit_asm(ir, TGSI_OPCODE_I642D, result_dst, op[0]);
      break;
   case ir_unop_u642d:
      emit_asm(ir, TGSI_OPCODE_U642D, result_dst, op[0]);
      break;
   case ir_unop_i2i64:
      emit_asm(ir, TGSI_OPCODE_I2I64, result_dst, op[0]);
      break;
   case ir_unop_f2i64:
      emit_asm(ir, TGSI_OPCODE_F2I64, result_dst, op[0]);
      break;
   case ir_unop_d2i64:
      emit_asm(ir, TGSI_OPCODE_D2I64, result_dst, op[0]);
      break;
   case ir_unop_i2u64:
      emit_asm(ir, TGSI_OPCODE_I2I64, result_dst, op[0]);
      break;
   case ir_unop_f2u64:
      emit_asm(ir, TGSI_OPCODE_F2U64, result_dst, op[0]);
      break;
   case ir_unop_d2u64:
      emit_asm(ir, TGSI_OPCODE_D2U64, result_dst, op[0]);
      break;
      /* these might be needed */
   case ir_unop_pack_snorm_2x16:
   case ir_unop_pack_unorm_2x16:
   case ir_unop_pack_snorm_4x8:
   case ir_unop_pack_unorm_4x8:

   case ir_unop_unpack_snorm_2x16:
   case ir_unop_unpack_unorm_2x16:
   case ir_unop_unpack_snorm_4x8:
   case ir_unop_unpack_unorm_4x8:

   case ir_quadop_vector:
   case ir_binop_vector_extract:
   case ir_triop_vector_insert:
   case ir_binop_carry:
   case ir_binop_borrow:
   case ir_unop_ssbo_unsized_array_length:
      /* This operation is not supported, or should have already been handled.
       */
      assert(!"Invalid ir opcode in glsl_to_tgsi_visitor::visit()");
      break;
   }

   this->result = result_src;
}


void
glsl_to_tgsi_visitor::visit(ir_swizzle *ir)
{
   st_src_reg src;
   int i;
   int swizzle[4];

   /* Note that this is only swizzles in expressions, not those on the left
    * hand side of an assignment, which do write masking.  See ir_assignment
    * for that.
    */

   ir->val->accept(this);
   src = this->result;
   assert(src.file != PROGRAM_UNDEFINED);
   assert(ir->type->vector_elements > 0);

   for (i = 0; i < 4; i++) {
      if (i < ir->type->vector_elements) {
         switch (i) {
         case 0:
            swizzle[i] = GET_SWZ(src.swizzle, ir->mask.x);
            break;
         case 1:
            swizzle[i] = GET_SWZ(src.swizzle, ir->mask.y);
            break;
         case 2:
            swizzle[i] = GET_SWZ(src.swizzle, ir->mask.z);
            break;
         case 3:
            swizzle[i] = GET_SWZ(src.swizzle, ir->mask.w);
            break;
         }
      } else {
         /* If the type is smaller than a vec4, replicate the last
          * channel out.
          */
         swizzle[i] = swizzle[ir->type->vector_elements - 1];
      }
   }

   src.swizzle = MAKE_SWIZZLE4(swizzle[0], swizzle[1], swizzle[2], swizzle[3]);

   this->result = src;
}

/* Test if the variable is an array. Note that geometry and
 * tessellation shader inputs are outputs are always arrays (except
 * for patch inputs), so only the array element type is considered.
 */
static bool
is_inout_array(unsigned stage, ir_variable *var, bool *remove_array)
{
   const glsl_type *type = var->type;

   *remove_array = false;

   if ((stage == MESA_SHADER_VERTEX && var->data.mode == ir_var_shader_in) ||
       (stage == MESA_SHADER_FRAGMENT && var->data.mode == ir_var_shader_out))
      return false;

   if (((stage == MESA_SHADER_GEOMETRY && var->data.mode == ir_var_shader_in) ||
        (stage == MESA_SHADER_TESS_EVAL && var->data.mode == ir_var_shader_in) ||
        stage == MESA_SHADER_TESS_CTRL) &&
       !var->data.patch) {
      if (!var->type->is_array())
         return false; /* a system value probably */

      type = var->type->fields.array;
      *remove_array = true;
   }

   return type->is_array() || type->is_matrix();
}

static unsigned
st_translate_interp_loc(ir_variable *var)
{
   if (var->data.centroid)
      return TGSI_INTERPOLATE_LOC_CENTROID;
   else if (var->data.sample)
      return TGSI_INTERPOLATE_LOC_SAMPLE;
   else
      return TGSI_INTERPOLATE_LOC_CENTER;
}

void
glsl_to_tgsi_visitor::visit(ir_dereference_variable *ir)
{
   variable_storage *entry = find_variable_storage(ir->var);
   ir_variable *var = ir->var;
   bool remove_array;

   if (!entry) {
      switch (var->data.mode) {
      case ir_var_uniform:
         entry = new(mem_ctx) variable_storage(var, PROGRAM_UNIFORM,
                                               var->data.param_index);
         _mesa_hash_table_insert(this->variables, var, entry);
         break;
      case ir_var_shader_in: {
         /* The linker assigns locations for varyings and attributes,
          * including deprecated builtins (like gl_Color), user-assign
          * generic attributes (glBindVertexLocation), and
          * user-defined varyings.
          */
         assert(var->data.location != -1);

         const glsl_type *type_without_array = var->type->without_array();
         struct inout_decl *decl = &inputs[num_inputs];
         unsigned component = var->data.location_frac;
         unsigned num_components;
         num_inputs++;

         if (type_without_array->is_64bit())
            component = component / 2;
         if (type_without_array->vector_elements)
            num_components = type_without_array->vector_elements;
         else
            num_components = 4;

         decl->mesa_index = var->data.location;
         decl->interp = (glsl_interp_mode) var->data.interpolation;
         decl->interp_loc = st_translate_interp_loc(var);
         decl->base_type = type_without_array->base_type;
         decl->usage_mask = u_bit_consecutive(component, num_components);

         if (is_inout_array(shader->Stage, var, &remove_array)) {
            decl->array_id = num_input_arrays + 1;
            num_input_arrays++;
         } else {
            decl->array_id = 0;
         }

         if (remove_array)
            decl->size = type_size(var->type->fields.array);
         else
            decl->size = type_size(var->type);

         entry = new(mem_ctx) variable_storage(var,
                                               PROGRAM_INPUT,
                                               decl->mesa_index,
                                               decl->array_id);
         entry->component = component;

         _mesa_hash_table_insert(this->variables, var, entry);

         break;
      }
      case ir_var_shader_out: {
         assert(var->data.location != -1);

         const glsl_type *type_without_array = var->type->without_array();
         struct inout_decl *decl = &outputs[num_outputs];
         unsigned component = var->data.location_frac;
         unsigned num_components;
         num_outputs++;

         if (type_without_array->is_64bit())
            component = component / 2;
         if (type_without_array->vector_elements)
            num_components = type_without_array->vector_elements;
         else
            num_components = 4;

         decl->mesa_index = var->data.location + FRAG_RESULT_MAX * var->data.index;
         decl->base_type = type_without_array->base_type;
         decl->usage_mask = u_bit_consecutive(component, num_components);
         if (var->data.stream & (1u << 31)) {
            decl->gs_out_streams = var->data.stream & ~(1u << 31);
         } else {
            assert(var->data.stream < 4);
            decl->gs_out_streams = 0;
            for (unsigned i = 0; i < num_components; ++i)
               decl->gs_out_streams |= var->data.stream << (2 * (component + i));
         }

         if (is_inout_array(shader->Stage, var, &remove_array)) {
            decl->array_id = num_output_arrays + 1;
            num_output_arrays++;
         } else {
            decl->array_id = 0;
         }

         if (remove_array)
            decl->size = type_size(var->type->fields.array);
         else
            decl->size = type_size(var->type);

         if (var->data.fb_fetch_output) {
            st_dst_reg dst = st_dst_reg(get_temp(var->type));
            st_src_reg src = st_src_reg(PROGRAM_OUTPUT, decl->mesa_index,
                                        var->type, component, decl->array_id);
            emit_asm(NULL, TGSI_OPCODE_FBFETCH, dst, src);
            entry = new(mem_ctx) variable_storage(var, dst.file, dst.index,
                                                  dst.array_id);
         } else {
            entry = new(mem_ctx) variable_storage(var,
                                                  PROGRAM_OUTPUT,
                                                  decl->mesa_index,
                                                  decl->array_id);
         }
         entry->component = component;

         _mesa_hash_table_insert(this->variables, var, entry);

         break;
      }
      case ir_var_system_value:
         entry = new(mem_ctx) variable_storage(var,
                                               PROGRAM_SYSTEM_VALUE,
                                               var->data.location);
         break;
      case ir_var_auto:
      case ir_var_temporary:
         st_src_reg src = get_temp(var->type);

         entry = new(mem_ctx) variable_storage(var, src.file, src.index,
                                               src.array_id);
         _mesa_hash_table_insert(this->variables, var, entry);

         break;
      }

      if (!entry) {
         printf("Failed to make storage for %s\n", var->name);
         exit(1);
      }
   }

   this->result = st_src_reg(entry->file, entry->index, var->type,
                             entry->component, entry->array_id);
   if (this->shader->Stage == MESA_SHADER_VERTEX &&
       var->data.mode == ir_var_shader_in &&
       var->type->without_array()->is_double())
      this->result.is_double_vertex_input = true;
   if (!native_integers)
      this->result.type = GLSL_TYPE_FLOAT;
}

static void
shrink_array_declarations(struct inout_decl *decls, unsigned count,
                          GLbitfield64* usage_mask,
                          GLbitfield64 double_usage_mask,
                          GLbitfield* patch_usage_mask)
{
   unsigned i;
   int j;

   /* Fix array declarations by removing unused array elements at both ends
    * of the arrays. For example, mat4[3] where only mat[1] is used.
    */
   for (i = 0; i < count; i++) {
      struct inout_decl *decl = &decls[i];
      if (!decl->array_id)
         continue;

      /* Shrink the beginning. */
      for (j = 0; j < (int)decl->size; j++) {
         if (decl->mesa_index >= VARYING_SLOT_PATCH0) {
            if (*patch_usage_mask &
                BITFIELD64_BIT(decl->mesa_index - VARYING_SLOT_PATCH0 + j))
               break;
         }
         else {
            if (*usage_mask & BITFIELD64_BIT(decl->mesa_index+j))
               break;
            if (double_usage_mask & BITFIELD64_BIT(decl->mesa_index+j-1))
               break;
         }

         decl->mesa_index++;
         decl->size--;
         j--;
      }

      /* Shrink the end. */
      for (j = decl->size-1; j >= 0; j--) {
         if (decl->mesa_index >= VARYING_SLOT_PATCH0) {
            if (*patch_usage_mask &
                BITFIELD64_BIT(decl->mesa_index - VARYING_SLOT_PATCH0 + j))
               break;
         }
         else {
            if (*usage_mask & BITFIELD64_BIT(decl->mesa_index+j))
               break;
            if (double_usage_mask & BITFIELD64_BIT(decl->mesa_index+j-1))
               break;
         }

         decl->size--;
      }

      /* When not all entries of an array are accessed, we mark them as used
       * here anyway, to ensure that the input/output mapping logic doesn't get
       * confused.
       *
       * TODO This happens when an array isn't used via indirect access, which
       * some game ports do (at least eON-based). There is an optimization
       * opportunity here by replacing the array declaration with non-array
       * declarations of those slots that are actually used.
       */
      for (j = 1; j < (int)decl->size; ++j) {
         if (decl->mesa_index >= VARYING_SLOT_PATCH0)
            *patch_usage_mask |= BITFIELD64_BIT(decl->mesa_index - VARYING_SLOT_PATCH0 + j);
         else
            *usage_mask |= BITFIELD64_BIT(decl->mesa_index + j);
      }
   }
}

void
glsl_to_tgsi_visitor::visit(ir_dereference_array *ir)
{
   ir_constant *index;
   st_src_reg src;
   bool is_2D = false;
   ir_variable *var = ir->variable_referenced();

   /* We only need the logic provided by st_glsl_storage_type_size()
    * for arrays of structs. Indirect sampler and image indexing is handled
    * elsewhere.
    */
   int element_size = ir->type->without_array()->is_record() ?
      st_glsl_storage_type_size(ir->type, var->data.bindless) :
      type_size(ir->type);

   index = ir->array_index->constant_expression_value(ralloc_parent(ir));

   ir->array->accept(this);
   src = this->result;

   if (!src.has_index2) {
      switch (this->prog->Target) {
      case GL_TESS_CONTROL_PROGRAM_NV:
         is_2D = (src.file == PROGRAM_INPUT || src.file == PROGRAM_OUTPUT) &&
                 !ir->variable_referenced()->data.patch;
         break;
      case GL_TESS_EVALUATION_PROGRAM_NV:
         is_2D = src.file == PROGRAM_INPUT &&
                 !ir->variable_referenced()->data.patch;
         break;
      case GL_GEOMETRY_PROGRAM_NV:
         is_2D = src.file == PROGRAM_INPUT;
         break;
      }
   }

   if (is_2D)
      element_size = 1;

   if (index) {

      if (this->prog->Target == GL_VERTEX_PROGRAM_ARB &&
          src.file == PROGRAM_INPUT)
         element_size = attrib_type_size(ir->type, true);
      if (is_2D) {
         src.index2D = index->value.i[0];
         src.has_index2 = true;
      } else
         src.index += index->value.i[0] * element_size;
   } else {
      /* Variable index array dereference.  It eats the "vec4" of the
       * base of the array and an index that offsets the TGSI register
       * index.
       */
      ir->array_index->accept(this);

      st_src_reg index_reg;

      if (element_size == 1) {
         index_reg = this->result;
      } else {
         index_reg = get_temp(native_integers ?
                              glsl_type::int_type : glsl_type::float_type);

         emit_asm(ir, TGSI_OPCODE_MUL, st_dst_reg(index_reg),
              this->result, st_src_reg_for_type(index_reg.type, element_size));
      }

      /* If there was already a relative address register involved, add the
       * new and the old together to get the new offset.
       */
      if (!is_2D && src.reladdr != NULL) {
         st_src_reg accum_reg = get_temp(native_integers ?
                                glsl_type::int_type : glsl_type::float_type);

         emit_asm(ir, TGSI_OPCODE_ADD, st_dst_reg(accum_reg),
              index_reg, *src.reladdr);

         index_reg = accum_reg;
      }

      if (is_2D) {
         src.reladdr2 = ralloc(mem_ctx, st_src_reg);
         memcpy(src.reladdr2, &index_reg, sizeof(index_reg));
         src.index2D = 0;
         src.has_index2 = true;
      } else {
         src.reladdr = ralloc(mem_ctx, st_src_reg);
         memcpy(src.reladdr, &index_reg, sizeof(index_reg));
      }
   }

   /* Change the register type to the element type of the array. */
   src.type = ir->type->base_type;

   this->result = src;
}

void
glsl_to_tgsi_visitor::visit(ir_dereference_record *ir)
{
   unsigned int i;
   const glsl_type *struct_type = ir->record->type;
   ir_variable *var = ir->record->variable_referenced();
   int offset = 0;

   ir->record->accept(this);

   assert(ir->field_idx >= 0);
   assert(var);
   for (i = 0; i < struct_type->length; i++) {
      if (i == (unsigned) ir->field_idx)
         break;
      const glsl_type *member_type = struct_type->fields.structure[i].type;
      offset += st_glsl_storage_type_size(member_type, var->data.bindless);
   }

   /* If the type is smaller than a vec4, replicate the last channel out. */
   if (ir->type->is_scalar() || ir->type->is_vector())
      this->result.swizzle = swizzle_for_size(ir->type->vector_elements);
   else
      this->result.swizzle = SWIZZLE_NOOP;

   this->result.index += offset;
   this->result.type = ir->type->base_type;
}

/**
 * We want to be careful in assignment setup to hit the actual storage
 * instead of potentially using a temporary like we might with the
 * ir_dereference handler.
 */
static st_dst_reg
get_assignment_lhs(ir_dereference *ir, glsl_to_tgsi_visitor *v, int *component)
{
   /* The LHS must be a dereference.  If the LHS is a variable indexed array
    * access of a vector, it must be separated into a series conditional moves
    * before reaching this point (see ir_vec_index_to_cond_assign).
    */
   assert(ir->as_dereference());
   ir_dereference_array *deref_array = ir->as_dereference_array();
   if (deref_array) {
      assert(!deref_array->array->type->is_vector());
   }

   /* Use the rvalue deref handler for the most part.  We write swizzles using
    * the writemask, but we do extract the base component for enhanced layouts
    * from the source swizzle.
    */
   ir->accept(v);
   *component = GET_SWZ(v->result.swizzle, 0);
   return st_dst_reg(v->result);
}

/**
 * Process the condition of a conditional assignment
 *
 * Examines the condition of a conditional assignment to generate the optimal
 * first operand of a \c CMP instruction.  If the condition is a relational
 * operator with 0 (e.g., \c ir_binop_less), the value being compared will be
 * used as the source for the \c CMP instruction.  Otherwise the comparison
 * is processed to a boolean result, and the boolean result is used as the
 * operand to the CMP instruction.
 */
bool
glsl_to_tgsi_visitor::process_move_condition(ir_rvalue *ir)
{
   ir_rvalue *src_ir = ir;
   bool negate = true;
   bool switch_order = false;

   ir_expression *const expr = ir->as_expression();

   if (native_integers) {
      if ((expr != NULL) && (expr->num_operands == 2)) {
         enum glsl_base_type type = expr->operands[0]->type->base_type;
         if (type == GLSL_TYPE_INT || type == GLSL_TYPE_UINT ||
             type == GLSL_TYPE_BOOL) {
            if (expr->operation == ir_binop_equal) {
               if (expr->operands[0]->is_zero()) {
                  src_ir = expr->operands[1];
                  switch_order = true;
               }
               else if (expr->operands[1]->is_zero()) {
                  src_ir = expr->operands[0];
                  switch_order = true;
               }
            }
            else if (expr->operation == ir_binop_nequal) {
               if (expr->operands[0]->is_zero()) {
                  src_ir = expr->operands[1];
               }
               else if (expr->operands[1]->is_zero()) {
                  src_ir = expr->operands[0];
               }
            }
         }
      }

      src_ir->accept(this);
      return switch_order;
   }

   if ((expr != NULL) && (expr->num_operands == 2)) {
      bool zero_on_left = false;

      if (expr->operands[0]->is_zero()) {
         src_ir = expr->operands[1];
         zero_on_left = true;
      } else if (expr->operands[1]->is_zero()) {
         src_ir = expr->operands[0];
         zero_on_left = false;
      }

      /*      a is -  0  +            -  0  +
       * (a <  0)  T  F  F  ( a < 0)  T  F  F
       * (0 <  a)  F  F  T  (-a < 0)  F  F  T
       * (a >= 0)  F  T  T  ( a < 0)  T  F  F  (swap order of other operands)
       * (0 >= a)  T  T  F  (-a < 0)  F  F  T  (swap order of other operands)
       *
       * Note that exchanging the order of 0 and 'a' in the comparison simply
       * means that the value of 'a' should be negated.
       */
      if (src_ir != ir) {
         switch (expr->operation) {
         case ir_binop_less:
            switch_order = false;
            negate = zero_on_left;
            break;

         case ir_binop_gequal:
            switch_order = true;
            negate = zero_on_left;
            break;

         default:
            /* This isn't the right kind of comparison afterall, so make sure
             * the whole condition is visited.
             */
            src_ir = ir;
            break;
         }
      }
   }

   src_ir->accept(this);

   /* We use the TGSI_OPCODE_CMP (a < 0 ? b : c) for conditional moves, and the
    * condition we produced is 0.0 or 1.0.  By flipping the sign, we can
    * choose which value TGSI_OPCODE_CMP produces without an extra instruction
    * computing the condition.
    */
   if (negate)
      this->result.negate = ~this->result.negate;

   return switch_order;
}

void
glsl_to_tgsi_visitor::emit_block_mov(ir_assignment *ir, const struct glsl_type *type,
                                     st_dst_reg *l, st_src_reg *r,
                                     st_src_reg *cond, bool cond_swap)
{
   if (type->is_record()) {
      for (unsigned int i = 0; i < type->length; i++) {
         emit_block_mov(ir, type->fields.structure[i].type, l, r,
                        cond, cond_swap);
      }
      return;
   }

   if (type->is_array()) {
      for (unsigned int i = 0; i < type->length; i++) {
         emit_block_mov(ir, type->fields.array, l, r, cond, cond_swap);
      }
      return;
   }

   if (type->is_matrix()) {
      const struct glsl_type *vec_type;

      vec_type = glsl_type::get_instance(type->is_double()
                                         ? GLSL_TYPE_DOUBLE : GLSL_TYPE_FLOAT,
                                         type->vector_elements, 1);

      for (int i = 0; i < type->matrix_columns; i++) {
         emit_block_mov(ir, vec_type, l, r, cond, cond_swap);
      }
      return;
   }

   assert(type->is_scalar() || type->is_vector());

   l->type = type->base_type;
   r->type = type->base_type;
   if (cond) {
      st_src_reg l_src = st_src_reg(*l);

      if (l_src.file == PROGRAM_OUTPUT &&
          this->prog->Target == GL_FRAGMENT_PROGRAM_ARB &&
          (l_src.index == FRAG_RESULT_DEPTH ||
           l_src.index == FRAG_RESULT_STENCIL)) {
         /* This is a special case because the source swizzles will be shifted
          * later to account for the difference between GLSL (where they're
          * plain floats) and TGSI (where they're Z and Y components). */
         l_src.swizzle = SWIZZLE_XXXX;
      }

      if (native_integers) {
         emit_asm(ir, TGSI_OPCODE_UCMP, *l, *cond,
              cond_swap ? l_src : *r,
              cond_swap ? *r : l_src);
      } else {
         emit_asm(ir, TGSI_OPCODE_CMP, *l, *cond,
              cond_swap ? l_src : *r,
              cond_swap ? *r : l_src);
      }
   } else {
      emit_asm(ir, TGSI_OPCODE_MOV, *l, *r);
   }
   l->index++;
   r->index++;
   if (type->is_dual_slot()) {
      l->index++;
      if (r->is_double_vertex_input == false)
         r->index++;
   }
}

void
glsl_to_tgsi_visitor::visit(ir_assignment *ir)
{
   int dst_component;
   st_dst_reg l;
   st_src_reg r;

   /* all generated instructions need to be flaged as precise */
   this->precise = is_precise(ir->lhs->variable_referenced());
   ir->rhs->accept(this);
   r = this->result;

   l = get_assignment_lhs(ir->lhs, this, &dst_component);

   {
      int swizzles[4];
      int first_enabled_chan = 0;
      int rhs_chan = 0;
      ir_variable *variable = ir->lhs->variable_referenced();

      if (shader->Stage == MESA_SHADER_FRAGMENT &&
          variable->data.mode == ir_var_shader_out &&
          (variable->data.location == FRAG_RESULT_DEPTH ||
           variable->data.location == FRAG_RESULT_STENCIL)) {
         assert(ir->lhs->type->is_scalar());
         assert(ir->write_mask == WRITEMASK_X);

         if (variable->data.location == FRAG_RESULT_DEPTH)
            l.writemask = WRITEMASK_Z;
         else {
            assert(variable->data.location == FRAG_RESULT_STENCIL);
            l.writemask = WRITEMASK_Y;
         }
      } else if (ir->write_mask == 0) {
         assert(!ir->lhs->type->is_scalar() && !ir->lhs->type->is_vector());

         unsigned num_elements =
            ir->lhs->type->without_array()->vector_elements;

         if (num_elements) {
            l.writemask = u_bit_consecutive(0, num_elements);
         } else {
            /* The type is a struct or an array of (array of) structs. */
            l.writemask = WRITEMASK_XYZW;
         }
      } else {
         l.writemask = ir->write_mask;
      }

      for (int i = 0; i < 4; i++) {
         if (l.writemask & (1 << i)) {
            first_enabled_chan = GET_SWZ(r.swizzle, i);
            break;
         }
      }

      l.writemask = l.writemask << dst_component;

      /* Swizzle a small RHS vector into the channels being written.
       *
       * glsl ir treats write_mask as dictating how many channels are
       * present on the RHS while TGSI treats write_mask as just
       * showing which channels of the vec4 RHS get written.
       */
      for (int i = 0; i < 4; i++) {
         if (l.writemask & (1 << i))
            swizzles[i] = GET_SWZ(r.swizzle, rhs_chan++);
         else
            swizzles[i] = first_enabled_chan;
      }
      r.swizzle = MAKE_SWIZZLE4(swizzles[0], swizzles[1],
                                swizzles[2], swizzles[3]);
   }

   assert(l.file != PROGRAM_UNDEFINED);
   assert(r.file != PROGRAM_UNDEFINED);

   if (ir->condition) {
      const bool switch_order = this->process_move_condition(ir->condition);
      st_src_reg condition = this->result;

      emit_block_mov(ir, ir->lhs->type, &l, &r, &condition, switch_order);
   } else if (ir->rhs->as_expression() &&
              this->instructions.get_tail() &&
              ir->rhs == ((glsl_to_tgsi_instruction *)this->instructions.get_tail())->ir &&
              !((glsl_to_tgsi_instruction *)this->instructions.get_tail())->is_64bit_expanded &&
              type_size(ir->lhs->type) == 1 &&
              l.writemask == ((glsl_to_tgsi_instruction *)this->instructions.get_tail())->dst[0].writemask) {
      /* To avoid emitting an extra MOV when assigning an expression to a
       * variable, emit the last instruction of the expression again, but
       * replace the destination register with the target of the assignment.
       * Dead code elimination will remove the original instruction.
       */
      glsl_to_tgsi_instruction *inst, *new_inst;
      inst = (glsl_to_tgsi_instruction *)this->instructions.get_tail();
      new_inst = emit_asm(ir, inst->op, l, inst->src[0], inst->src[1], inst->src[2], inst->src[3]);
      new_inst->saturate = inst->saturate;
      new_inst->resource = inst->resource;
      inst->dead_mask = inst->dst[0].writemask;
   } else {
      emit_block_mov(ir, ir->rhs->type, &l, &r, NULL, false);
   }
   this->precise = 0;
}


void
glsl_to_tgsi_visitor::visit(ir_constant *ir)
{
   st_src_reg src;
   GLdouble stack_vals[4] = { 0 };
   gl_constant_value *values = (gl_constant_value *) stack_vals;
   GLenum gl_type = GL_NONE;
   unsigned int i;
   static int in_array = 0;
   gl_register_file file = in_array ? PROGRAM_CONSTANT : PROGRAM_IMMEDIATE;

   /* Unfortunately, 4 floats is all we can get into
    * _mesa_add_typed_unnamed_constant.  So, make a temp to store an
    * aggregate constant and move each constant value into it.  If we
    * get lucky, copy propagation will eliminate the extra moves.
    */
   if (ir->type->is_record()) {
      st_src_reg temp_base = get_temp(ir->type);
      st_dst_reg temp = st_dst_reg(temp_base);

      for (i = 0; i < ir->type->length; i++) {
         ir_constant *const field_value = ir->get_record_field(i);
         int size = type_size(field_value->type);

         assert(size > 0);

         field_value->accept(this);
         src = this->result;

         for (unsigned j = 0; j < (unsigned int)size; j++) {
            emit_asm(ir, TGSI_OPCODE_MOV, temp, src);

            src.index++;
            temp.index++;
         }
      }
      this->result = temp_base;
      return;
   }

   if (ir->type->is_array()) {
      st_src_reg temp_base = get_temp(ir->type);
      st_dst_reg temp = st_dst_reg(temp_base);
      int size = type_size(ir->type->fields.array);

      assert(size > 0);
      in_array++;

      for (i = 0; i < ir->type->length; i++) {
         ir->const_elements[i]->accept(this);
         src = this->result;
         for (int j = 0; j < size; j++) {
            emit_asm(ir, TGSI_OPCODE_MOV, temp, src);

            src.index++;
            temp.index++;
         }
      }
      this->result = temp_base;
      in_array--;
      return;
   }

   if (ir->type->is_matrix()) {
      st_src_reg mat = get_temp(ir->type);
      st_dst_reg mat_column = st_dst_reg(mat);

      for (i = 0; i < ir->type->matrix_columns; i++) {
         switch (ir->type->base_type) {
         case GLSL_TYPE_FLOAT:
            values = (gl_constant_value *)
               &ir->value.f[i * ir->type->vector_elements];

            src = st_src_reg(file, -1, ir->type->base_type);
            src.index = add_constant(file,
                                     values,
                                     ir->type->vector_elements,
                                     GL_FLOAT,
                                     &src.swizzle);
            emit_asm(ir, TGSI_OPCODE_MOV, mat_column, src);
            break;
         case GLSL_TYPE_DOUBLE:
            values = (gl_constant_value *)
               &ir->value.d[i * ir->type->vector_elements];
            src = st_src_reg(file, -1, ir->type->base_type);
            src.index = add_constant(file,
                                     values,
                                     ir->type->vector_elements,
                                     GL_DOUBLE,
                                     &src.swizzle);
            if (ir->type->vector_elements >= 2) {
               mat_column.writemask = WRITEMASK_XY;
               src.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y,
                                           SWIZZLE_X, SWIZZLE_Y);
               emit_asm(ir, TGSI_OPCODE_MOV, mat_column, src);
            } else {
               mat_column.writemask = WRITEMASK_X;
               src.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X,
                                           SWIZZLE_X, SWIZZLE_X);
               emit_asm(ir, TGSI_OPCODE_MOV, mat_column, src);
            }
            src.index++;
            if (ir->type->vector_elements > 2) {
               if (ir->type->vector_elements == 4) {
                  mat_column.writemask = WRITEMASK_ZW;
                  src.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y,
                                              SWIZZLE_X, SWIZZLE_Y);
                  emit_asm(ir, TGSI_OPCODE_MOV, mat_column, src);
               } else {
                  mat_column.writemask = WRITEMASK_Z;
                  src.swizzle = MAKE_SWIZZLE4(SWIZZLE_Y, SWIZZLE_Y,
                                              SWIZZLE_Y, SWIZZLE_Y);
                  emit_asm(ir, TGSI_OPCODE_MOV, mat_column, src);
                  mat_column.writemask = WRITEMASK_XYZW;
                  src.swizzle = SWIZZLE_XYZW;
               }
               mat_column.index++;
            }
            break;
         default:
            unreachable("Illegal matrix constant type.\n");
            break;
         }
         mat_column.index++;
      }
      this->result = mat;
      return;
   }

   switch (ir->type->base_type) {
   case GLSL_TYPE_FLOAT:
      gl_type = GL_FLOAT;
      for (i = 0; i < ir->type->vector_elements; i++) {
         values[i].f = ir->value.f[i];
      }
      break;
   case GLSL_TYPE_DOUBLE:
      gl_type = GL_DOUBLE;
      for (i = 0; i < ir->type->vector_elements; i++) {
         memcpy(&values[i * 2], &ir->value.d[i], sizeof(double));
      }
      break;
   case GLSL_TYPE_INT64:
      gl_type = GL_INT64_ARB;
      for (i = 0; i < ir->type->vector_elements; i++) {
         memcpy(&values[i * 2], &ir->value.d[i], sizeof(int64_t));
      }
      break;
   case GLSL_TYPE_UINT64:
      gl_type = GL_UNSIGNED_INT64_ARB;
      for (i = 0; i < ir->type->vector_elements; i++) {
         memcpy(&values[i * 2], &ir->value.d[i], sizeof(uint64_t));
      }
      break;
   case GLSL_TYPE_UINT:
      gl_type = native_integers ? GL_UNSIGNED_INT : GL_FLOAT;
      for (i = 0; i < ir->type->vector_elements; i++) {
         if (native_integers)
            values[i].u = ir->value.u[i];
         else
            values[i].f = ir->value.u[i];
      }
      break;
   case GLSL_TYPE_INT:
      gl_type = native_integers ? GL_INT : GL_FLOAT;
      for (i = 0; i < ir->type->vector_elements; i++) {
         if (native_integers)
            values[i].i = ir->value.i[i];
         else
            values[i].f = ir->value.i[i];
      }
      break;
   case GLSL_TYPE_BOOL:
      gl_type = native_integers ? GL_BOOL : GL_FLOAT;
      for (i = 0; i < ir->type->vector_elements; i++) {
         values[i].u = ir->value.b[i] ? ctx->Const.UniformBooleanTrue : 0;
      }
      break;
   default:
      assert(!"Non-float/uint/int/bool constant");
   }

   this->result = st_src_reg(file, -1, ir->type);
   this->result.index = add_constant(file,
                                     values,
                                     ir->type->vector_elements,
                                     gl_type,
                                     &this->result.swizzle);
}

void
glsl_to_tgsi_visitor::visit_atomic_counter_intrinsic(ir_call *ir)
{
   exec_node *param = ir->actual_parameters.get_head();
   ir_dereference *deref = static_cast<ir_dereference *>(param);
   ir_variable *location = deref->variable_referenced();
   bool has_hw_atomics = st_context(ctx)->has_hw_atomics;
   /* Calculate the surface offset */
   st_src_reg offset;
   unsigned array_size = 0, base = 0;
   uint16_t index = 0;
   st_src_reg resource;

   get_deref_offsets(deref, &array_size, &base, &index, &offset, false);

   if (has_hw_atomics) {
      variable_storage *entry = find_variable_storage(location);
      st_src_reg buffer(PROGRAM_HW_ATOMIC, 0, GLSL_TYPE_ATOMIC_UINT,
                        location->data.binding);

      if (!entry) {
         entry = new(mem_ctx) variable_storage(location, PROGRAM_HW_ATOMIC,
                                               num_atomics);
         _mesa_hash_table_insert(this->variables, location, entry);

         atomic_info[num_atomics].location = location->data.location;
         atomic_info[num_atomics].binding = location->data.binding;
         atomic_info[num_atomics].size = location->type->arrays_of_arrays_size();
         if (atomic_info[num_atomics].size == 0)
            atomic_info[num_atomics].size = 1;
         atomic_info[num_atomics].array_id = 0;
         num_atomics++;
      }

      if (offset.file != PROGRAM_UNDEFINED) {
         if (atomic_info[entry->index].array_id == 0) {
            num_atomic_arrays++;
            atomic_info[entry->index].array_id = num_atomic_arrays;
         }
         buffer.array_id = atomic_info[entry->index].array_id;
      }

      buffer.index = index;
      buffer.index += location->data.offset / ATOMIC_COUNTER_SIZE;
      buffer.has_index2 = true;

      if (offset.file != PROGRAM_UNDEFINED) {
         buffer.reladdr = ralloc(mem_ctx, st_src_reg);
         *buffer.reladdr = offset;
         emit_arl(ir, sampler_reladdr, offset);
      }
      offset = st_src_reg_for_int(0);

      resource = buffer;
   } else {
      st_src_reg buffer(PROGRAM_BUFFER, location->data.binding,
                        GLSL_TYPE_ATOMIC_UINT);

      if (offset.file != PROGRAM_UNDEFINED) {
         emit_asm(ir, TGSI_OPCODE_MUL, st_dst_reg(offset),
                  offset, st_src_reg_for_int(ATOMIC_COUNTER_SIZE));
         emit_asm(ir, TGSI_OPCODE_ADD, st_dst_reg(offset),
                  offset, st_src_reg_for_int(location->data.offset + index * ATOMIC_COUNTER_SIZE));
      } else {
         offset = st_src_reg_for_int(location->data.offset + index * ATOMIC_COUNTER_SIZE);
      }
      resource = buffer;
   }

   ir->return_deref->accept(this);
   st_dst_reg dst(this->result);
   dst.writemask = WRITEMASK_X;

   glsl_to_tgsi_instruction *inst;

   if (ir->callee->intrinsic_id == ir_intrinsic_atomic_counter_read) {
      inst = emit_asm(ir, TGSI_OPCODE_LOAD, dst, offset);
   } else if (ir->callee->intrinsic_id == ir_intrinsic_atomic_counter_increment) {
      inst = emit_asm(ir, TGSI_OPCODE_ATOMUADD, dst, offset,
                      st_src_reg_for_int(1));
   } else if (ir->callee->intrinsic_id == ir_intrinsic_atomic_counter_predecrement) {
      inst = emit_asm(ir, TGSI_OPCODE_ATOMUADD, dst, offset,
                      st_src_reg_for_int(-1));
      emit_asm(ir, TGSI_OPCODE_ADD, dst, this->result, st_src_reg_for_int(-1));
   } else {
      param = param->get_next();
      ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();
      val->accept(this);

      st_src_reg data = this->result, data2 = undef_src;
      enum tgsi_opcode opcode;
      switch (ir->callee->intrinsic_id) {
      case ir_intrinsic_atomic_counter_add:
         opcode = TGSI_OPCODE_ATOMUADD;
         break;
      case ir_intrinsic_atomic_counter_min:
         opcode = TGSI_OPCODE_ATOMIMIN;
         break;
      case ir_intrinsic_atomic_counter_max:
         opcode = TGSI_OPCODE_ATOMIMAX;
         break;
      case ir_intrinsic_atomic_counter_and:
         opcode = TGSI_OPCODE_ATOMAND;
         break;
      case ir_intrinsic_atomic_counter_or:
         opcode = TGSI_OPCODE_ATOMOR;
         break;
      case ir_intrinsic_atomic_counter_xor:
         opcode = TGSI_OPCODE_ATOMXOR;
         break;
      case ir_intrinsic_atomic_counter_exchange:
         opcode = TGSI_OPCODE_ATOMXCHG;
         break;
      case ir_intrinsic_atomic_counter_comp_swap: {
         opcode = TGSI_OPCODE_ATOMCAS;
         param = param->get_next();
         val = ((ir_instruction *)param)->as_rvalue();
         val->accept(this);
         data2 = this->result;
         break;
      }
      default:
         assert(!"Unexpected intrinsic");
         return;
      }

      inst = emit_asm(ir, opcode, dst, offset, data, data2);
   }

   inst->resource = resource;
}

void
glsl_to_tgsi_visitor::visit_ssbo_intrinsic(ir_call *ir)
{
   exec_node *param = ir->actual_parameters.get_head();

   ir_rvalue *block = ((ir_instruction *)param)->as_rvalue();

   param = param->get_next();
   ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

   ir_constant *const_block = block->as_constant();
   int buf_base = st_context(ctx)->has_hw_atomics
      ? 0 : ctx->Const.Program[shader->Stage].MaxAtomicBuffers;
   st_src_reg buffer(
         PROGRAM_BUFFER,
         buf_base + (const_block ? const_block->value.u[0] : 0),
         GLSL_TYPE_UINT);

   if (!const_block) {
      block->accept(this);
      buffer.reladdr = ralloc(mem_ctx, st_src_reg);
      *buffer.reladdr = this->result;
      emit_arl(ir, sampler_reladdr, this->result);
   }

   /* Calculate the surface offset */
   offset->accept(this);
   st_src_reg off = this->result;

   st_dst_reg dst = undef_dst;
   if (ir->return_deref) {
      ir->return_deref->accept(this);
      dst = st_dst_reg(this->result);
      dst.writemask = (1 << ir->return_deref->type->vector_elements) - 1;
   }

   glsl_to_tgsi_instruction *inst;

   if (ir->callee->intrinsic_id == ir_intrinsic_ssbo_load) {
      inst = emit_asm(ir, TGSI_OPCODE_LOAD, dst, off);
      if (dst.type == GLSL_TYPE_BOOL)
         emit_asm(ir, TGSI_OPCODE_USNE, dst, st_src_reg(dst),
                  st_src_reg_for_int(0));
   } else if (ir->callee->intrinsic_id == ir_intrinsic_ssbo_store) {
      param = param->get_next();
      ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();
      val->accept(this);

      param = param->get_next();
      ir_constant *write_mask = ((ir_instruction *)param)->as_constant();
      assert(write_mask);
      dst.writemask = write_mask->value.u[0];

      dst.type = this->result.type;
      inst = emit_asm(ir, TGSI_OPCODE_STORE, dst, off, this->result);
   } else {
      param = param->get_next();
      ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();
      val->accept(this);

      st_src_reg data = this->result, data2 = undef_src;
      enum tgsi_opcode opcode;
      switch (ir->callee->intrinsic_id) {
      case ir_intrinsic_ssbo_atomic_add:
         opcode = TGSI_OPCODE_ATOMUADD;
         break;
      case ir_intrinsic_ssbo_atomic_min:
         opcode = TGSI_OPCODE_ATOMIMIN;
         break;
      case ir_intrinsic_ssbo_atomic_max:
         opcode = TGSI_OPCODE_ATOMIMAX;
         break;
      case ir_intrinsic_ssbo_atomic_and:
         opcode = TGSI_OPCODE_ATOMAND;
         break;
      case ir_intrinsic_ssbo_atomic_or:
         opcode = TGSI_OPCODE_ATOMOR;
         break;
      case ir_intrinsic_ssbo_atomic_xor:
         opcode = TGSI_OPCODE_ATOMXOR;
         break;
      case ir_intrinsic_ssbo_atomic_exchange:
         opcode = TGSI_OPCODE_ATOMXCHG;
         break;
      case ir_intrinsic_ssbo_atomic_comp_swap:
         opcode = TGSI_OPCODE_ATOMCAS;
         param = param->get_next();
         val = ((ir_instruction *)param)->as_rvalue();
         val->accept(this);
         data2 = this->result;
         break;
      default:
         assert(!"Unexpected intrinsic");
         return;
      }

      inst = emit_asm(ir, opcode, dst, off, data, data2);
   }

   param = param->get_next();
   ir_constant *access = NULL;
   if (!param->is_tail_sentinel()) {
      access = ((ir_instruction *)param)->as_constant();
      assert(access);
   }

   add_buffer_to_load_and_stores(inst, &buffer, &this->instructions, access);
}

void
glsl_to_tgsi_visitor::visit_membar_intrinsic(ir_call *ir)
{
   switch (ir->callee->intrinsic_id) {
   case ir_intrinsic_memory_barrier:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_SHADER_BUFFER |
                                  TGSI_MEMBAR_ATOMIC_BUFFER |
                                  TGSI_MEMBAR_SHADER_IMAGE |
                                  TGSI_MEMBAR_SHARED));
      break;
   case ir_intrinsic_memory_barrier_atomic_counter:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_ATOMIC_BUFFER));
      break;
   case ir_intrinsic_memory_barrier_buffer:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_SHADER_BUFFER));
      break;
   case ir_intrinsic_memory_barrier_image:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_SHADER_IMAGE));
      break;
   case ir_intrinsic_memory_barrier_shared:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_SHARED));
      break;
   case ir_intrinsic_group_memory_barrier:
      emit_asm(ir, TGSI_OPCODE_MEMBAR, undef_dst,
               st_src_reg_for_int(TGSI_MEMBAR_SHADER_BUFFER |
                                  TGSI_MEMBAR_ATOMIC_BUFFER |
                                  TGSI_MEMBAR_SHADER_IMAGE |
                                  TGSI_MEMBAR_SHARED |
                                  TGSI_MEMBAR_THREAD_GROUP));
      break;
   default:
      assert(!"Unexpected memory barrier intrinsic");
   }
}

void
glsl_to_tgsi_visitor::visit_shared_intrinsic(ir_call *ir)
{
   exec_node *param = ir->actual_parameters.get_head();

   ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

   st_src_reg buffer(PROGRAM_MEMORY, 0, GLSL_TYPE_UINT);

   /* Calculate the surface offset */
   offset->accept(this);
   st_src_reg off = this->result;

   st_dst_reg dst = undef_dst;
   if (ir->return_deref) {
      ir->return_deref->accept(this);
      dst = st_dst_reg(this->result);
      dst.writemask = (1 << ir->return_deref->type->vector_elements) - 1;
   }

   glsl_to_tgsi_instruction *inst;

   if (ir->callee->intrinsic_id == ir_intrinsic_shared_load) {
      inst = emit_asm(ir, TGSI_OPCODE_LOAD, dst, off);
      inst->resource = buffer;
   } else if (ir->callee->intrinsic_id == ir_intrinsic_shared_store) {
      param = param->get_next();
      ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();
      val->accept(this);

      param = param->get_next();
      ir_constant *write_mask = ((ir_instruction *)param)->as_constant();
      assert(write_mask);
      dst.writemask = write_mask->value.u[0];

      dst.type = this->result.type;
      inst = emit_asm(ir, TGSI_OPCODE_STORE, dst, off, this->result);
      inst->resource = buffer;
   } else {
      param = param->get_next();
      ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();
      val->accept(this);

      st_src_reg data = this->result, data2 = undef_src;
      enum tgsi_opcode opcode;
      switch (ir->callee->intrinsic_id) {
      case ir_intrinsic_shared_atomic_add:
         opcode = TGSI_OPCODE_ATOMUADD;
         break;
      case ir_intrinsic_shared_atomic_min:
         opcode = TGSI_OPCODE_ATOMIMIN;
         break;
      case ir_intrinsic_shared_atomic_max:
         opcode = TGSI_OPCODE_ATOMIMAX;
         break;
      case ir_intrinsic_shared_atomic_and:
         opcode = TGSI_OPCODE_ATOMAND;
         break;
      case ir_intrinsic_shared_atomic_or:
         opcode = TGSI_OPCODE_ATOMOR;
         break;
      case ir_intrinsic_shared_atomic_xor:
         opcode = TGSI_OPCODE_ATOMXOR;
         break;
      case ir_intrinsic_shared_atomic_exchange:
         opcode = TGSI_OPCODE_ATOMXCHG;
         break;
      case ir_intrinsic_shared_atomic_comp_swap:
         opcode = TGSI_OPCODE_ATOMCAS;
         param = param->get_next();
         val = ((ir_instruction *)param)->as_rvalue();
         val->accept(this);
         data2 = this->result;
         break;
      default:
         assert(!"Unexpected intrinsic");
         return;
      }

      inst = emit_asm(ir, opcode, dst, off, data, data2);
      inst->resource = buffer;
   }
}

static void
get_image_qualifiers(ir_dereference *ir, const glsl_type **type,
                     bool *memory_coherent, bool *memory_volatile,
                     bool *memory_restrict, unsigned *image_format)
{

   switch (ir->ir_type) {
   case ir_type_dereference_record: {
      ir_dereference_record *deref_record = ir->as_dereference_record();
      const glsl_type *struct_type = deref_record->record->type;
      int fild_idx = deref_record->field_idx;

      *type = struct_type->fields.structure[fild_idx].type->without_array();
      *memory_coherent =
         struct_type->fields.structure[fild_idx].memory_coherent;
      *memory_volatile =
         struct_type->fields.structure[fild_idx].memory_volatile;
      *memory_restrict =
         struct_type->fields.structure[fild_idx].memory_restrict;
      *image_format =
         struct_type->fields.structure[fild_idx].image_format;
      break;
   }

   case ir_type_dereference_array: {
      ir_dereference_array *deref_arr = ir->as_dereference_array();
      get_image_qualifiers((ir_dereference *)deref_arr->array, type,
                           memory_coherent, memory_volatile, memory_restrict,
                           image_format);
      break;
   }

   case ir_type_dereference_variable: {
      ir_variable *var = ir->variable_referenced();

      *type = var->type->without_array();
      *memory_coherent = var->data.memory_coherent;
      *memory_volatile = var->data.memory_volatile;
      *memory_restrict = var->data.memory_restrict;
      *image_format = var->data.image_format;
      break;
   }

   default:
      break;
   }
}

void
glsl_to_tgsi_visitor::visit_image_intrinsic(ir_call *ir)
{
   exec_node *param = ir->actual_parameters.get_head();

   ir_dereference *img = (ir_dereference *)param;
   const ir_variable *imgvar = img->variable_referenced();
   unsigned sampler_array_size = 1, sampler_base = 0;
   bool memory_coherent = false, memory_volatile = false, memory_restrict = false;
   unsigned image_format = 0;
   const glsl_type *type = NULL;

   get_image_qualifiers(img, &type, &memory_coherent, &memory_volatile,
                        &memory_restrict, &image_format);

   st_src_reg reladdr;
   st_src_reg image(PROGRAM_IMAGE, 0, GLSL_TYPE_UINT);
   uint16_t index = 0;
   get_deref_offsets(img, &sampler_array_size, &sampler_base,
                     &index, &reladdr, !imgvar->contains_bindless());

   image.index = index;
   if (reladdr.file != PROGRAM_UNDEFINED) {
      image.reladdr = ralloc(mem_ctx, st_src_reg);
      *image.reladdr = reladdr;
      emit_arl(ir, sampler_reladdr, reladdr);
   }

   st_dst_reg dst = undef_dst;
   if (ir->return_deref) {
      ir->return_deref->accept(this);
      dst = st_dst_reg(this->result);
      dst.writemask = (1 << ir->return_deref->type->vector_elements) - 1;
   }

   glsl_to_tgsi_instruction *inst;

   st_src_reg bindless;
   if (imgvar->contains_bindless()) {
      img->accept(this);
      bindless = this->result;
   }

   if (ir->callee->intrinsic_id == ir_intrinsic_image_size) {
      dst.writemask = WRITEMASK_XYZ;
      inst = emit_asm(ir, TGSI_OPCODE_RESQ, dst);
   } else if (ir->callee->intrinsic_id == ir_intrinsic_image_samples) {
      st_src_reg res = get_temp(glsl_type::ivec4_type);
      st_dst_reg dstres = st_dst_reg(res);
      dstres.writemask = WRITEMASK_W;
      inst = emit_asm(ir, TGSI_OPCODE_RESQ, dstres);
      res.swizzle = SWIZZLE_WWWW;
      emit_asm(ir, TGSI_OPCODE_MOV, dst, res);
   } else {
      st_src_reg arg1 = undef_src, arg2 = undef_src;
      st_src_reg coord;
      st_dst_reg coord_dst;
      coord = get_temp(glsl_type::ivec4_type);
      coord_dst = st_dst_reg(coord);
      coord_dst.writemask = (1 << type->coordinate_components()) - 1;
      param = param->get_next();
      ((ir_dereference *)param)->accept(this);
      emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, this->result);
      coord.swizzle = SWIZZLE_XXXX;
      switch (type->coordinate_components()) {
      case 4: assert(!"unexpected coord count");
      /* fallthrough */
      case 3: coord.swizzle |= SWIZZLE_Z << 6;
      /* fallthrough */
      case 2: coord.swizzle |= SWIZZLE_Y << 3;
      }

      if (type->sampler_dimensionality == GLSL_SAMPLER_DIM_MS) {
         param = param->get_next();
         ((ir_dereference *)param)->accept(this);
         st_src_reg sample = this->result;
         sample.swizzle = SWIZZLE_XXXX;
         coord_dst.writemask = WRITEMASK_W;
         emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, sample);
         coord.swizzle |= SWIZZLE_W << 9;
      }

      param = param->get_next();
      if (!param->is_tail_sentinel()) {
         ((ir_dereference *)param)->accept(this);
         arg1 = this->result;
         param = param->get_next();
      }

      if (!param->is_tail_sentinel()) {
         ((ir_dereference *)param)->accept(this);
         arg2 = this->result;
         param = param->get_next();
      }

      assert(param->is_tail_sentinel());

      enum tgsi_opcode opcode;
      switch (ir->callee->intrinsic_id) {
      case ir_intrinsic_image_load:
         opcode = TGSI_OPCODE_LOAD;
         break;
      case ir_intrinsic_image_store:
         opcode = TGSI_OPCODE_STORE;
         break;
      case ir_intrinsic_image_atomic_add:
         opcode = TGSI_OPCODE_ATOMUADD;
         break;
      case ir_intrinsic_image_atomic_min:
         opcode = TGSI_OPCODE_ATOMIMIN;
         break;
      case ir_intrinsic_image_atomic_max:
         opcode = TGSI_OPCODE_ATOMIMAX;
         break;
      case ir_intrinsic_image_atomic_and:
         opcode = TGSI_OPCODE_ATOMAND;
         break;
      case ir_intrinsic_image_atomic_or:
         opcode = TGSI_OPCODE_ATOMOR;
         break;
      case ir_intrinsic_image_atomic_xor:
         opcode = TGSI_OPCODE_ATOMXOR;
         break;
      case ir_intrinsic_image_atomic_exchange:
         opcode = TGSI_OPCODE_ATOMXCHG;
         break;
      case ir_intrinsic_image_atomic_comp_swap:
         opcode = TGSI_OPCODE_ATOMCAS;
         break;
      default:
         assert(!"Unexpected intrinsic");
         return;
      }

      inst = emit_asm(ir, opcode, dst, coord, arg1, arg2);
      if (opcode == TGSI_OPCODE_STORE)
         inst->dst[0].writemask = WRITEMASK_XYZW;
   }

   if (imgvar->contains_bindless()) {
      inst->resource = bindless;
      inst->resource.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y,
                                             SWIZZLE_X, SWIZZLE_Y);
   } else {
      inst->resource = image;
      inst->sampler_array_size = sampler_array_size;
      inst->sampler_base = sampler_base;
   }

   inst->tex_target = type->sampler_index();
   inst->image_format = st_mesa_format_to_pipe_format(st_context(ctx),
         _mesa_get_shader_image_format(image_format));

   if (memory_coherent)
      inst->buffer_access |= TGSI_MEMORY_COHERENT;
   if (memory_restrict)
      inst->buffer_access |= TGSI_MEMORY_RESTRICT;
   if (memory_volatile)
      inst->buffer_access |= TGSI_MEMORY_VOLATILE;
}

void
glsl_to_tgsi_visitor::visit_generic_intrinsic(ir_call *ir, enum tgsi_opcode op)
{
   ir->return_deref->accept(this);
   st_dst_reg dst = st_dst_reg(this->result);

   dst.writemask = u_bit_consecutive(0, ir->return_deref->var->type->vector_elements);

   st_src_reg src[4] = { undef_src, undef_src, undef_src, undef_src };
   unsigned num_src = 0;
   foreach_in_list(ir_rvalue, param, &ir->actual_parameters) {
      assert(num_src < ARRAY_SIZE(src));

      this->result.file = PROGRAM_UNDEFINED;
      param->accept(this);
      assert(this->result.file != PROGRAM_UNDEFINED);

      src[num_src] = this->result;
      num_src++;
   }

   emit_asm(ir, op, dst, src[0], src[1], src[2], src[3]);
}

void
glsl_to_tgsi_visitor::visit(ir_call *ir)
{
   ir_function_signature *sig = ir->callee;

   /* Filter out intrinsics */
   switch (sig->intrinsic_id) {
   case ir_intrinsic_atomic_counter_read:
   case ir_intrinsic_atomic_counter_increment:
   case ir_intrinsic_atomic_counter_predecrement:
   case ir_intrinsic_atomic_counter_add:
   case ir_intrinsic_atomic_counter_min:
   case ir_intrinsic_atomic_counter_max:
   case ir_intrinsic_atomic_counter_and:
   case ir_intrinsic_atomic_counter_or:
   case ir_intrinsic_atomic_counter_xor:
   case ir_intrinsic_atomic_counter_exchange:
   case ir_intrinsic_atomic_counter_comp_swap:
      visit_atomic_counter_intrinsic(ir);
      return;

   case ir_intrinsic_ssbo_load:
   case ir_intrinsic_ssbo_store:
   case ir_intrinsic_ssbo_atomic_add:
   case ir_intrinsic_ssbo_atomic_min:
   case ir_intrinsic_ssbo_atomic_max:
   case ir_intrinsic_ssbo_atomic_and:
   case ir_intrinsic_ssbo_atomic_or:
   case ir_intrinsic_ssbo_atomic_xor:
   case ir_intrinsic_ssbo_atomic_exchange:
   case ir_intrinsic_ssbo_atomic_comp_swap:
      visit_ssbo_intrinsic(ir);
      return;

   case ir_intrinsic_memory_barrier:
   case ir_intrinsic_memory_barrier_atomic_counter:
   case ir_intrinsic_memory_barrier_buffer:
   case ir_intrinsic_memory_barrier_image:
   case ir_intrinsic_memory_barrier_shared:
   case ir_intrinsic_group_memory_barrier:
      visit_membar_intrinsic(ir);
      return;

   case ir_intrinsic_shared_load:
   case ir_intrinsic_shared_store:
   case ir_intrinsic_shared_atomic_add:
   case ir_intrinsic_shared_atomic_min:
   case ir_intrinsic_shared_atomic_max:
   case ir_intrinsic_shared_atomic_and:
   case ir_intrinsic_shared_atomic_or:
   case ir_intrinsic_shared_atomic_xor:
   case ir_intrinsic_shared_atomic_exchange:
   case ir_intrinsic_shared_atomic_comp_swap:
      visit_shared_intrinsic(ir);
      return;

   case ir_intrinsic_image_load:
   case ir_intrinsic_image_store:
   case ir_intrinsic_image_atomic_add:
   case ir_intrinsic_image_atomic_min:
   case ir_intrinsic_image_atomic_max:
   case ir_intrinsic_image_atomic_and:
   case ir_intrinsic_image_atomic_or:
   case ir_intrinsic_image_atomic_xor:
   case ir_intrinsic_image_atomic_exchange:
   case ir_intrinsic_image_atomic_comp_swap:
   case ir_intrinsic_image_size:
   case ir_intrinsic_image_samples:
      visit_image_intrinsic(ir);
      return;

   case ir_intrinsic_shader_clock:
      visit_generic_intrinsic(ir, TGSI_OPCODE_CLOCK);
      return;

   case ir_intrinsic_vote_all:
      visit_generic_intrinsic(ir, TGSI_OPCODE_VOTE_ALL);
      return;
   case ir_intrinsic_vote_any:
      visit_generic_intrinsic(ir, TGSI_OPCODE_VOTE_ANY);
      return;
   case ir_intrinsic_vote_eq:
      visit_generic_intrinsic(ir, TGSI_OPCODE_VOTE_EQ);
      return;
   case ir_intrinsic_ballot:
      visit_generic_intrinsic(ir, TGSI_OPCODE_BALLOT);
      return;
   case ir_intrinsic_read_first_invocation:
      visit_generic_intrinsic(ir, TGSI_OPCODE_READ_FIRST);
      return;
   case ir_intrinsic_read_invocation:
      visit_generic_intrinsic(ir, TGSI_OPCODE_READ_INVOC);
      return;

   case ir_intrinsic_invalid:
   case ir_intrinsic_generic_load:
   case ir_intrinsic_generic_store:
   case ir_intrinsic_generic_atomic_add:
   case ir_intrinsic_generic_atomic_and:
   case ir_intrinsic_generic_atomic_or:
   case ir_intrinsic_generic_atomic_xor:
   case ir_intrinsic_generic_atomic_min:
   case ir_intrinsic_generic_atomic_max:
   case ir_intrinsic_generic_atomic_exchange:
   case ir_intrinsic_generic_atomic_comp_swap:
      unreachable("Invalid intrinsic");
   }
}

void
glsl_to_tgsi_visitor::calc_deref_offsets(ir_dereference *tail,
                                         unsigned *array_elements,
                                         uint16_t *index,
                                         st_src_reg *indirect,
                                         unsigned *location)
{
   switch (tail->ir_type) {
   case ir_type_dereference_record: {
      ir_dereference_record *deref_record = tail->as_dereference_record();
      const glsl_type *struct_type = deref_record->record->type;
      int field_index = deref_record->field_idx;

      calc_deref_offsets(deref_record->record->as_dereference(), array_elements, index, indirect, location);

      assert(field_index >= 0);
      *location += struct_type->record_location_offset(field_index);
      break;
   }

   case ir_type_dereference_array: {
      ir_dereference_array *deref_arr = tail->as_dereference_array();

      void *mem_ctx = ralloc_parent(deref_arr);
      ir_constant *array_index =
         deref_arr->array_index->constant_expression_value(mem_ctx);

      if (!array_index) {
         st_src_reg temp_reg;
         st_dst_reg temp_dst;

         temp_reg = get_temp(glsl_type::uint_type);
         temp_dst = st_dst_reg(temp_reg);
         temp_dst.writemask = 1;

         deref_arr->array_index->accept(this);
         if (*array_elements != 1)
            emit_asm(NULL, TGSI_OPCODE_MUL, temp_dst, this->result, st_src_reg_for_int(*array_elements));
         else
            emit_asm(NULL, TGSI_OPCODE_MOV, temp_dst, this->result);

         if (indirect->file == PROGRAM_UNDEFINED)
            *indirect = temp_reg;
         else {
            temp_dst = st_dst_reg(*indirect);
            temp_dst.writemask = 1;
            emit_asm(NULL, TGSI_OPCODE_ADD, temp_dst, *indirect, temp_reg);
         }
      } else
         *index += array_index->value.u[0] * *array_elements;

      *array_elements *= deref_arr->array->type->length;

      calc_deref_offsets(deref_arr->array->as_dereference(), array_elements, index, indirect, location);
      break;
   }
   default:
      break;
   }
}

void
glsl_to_tgsi_visitor::get_deref_offsets(ir_dereference *ir,
                                        unsigned *array_size,
                                        unsigned *base,
                                        uint16_t *index,
                                        st_src_reg *reladdr,
                                        bool opaque)
{
   GLuint shader = _mesa_program_enum_to_shader_stage(this->prog->Target);
   unsigned location = 0;
   ir_variable *var = ir->variable_referenced();

   memset(reladdr, 0, sizeof(*reladdr));
   reladdr->file = PROGRAM_UNDEFINED;

   *base = 0;
   *array_size = 1;

   assert(var);
   location = var->data.location;
   calc_deref_offsets(ir, array_size, index, reladdr, &location);

   /*
    * If we end up with no indirect then adjust the base to the index,
    * and set the array size to 1.
    */
   if (reladdr->file == PROGRAM_UNDEFINED) {
      *base = *index;
      *array_size = 1;
   }

   if (opaque) {
      assert(location != 0xffffffff);
      *base += this->shader_program->data->UniformStorage[location].opaque[shader].index;
      *index += this->shader_program->data->UniformStorage[location].opaque[shader].index;
   }
}

st_src_reg
glsl_to_tgsi_visitor::canonicalize_gather_offset(st_src_reg offset)
{
   if (offset.reladdr || offset.reladdr2 ||
       offset.has_index2 ||
       offset.file == PROGRAM_UNIFORM ||
       offset.file == PROGRAM_CONSTANT ||
       offset.file == PROGRAM_STATE_VAR) {
      st_src_reg tmp = get_temp(glsl_type::ivec2_type);
      st_dst_reg tmp_dst = st_dst_reg(tmp);
      tmp_dst.writemask = WRITEMASK_XY;
      emit_asm(NULL, TGSI_OPCODE_MOV, tmp_dst, offset);
      return tmp;
   }

   return offset;
}

void
glsl_to_tgsi_visitor::visit(ir_texture *ir)
{
   st_src_reg result_src, coord, cube_sc, lod_info, projector, dx, dy;
   st_src_reg offset[MAX_GLSL_TEXTURE_OFFSET], sample_index, component;
   st_src_reg levels_src, reladdr;
   st_dst_reg result_dst, coord_dst, cube_sc_dst;
   glsl_to_tgsi_instruction *inst = NULL;
   enum tgsi_opcode opcode = TGSI_OPCODE_NOP;
   const glsl_type *sampler_type = ir->sampler->type;
   unsigned sampler_array_size = 1, sampler_base = 0;
   bool is_cube_array = false, is_cube_shadow = false;
   ir_variable *var = ir->sampler->variable_referenced();
   unsigned i;

   /* if we are a cube array sampler or a cube shadow */
   if (sampler_type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE) {
      is_cube_array = sampler_type->sampler_array;
      is_cube_shadow = sampler_type->sampler_shadow;
   }

   if (ir->coordinate) {
      ir->coordinate->accept(this);

      /* Put our coords in a temp.  We'll need to modify them for shadow,
       * projection, or LOD, so the only case we'd use it as-is is if
       * we're doing plain old texturing.  The optimization passes on
       * glsl_to_tgsi_visitor should handle cleaning up our mess in that case.
       */
      coord = get_temp(glsl_type::vec4_type);
      coord_dst = st_dst_reg(coord);
      coord_dst.writemask = (1 << ir->coordinate->type->vector_elements) - 1;
      emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, this->result);
   }

   if (ir->projector) {
      ir->projector->accept(this);
      projector = this->result;
   }

   /* Storage for our result.  Ideally for an assignment we'd be using
    * the actual storage for the result here, instead.
    */
   result_src = get_temp(ir->type);
   result_dst = st_dst_reg(result_src);
   result_dst.writemask = (1 << ir->type->vector_elements) - 1;

   switch (ir->op) {
   case ir_tex:
      opcode = (is_cube_array && ir->shadow_comparator) ? TGSI_OPCODE_TEX2 : TGSI_OPCODE_TEX;
      if (ir->offset) {
         ir->offset->accept(this);
         offset[0] = this->result;
      }
      break;
   case ir_txb:
      if (is_cube_array || is_cube_shadow) {
         opcode = TGSI_OPCODE_TXB2;
      }
      else {
         opcode = TGSI_OPCODE_TXB;
      }
      ir->lod_info.bias->accept(this);
      lod_info = this->result;
      if (ir->offset) {
         ir->offset->accept(this);
         offset[0] = this->result;
      }
      break;
   case ir_txl:
      if (this->has_tex_txf_lz && ir->lod_info.lod->is_zero()) {
         opcode = TGSI_OPCODE_TEX_LZ;
      } else {
         opcode = is_cube_array ? TGSI_OPCODE_TXL2 : TGSI_OPCODE_TXL;
         ir->lod_info.lod->accept(this);
         lod_info = this->result;
      }
      if (ir->offset) {
         ir->offset->accept(this);
         offset[0] = this->result;
      }
      break;
   case ir_txd:
      opcode = TGSI_OPCODE_TXD;
      ir->lod_info.grad.dPdx->accept(this);
      dx = this->result;
      ir->lod_info.grad.dPdy->accept(this);
      dy = this->result;
      if (ir->offset) {
         ir->offset->accept(this);
         offset[0] = this->result;
      }
      break;
   case ir_txs:
      opcode = TGSI_OPCODE_TXQ;
      ir->lod_info.lod->accept(this);
      lod_info = this->result;
      break;
   case ir_query_levels:
      opcode = TGSI_OPCODE_TXQ;
      lod_info = undef_src;
      levels_src = get_temp(ir->type);
      break;
   case ir_txf:
      if (this->has_tex_txf_lz && ir->lod_info.lod->is_zero()) {
         opcode = TGSI_OPCODE_TXF_LZ;
      } else {
         opcode = TGSI_OPCODE_TXF;
         ir->lod_info.lod->accept(this);
         lod_info = this->result;
      }
      if (ir->offset) {
         ir->offset->accept(this);
         offset[0] = this->result;
      }
      break;
   case ir_txf_ms:
      opcode = TGSI_OPCODE_TXF;
      ir->lod_info.sample_index->accept(this);
      sample_index = this->result;
      break;
   case ir_tg4:
      opcode = TGSI_OPCODE_TG4;
      ir->lod_info.component->accept(this);
      component = this->result;
      if (ir->offset) {
         ir->offset->accept(this);
         if (ir->offset->type->is_array()) {
            const glsl_type *elt_type = ir->offset->type->fields.array;
            for (i = 0; i < ir->offset->type->length; i++) {
               offset[i] = this->result;
               offset[i].index += i * type_size(elt_type);
               offset[i].type = elt_type->base_type;
               offset[i].swizzle = swizzle_for_size(elt_type->vector_elements);
               offset[i] = canonicalize_gather_offset(offset[i]);
            }
         } else {
            offset[0] = canonicalize_gather_offset(this->result);
         }
      }
      break;
   case ir_lod:
      opcode = TGSI_OPCODE_LODQ;
      break;
   case ir_texture_samples:
      opcode = TGSI_OPCODE_TXQS;
      break;
   case ir_samples_identical:
      unreachable("Unexpected ir_samples_identical opcode");
   }

   if (ir->projector) {
      if (opcode == TGSI_OPCODE_TEX) {
         /* Slot the projector in as the last component of the coord. */
         coord_dst.writemask = WRITEMASK_W;
         emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, projector);
         coord_dst.writemask = WRITEMASK_XYZW;
         opcode = TGSI_OPCODE_TXP;
      } else {
         st_src_reg coord_w = coord;
         coord_w.swizzle = SWIZZLE_WWWW;

         /* For the other TEX opcodes there's no projective version
          * since the last slot is taken up by LOD info.  Do the
          * projective divide now.
          */
         coord_dst.writemask = WRITEMASK_W;
         emit_asm(ir, TGSI_OPCODE_RCP, coord_dst, projector);

         /* In the case where we have to project the coordinates "by hand,"
          * the shadow comparator value must also be projected.
          */
         st_src_reg tmp_src = coord;
         if (ir->shadow_comparator) {
            /* Slot the shadow value in as the second to last component of the
             * coord.
             */
            ir->shadow_comparator->accept(this);

            tmp_src = get_temp(glsl_type::vec4_type);
            st_dst_reg tmp_dst = st_dst_reg(tmp_src);

            /* Projective division not allowed for array samplers. */
            assert(!sampler_type->sampler_array);

            tmp_dst.writemask = WRITEMASK_Z;
            emit_asm(ir, TGSI_OPCODE_MOV, tmp_dst, this->result);

            tmp_dst.writemask = WRITEMASK_XY;
            emit_asm(ir, TGSI_OPCODE_MOV, tmp_dst, coord);
         }

         coord_dst.writemask = WRITEMASK_XYZ;
         emit_asm(ir, TGSI_OPCODE_MUL, coord_dst, tmp_src, coord_w);

         coord_dst.writemask = WRITEMASK_XYZW;
         coord.swizzle = SWIZZLE_XYZW;
      }
   }

   /* If projection is done and the opcode is not TGSI_OPCODE_TXP, then the
    * shadow comparator was put in the correct place (and projected) by the
    * code, above, that handles by-hand projection.
    */
   if (ir->shadow_comparator && (!ir->projector || opcode == TGSI_OPCODE_TXP)) {
      /* Slot the shadow value in as the second to last component of the
       * coord.
       */
      ir->shadow_comparator->accept(this);

      if (is_cube_array) {
         cube_sc = get_temp(glsl_type::float_type);
         cube_sc_dst = st_dst_reg(cube_sc);
         cube_sc_dst.writemask = WRITEMASK_X;
         emit_asm(ir, TGSI_OPCODE_MOV, cube_sc_dst, this->result);
         cube_sc_dst.writemask = WRITEMASK_X;
      }
      else {
         if ((sampler_type->sampler_dimensionality == GLSL_SAMPLER_DIM_2D &&
              sampler_type->sampler_array) ||
             sampler_type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE) {
            coord_dst.writemask = WRITEMASK_W;
         } else {
            coord_dst.writemask = WRITEMASK_Z;
         }
         emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, this->result);
         coord_dst.writemask = WRITEMASK_XYZW;
      }
   }

   if (ir->op == ir_txf_ms) {
      coord_dst.writemask = WRITEMASK_W;
      emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, sample_index);
      coord_dst.writemask = WRITEMASK_XYZW;
   } else if (opcode == TGSI_OPCODE_TXL || opcode == TGSI_OPCODE_TXB ||
       opcode == TGSI_OPCODE_TXF) {
      /* TGSI stores LOD or LOD bias in the last channel of the coords. */
      coord_dst.writemask = WRITEMASK_W;
      emit_asm(ir, TGSI_OPCODE_MOV, coord_dst, lod_info);
      coord_dst.writemask = WRITEMASK_XYZW;
   }

   st_src_reg sampler(PROGRAM_SAMPLER, 0, GLSL_TYPE_UINT);

   uint16_t index = 0;
   get_deref_offsets(ir->sampler, &sampler_array_size, &sampler_base,
                     &index, &reladdr, !var->contains_bindless());

   sampler.index = index;
   if (reladdr.file != PROGRAM_UNDEFINED) {
      sampler.reladdr = ralloc(mem_ctx, st_src_reg);
      *sampler.reladdr = reladdr;
      emit_arl(ir, sampler_reladdr, reladdr);
   }

   st_src_reg bindless;
   if (var->contains_bindless()) {
      ir->sampler->accept(this);
      bindless = this->result;
   }

   if (opcode == TGSI_OPCODE_TXD)
      inst = emit_asm(ir, opcode, result_dst, coord, dx, dy);
   else if (opcode == TGSI_OPCODE_TXQ) {
      if (ir->op == ir_query_levels) {
         /* the level is stored in W */
         inst = emit_asm(ir, opcode, st_dst_reg(levels_src), lod_info);
         result_dst.writemask = WRITEMASK_X;
         levels_src.swizzle = SWIZZLE_WWWW;
         emit_asm(ir, TGSI_OPCODE_MOV, result_dst, levels_src);
      } else
         inst = emit_asm(ir, opcode, result_dst, lod_info);
   } else if (opcode == TGSI_OPCODE_TXQS) {
      inst = emit_asm(ir, opcode, result_dst);
   } else if (opcode == TGSI_OPCODE_TXL2 || opcode == TGSI_OPCODE_TXB2) {
      inst = emit_asm(ir, opcode, result_dst, coord, lod_info);
   } else if (opcode == TGSI_OPCODE_TEX2) {
      inst = emit_asm(ir, opcode, result_dst, coord, cube_sc);
   } else if (opcode == TGSI_OPCODE_TG4) {
      if (is_cube_array && ir->shadow_comparator) {
         inst = emit_asm(ir, opcode, result_dst, coord, cube_sc);
      } else {
         inst = emit_asm(ir, opcode, result_dst, coord, component);
      }
   } else
      inst = emit_asm(ir, opcode, result_dst, coord);

   if (ir->shadow_comparator)
      inst->tex_shadow = GL_TRUE;

   if (var->contains_bindless()) {
      inst->resource = bindless;
      inst->resource.swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y,
                                             SWIZZLE_X, SWIZZLE_Y);
   } else {
      inst->resource = sampler;
      inst->sampler_array_size = sampler_array_size;
      inst->sampler_base = sampler_base;
   }

   if (ir->offset) {
      if (!inst->tex_offsets)
         inst->tex_offsets = rzalloc_array(inst, st_src_reg,
                                           MAX_GLSL_TEXTURE_OFFSET);

      for (i = 0; i < MAX_GLSL_TEXTURE_OFFSET &&
                  offset[i].file != PROGRAM_UNDEFINED; i++)
         inst->tex_offsets[i] = offset[i];
      inst->tex_offset_num_offset = i;
   }

   inst->tex_target = sampler_type->sampler_index();
   inst->tex_type = ir->type->base_type;

   this->result = result_src;
}

void
glsl_to_tgsi_visitor::visit(ir_return *ir)
{
   assert(!ir->get_value());

   emit_asm(ir, TGSI_OPCODE_RET);
}

void
glsl_to_tgsi_visitor::visit(ir_discard *ir)
{
   if (ir->condition) {
      ir->condition->accept(this);
      st_src_reg condition = this->result;

      /* Convert the bool condition to a float so we can negate. */
      if (native_integers) {
         st_src_reg temp = get_temp(ir->condition->type);
         emit_asm(ir, TGSI_OPCODE_AND, st_dst_reg(temp),
              condition, st_src_reg_for_float(1.0));
         condition = temp;
      }

      condition.negate = ~condition.negate;
      emit_asm(ir, TGSI_OPCODE_KILL_IF, undef_dst, condition);
   } else {
      /* unconditional kil */
      emit_asm(ir, TGSI_OPCODE_KILL);
   }
}

void
glsl_to_tgsi_visitor::visit(ir_if *ir)
{
   enum tgsi_opcode if_opcode;
   glsl_to_tgsi_instruction *if_inst;

   ir->condition->accept(this);
   assert(this->result.file != PROGRAM_UNDEFINED);

   if_opcode = native_integers ? TGSI_OPCODE_UIF : TGSI_OPCODE_IF;

   if_inst = emit_asm(ir->condition, if_opcode, undef_dst, this->result);

   this->instructions.push_tail(if_inst);

   visit_exec_list(&ir->then_instructions, this);

   if (!ir->else_instructions.is_empty()) {
      emit_asm(ir->condition, TGSI_OPCODE_ELSE);
      visit_exec_list(&ir->else_instructions, this);
   }

   if_inst = emit_asm(ir->condition, TGSI_OPCODE_ENDIF);
}


void
glsl_to_tgsi_visitor::visit(ir_emit_vertex *ir)
{
   assert(this->prog->Target == GL_GEOMETRY_PROGRAM_NV);

   ir->stream->accept(this);
   emit_asm(ir, TGSI_OPCODE_EMIT, undef_dst, this->result);
}

void
glsl_to_tgsi_visitor::visit(ir_end_primitive *ir)
{
   assert(this->prog->Target == GL_GEOMETRY_PROGRAM_NV);

   ir->stream->accept(this);
   emit_asm(ir, TGSI_OPCODE_ENDPRIM, undef_dst, this->result);
}

void
glsl_to_tgsi_visitor::visit(ir_barrier *ir)
{
   assert(this->prog->Target == GL_TESS_CONTROL_PROGRAM_NV ||
          this->prog->Target == GL_COMPUTE_PROGRAM_NV);

   emit_asm(ir, TGSI_OPCODE_BARRIER);
}

glsl_to_tgsi_visitor::glsl_to_tgsi_visitor()
{
   STATIC_ASSERT(sizeof(samplers_used) * 8 >= PIPE_MAX_SAMPLERS);

   result.file = PROGRAM_UNDEFINED;
   next_temp = 1;
   array_sizes = NULL;
   max_num_arrays = 0;
   next_array = 0;
   num_inputs = 0;
   num_outputs = 0;
   num_input_arrays = 0;
   num_output_arrays = 0;
   num_atomics = 0;
   num_atomic_arrays = 0;
   num_immediates = 0;
   num_address_regs = 0;
   samplers_used = 0;
   images_used = 0;
   indirect_addr_consts = false;
   wpos_transform_const = -1;
   native_integers = false;
   mem_ctx = ralloc_context(NULL);
   ctx = NULL;
   prog = NULL;
   precise = 0;
   shader_program = NULL;
   shader = NULL;
   options = NULL;
   have_sqrt = false;
   have_fma = false;
   use_shared_memory = false;
   has_tex_txf_lz = false;
   variables = NULL;
}

static void var_destroy(struct hash_entry *entry)
{
   variable_storage *storage = (variable_storage *)entry->data;

   delete storage;
}

glsl_to_tgsi_visitor::~glsl_to_tgsi_visitor()
{
   _mesa_hash_table_destroy(variables, var_destroy);
   free(array_sizes);
   ralloc_free(mem_ctx);
}

extern "C" void free_glsl_to_tgsi_visitor(glsl_to_tgsi_visitor *v)
{
   delete v;
}


/**
 * Count resources used by the given gpu program (number of texture
 * samplers, etc).
 */
static void
count_resources(glsl_to_tgsi_visitor *v, gl_program *prog)
{
   v->samplers_used = 0;
   v->images_used = 0;
   prog->info.textures_used_by_txf = 0;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &v->instructions) {
      if (inst->info->is_tex) {
         for (int i = 0; i < inst->sampler_array_size; i++) {
            unsigned idx = inst->sampler_base + i;
            v->samplers_used |= 1u << idx;

            debug_assert(idx < (int)ARRAY_SIZE(v->sampler_types));
            v->sampler_types[idx] = inst->tex_type;
            v->sampler_targets[idx] =
               st_translate_texture_target(inst->tex_target, inst->tex_shadow);

            if (inst->op == TGSI_OPCODE_TXF || inst->op == TGSI_OPCODE_TXF_LZ) {
               prog->info.textures_used_by_txf |= 1u << idx;
            }
         }
      }

      if (inst->tex_target == TEXTURE_EXTERNAL_INDEX)
         prog->ExternalSamplersUsed |= 1 << inst->resource.index;

      if (inst->resource.file != PROGRAM_UNDEFINED && (
                is_resource_instruction(inst->op) ||
                inst->op == TGSI_OPCODE_STORE)) {
         if (inst->resource.file == PROGRAM_MEMORY) {
            v->use_shared_memory = true;
         } else if (inst->resource.file == PROGRAM_IMAGE) {
            for (int i = 0; i < inst->sampler_array_size; i++) {
               unsigned idx = inst->sampler_base + i;
               v->images_used |= 1 << idx;
               v->image_targets[idx] =
                  st_translate_texture_target(inst->tex_target, false);
               v->image_formats[idx] = inst->image_format;
            }
         }
      }
   }
   prog->SamplersUsed = v->samplers_used;

   if (v->shader_program != NULL)
      _mesa_update_shader_textures_used(v->shader_program, prog);
}

/**
 * Returns the mask of channels (bitmask of WRITEMASK_X,Y,Z,W) which
 * are read from the given src in this instruction
 */
static int
get_src_arg_mask(st_dst_reg dst, st_src_reg src)
{
   int read_mask = 0, comp;

   /* Now, given the src swizzle and the written channels, find which
    * components are actually read
    */
   for (comp = 0; comp < 4; ++comp) {
      const unsigned coord = GET_SWZ(src.swizzle, comp);
      assert(coord < 4);
      if (dst.writemask & (1 << comp) && coord <= SWIZZLE_W)
         read_mask |= 1 << coord;
   }

   return read_mask;
}

/**
 * This pass replaces CMP T0, T1 T2 T0 with MOV T0, T2 when the CMP
 * instruction is the first instruction to write to register T0.  There are
 * several lowering passes done in GLSL IR (e.g. branches and
 * relative addressing) that create a large number of conditional assignments
 * that ir_to_mesa converts to CMP instructions like the one mentioned above.
 *
 * Here is why this conversion is safe:
 * CMP T0, T1 T2 T0 can be expanded to:
 * if (T1 < 0.0)
 *   MOV T0, T2;
 * else
 *   MOV T0, T0;
 *
 * If (T1 < 0.0) evaluates to true then our replacement MOV T0, T2 is the same
 * as the original program.  If (T1 < 0.0) evaluates to false, executing
 * MOV T0, T0 will store a garbage value in T0 since T0 is uninitialized.
 * Therefore, it doesn't matter that we are replacing MOV T0, T0 with MOV T0, T2
 * because any instruction that was going to read from T0 after this was going
 * to read a garbage value anyway.
 */
void
glsl_to_tgsi_visitor::simplify_cmp(void)
{
   int tempWritesSize = 0;
   unsigned *tempWrites = NULL;
   unsigned outputWrites[VARYING_SLOT_TESS_MAX];

   memset(outputWrites, 0, sizeof(outputWrites));

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      unsigned prevWriteMask = 0;

      /* Give up if we encounter relative addressing or flow control. */
      if (inst->dst[0].reladdr || inst->dst[0].reladdr2 ||
          inst->dst[1].reladdr || inst->dst[1].reladdr2 ||
          inst->info->is_branch ||
          inst->op == TGSI_OPCODE_CONT ||
          inst->op == TGSI_OPCODE_END ||
          inst->op == TGSI_OPCODE_RET) {
         break;
      }

      if (inst->dst[0].file == PROGRAM_OUTPUT) {
         assert(inst->dst[0].index < (signed)ARRAY_SIZE(outputWrites));
         prevWriteMask = outputWrites[inst->dst[0].index];
         outputWrites[inst->dst[0].index] |= inst->dst[0].writemask;
      } else if (inst->dst[0].file == PROGRAM_TEMPORARY) {
         if (inst->dst[0].index >= tempWritesSize) {
            const int inc = 4096;

            tempWrites = (unsigned*)
                         realloc(tempWrites,
                                 (tempWritesSize + inc) * sizeof(unsigned));
            if (!tempWrites)
               return;

            memset(tempWrites + tempWritesSize, 0, inc * sizeof(unsigned));
            tempWritesSize += inc;
         }

         prevWriteMask = tempWrites[inst->dst[0].index];
         tempWrites[inst->dst[0].index] |= inst->dst[0].writemask;
      } else
         continue;

      /* For a CMP to be considered a conditional write, the destination
       * register and source register two must be the same. */
      if (inst->op == TGSI_OPCODE_CMP
          && !(inst->dst[0].writemask & prevWriteMask)
          && inst->src[2].file == inst->dst[0].file
          && inst->src[2].index == inst->dst[0].index
          && inst->dst[0].writemask ==
             get_src_arg_mask(inst->dst[0], inst->src[2])) {

         inst->op = TGSI_OPCODE_MOV;
         inst->info = tgsi_get_opcode_info(inst->op);
         inst->src[0] = inst->src[1];
      }
   }

   free(tempWrites);
}

static void
rename_temp_handle_src(struct rename_reg_pair *renames, st_src_reg *src)
{
   if (src && src->file == PROGRAM_TEMPORARY) {
      int old_idx = src->index;
      if (renames[old_idx].valid)
         src->index = renames[old_idx].new_reg;
   }
}

/* Replaces all references to a temporary register index with another index. */
void
glsl_to_tgsi_visitor::rename_temp_registers(struct rename_reg_pair *renames)
{
   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      unsigned j;
      for (j = 0; j < num_inst_src_regs(inst); j++) {
         rename_temp_handle_src(renames, &inst->src[j]);
         rename_temp_handle_src(renames, inst->src[j].reladdr);
         rename_temp_handle_src(renames, inst->src[j].reladdr2);
      }

      for (j = 0; j < inst->tex_offset_num_offset; j++) {
         rename_temp_handle_src(renames, &inst->tex_offsets[j]);
         rename_temp_handle_src(renames, inst->tex_offsets[j].reladdr);
         rename_temp_handle_src(renames, inst->tex_offsets[j].reladdr2);
      }

      rename_temp_handle_src(renames, &inst->resource);
      rename_temp_handle_src(renames, inst->resource.reladdr);
      rename_temp_handle_src(renames, inst->resource.reladdr2);

      for (j = 0; j < num_inst_dst_regs(inst); j++) {
         if (inst->dst[j].file == PROGRAM_TEMPORARY) {
            int old_idx = inst->dst[j].index;
            if (renames[old_idx].valid)
               inst->dst[j].index = renames[old_idx].new_reg;
         }
         rename_temp_handle_src(renames, inst->dst[j].reladdr);
         rename_temp_handle_src(renames, inst->dst[j].reladdr2);
      }
   }
}

void
glsl_to_tgsi_visitor::get_first_temp_write(int *first_writes)
{
   int depth = 0; /* loop depth */
   int loop_start = -1; /* index of the first active BGNLOOP (if any) */
   unsigned i = 0, j;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      for (j = 0; j < num_inst_dst_regs(inst); j++) {
         if (inst->dst[j].file == PROGRAM_TEMPORARY) {
            if (first_writes[inst->dst[j].index] == -1)
                first_writes[inst->dst[j].index] = (depth == 0) ? i : loop_start;
         }
      }

      if (inst->op == TGSI_OPCODE_BGNLOOP) {
         if (depth++ == 0)
            loop_start = i;
      } else if (inst->op == TGSI_OPCODE_ENDLOOP) {
         if (--depth == 0)
            loop_start = -1;
      }
      assert(depth >= 0);
      i++;
   }
}

void
glsl_to_tgsi_visitor::get_first_temp_read(int *first_reads)
{
   int depth = 0; /* loop depth */
   int loop_start = -1; /* index of the first active BGNLOOP (if any) */
   unsigned i = 0, j;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      for (j = 0; j < num_inst_src_regs(inst); j++) {
         if (inst->src[j].file == PROGRAM_TEMPORARY) {
            if (first_reads[inst->src[j].index] == -1)
                first_reads[inst->src[j].index] = (depth == 0) ? i : loop_start;
         }
      }
      for (j = 0; j < inst->tex_offset_num_offset; j++) {
         if (inst->tex_offsets[j].file == PROGRAM_TEMPORARY) {
            if (first_reads[inst->tex_offsets[j].index] == -1)
               first_reads[inst->tex_offsets[j].index] = (depth == 0) ? i : loop_start;
         }
      }
      if (inst->op == TGSI_OPCODE_BGNLOOP) {
         if (depth++ == 0)
            loop_start = i;
      } else if (inst->op == TGSI_OPCODE_ENDLOOP) {
         if (--depth == 0)
            loop_start = -1;
      }
      assert(depth >= 0);
      i++;
   }
}

void
glsl_to_tgsi_visitor::get_last_temp_read_first_temp_write(int *last_reads, int *first_writes)
{
   int depth = 0; /* loop depth */
   int loop_start = -1; /* index of the first active BGNLOOP (if any) */
   unsigned i = 0, j;
   int k;
   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      for (j = 0; j < num_inst_src_regs(inst); j++) {
         if (inst->src[j].file == PROGRAM_TEMPORARY)
            last_reads[inst->src[j].index] = (depth == 0) ? i : -2;
      }
      for (j = 0; j < num_inst_dst_regs(inst); j++) {
         if (inst->dst[j].file == PROGRAM_TEMPORARY) {
            if (first_writes[inst->dst[j].index] == -1)
               first_writes[inst->dst[j].index] = (depth == 0) ? i : loop_start;
            last_reads[inst->dst[j].index] = (depth == 0) ? i : -2;
         }
      }
      for (j = 0; j < inst->tex_offset_num_offset; j++) {
         if (inst->tex_offsets[j].file == PROGRAM_TEMPORARY)
            last_reads[inst->tex_offsets[j].index] = (depth == 0) ? i : -2;
      }
      if (inst->op == TGSI_OPCODE_BGNLOOP) {
         if (depth++ == 0)
            loop_start = i;
      } else if (inst->op == TGSI_OPCODE_ENDLOOP) {
         if (--depth == 0) {
            loop_start = -1;
            for (k = 0; k < this->next_temp; k++) {
               if (last_reads[k] == -2) {
                  last_reads[k] = i;
               }
            }
         }
      }
      assert(depth >= 0);
      i++;
   }
}

void
glsl_to_tgsi_visitor::get_last_temp_write(int *last_writes)
{
   int depth = 0; /* loop depth */
   int i = 0, k;
   unsigned j;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      for (j = 0; j < num_inst_dst_regs(inst); j++) {
         if (inst->dst[j].file == PROGRAM_TEMPORARY)
            last_writes[inst->dst[j].index] = (depth == 0) ? i : -2;
      }

      if (inst->op == TGSI_OPCODE_BGNLOOP)
         depth++;
      else if (inst->op == TGSI_OPCODE_ENDLOOP)
         if (--depth == 0) {
            for (k = 0; k < this->next_temp; k++) {
               if (last_writes[k] == -2) {
                  last_writes[k] = i;
               }
            }
         }
      assert(depth >= 0);
      i++;
   }
}

/*
 * On a basic block basis, tracks available PROGRAM_TEMPORARY register
 * channels for copy propagation and updates following instructions to
 * use the original versions.
 *
 * The glsl_to_tgsi_visitor lazily produces code assuming that this pass
 * will occur.  As an example, a TXP production before this pass:
 *
 * 0: MOV TEMP[1], INPUT[4].xyyy;
 * 1: MOV TEMP[1].w, INPUT[4].wwww;
 * 2: TXP TEMP[2], TEMP[1], texture[0], 2D;
 *
 * and after:
 *
 * 0: MOV TEMP[1], INPUT[4].xyyy;
 * 1: MOV TEMP[1].w, INPUT[4].wwww;
 * 2: TXP TEMP[2], INPUT[4].xyyw, texture[0], 2D;
 *
 * which allows for dead code elimination on TEMP[1]'s writes.
 */
void
glsl_to_tgsi_visitor::copy_propagate(void)
{
   glsl_to_tgsi_instruction **acp = rzalloc_array(mem_ctx,
                                                  glsl_to_tgsi_instruction *,
                                                  this->next_temp * 4);
   int *acp_level = rzalloc_array(mem_ctx, int, this->next_temp * 4);
   int level = 0;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      assert(inst->dst[0].file != PROGRAM_TEMPORARY
             || inst->dst[0].index < this->next_temp);

      /* First, do any copy propagation possible into the src regs. */
      for (int r = 0; r < 3; r++) {
         glsl_to_tgsi_instruction *first = NULL;
         bool good = true;
         int acp_base = inst->src[r].index * 4;

         if (inst->src[r].file != PROGRAM_TEMPORARY ||
             inst->src[r].reladdr ||
             inst->src[r].reladdr2)
            continue;

         /* See if we can find entries in the ACP consisting of MOVs
          * from the same src register for all the swizzled channels
          * of this src register reference.
          */
         for (int i = 0; i < 4; i++) {
            int src_chan = GET_SWZ(inst->src[r].swizzle, i);
            glsl_to_tgsi_instruction *copy_chan = acp[acp_base + src_chan];

            if (!copy_chan) {
               good = false;
               break;
            }

            assert(acp_level[acp_base + src_chan] <= level);

            if (!first) {
               first = copy_chan;
            } else {
               if (first->src[0].file != copy_chan->src[0].file ||
                   first->src[0].index != copy_chan->src[0].index ||
                   first->src[0].double_reg2 != copy_chan->src[0].double_reg2 ||
                   first->src[0].index2D != copy_chan->src[0].index2D) {
                  good = false;
                  break;
               }
            }
         }

         if (good) {
            /* We've now validated that we can copy-propagate to
             * replace this src register reference.  Do it.
             */
            inst->src[r].file = first->src[0].file;
            inst->src[r].index = first->src[0].index;
            inst->src[r].index2D = first->src[0].index2D;
            inst->src[r].has_index2 = first->src[0].has_index2;
            inst->src[r].double_reg2 = first->src[0].double_reg2;
            inst->src[r].array_id = first->src[0].array_id;

            int swizzle = 0;
            for (int i = 0; i < 4; i++) {
               int src_chan = GET_SWZ(inst->src[r].swizzle, i);
               glsl_to_tgsi_instruction *copy_inst = acp[acp_base + src_chan];
               swizzle |= (GET_SWZ(copy_inst->src[0].swizzle, src_chan) << (3 * i));
            }
            inst->src[r].swizzle = swizzle;
         }
      }

      switch (inst->op) {
      case TGSI_OPCODE_BGNLOOP:
      case TGSI_OPCODE_ENDLOOP:
         /* End of a basic block, clear the ACP entirely. */
         memset(acp, 0, sizeof(*acp) * this->next_temp * 4);
         break;

      case TGSI_OPCODE_IF:
      case TGSI_OPCODE_UIF:
         ++level;
         break;

      case TGSI_OPCODE_ENDIF:
      case TGSI_OPCODE_ELSE:
         /* Clear all channels written inside the block from the ACP, but
          * leaving those that were not touched.
          */
         for (int r = 0; r < this->next_temp; r++) {
            for (int c = 0; c < 4; c++) {
               if (!acp[4 * r + c])
                  continue;

               if (acp_level[4 * r + c] >= level)
                  acp[4 * r + c] = NULL;
            }
         }
         if (inst->op == TGSI_OPCODE_ENDIF)
            --level;
         break;

      default:
         /* Continuing the block, clear any written channels from
          * the ACP.
          */
         for (int d = 0; d < 2; d++) {
            if (inst->dst[d].file == PROGRAM_TEMPORARY && inst->dst[d].reladdr) {
               /* Any temporary might be written, so no copy propagation
                * across this instruction.
                */
               memset(acp, 0, sizeof(*acp) * this->next_temp * 4);
            } else if (inst->dst[d].file == PROGRAM_OUTPUT &&
                       inst->dst[d].reladdr) {
               /* Any output might be written, so no copy propagation
                * from outputs across this instruction.
                */
               for (int r = 0; r < this->next_temp; r++) {
                  for (int c = 0; c < 4; c++) {
                     if (!acp[4 * r + c])
                        continue;

                     if (acp[4 * r + c]->src[0].file == PROGRAM_OUTPUT)
                        acp[4 * r + c] = NULL;
                  }
               }
            } else if (inst->dst[d].file == PROGRAM_TEMPORARY ||
                       inst->dst[d].file == PROGRAM_OUTPUT) {
               /* Clear where it's used as dst. */
               if (inst->dst[d].file == PROGRAM_TEMPORARY) {
                  for (int c = 0; c < 4; c++) {
                     if (inst->dst[d].writemask & (1 << c))
                        acp[4 * inst->dst[d].index + c] = NULL;
                  }
               }

               /* Clear where it's used as src. */
               for (int r = 0; r < this->next_temp; r++) {
                  for (int c = 0; c < 4; c++) {
                     if (!acp[4 * r + c])
                        continue;

                     int src_chan = GET_SWZ(acp[4 * r + c]->src[0].swizzle, c);

                     if (acp[4 * r + c]->src[0].file == inst->dst[d].file &&
                         acp[4 * r + c]->src[0].index == inst->dst[d].index &&
                         inst->dst[d].writemask & (1 << src_chan)) {
                        acp[4 * r + c] = NULL;
                     }
                  }
               }
            }
         }
         break;
      }

      /* If this is a copy, add it to the ACP. */
      if (inst->op == TGSI_OPCODE_MOV &&
          inst->dst[0].file == PROGRAM_TEMPORARY &&
          !(inst->dst[0].file == inst->src[0].file &&
             inst->dst[0].index == inst->src[0].index) &&
          !inst->dst[0].reladdr &&
          !inst->dst[0].reladdr2 &&
          !inst->saturate &&
          inst->src[0].file != PROGRAM_ARRAY &&
          (inst->src[0].file != PROGRAM_OUTPUT ||
           this->shader->Stage != MESA_SHADER_TESS_CTRL) &&
          !inst->src[0].reladdr &&
          !inst->src[0].reladdr2 &&
          !inst->src[0].negate &&
          !inst->src[0].abs) {
         for (int i = 0; i < 4; i++) {
            if (inst->dst[0].writemask & (1 << i)) {
               acp[4 * inst->dst[0].index + i] = inst;
               acp_level[4 * inst->dst[0].index + i] = level;
            }
         }
      }
   }

   ralloc_free(acp_level);
   ralloc_free(acp);
}

static void
dead_code_handle_reladdr(glsl_to_tgsi_instruction **writes, st_src_reg *reladdr)
{
   if (reladdr && reladdr->file == PROGRAM_TEMPORARY) {
      /* Clear where it's used as src. */
      int swz = GET_SWZ(reladdr->swizzle, 0);
      writes[4 * reladdr->index + swz] = NULL;
   }
}

/*
 * On a basic block basis, tracks available PROGRAM_TEMPORARY registers for dead
 * code elimination.
 *
 * The glsl_to_tgsi_visitor lazily produces code assuming that this pass
 * will occur.  As an example, a TXP production after copy propagation but
 * before this pass:
 *
 * 0: MOV TEMP[1], INPUT[4].xyyy;
 * 1: MOV TEMP[1].w, INPUT[4].wwww;
 * 2: TXP TEMP[2], INPUT[4].xyyw, texture[0], 2D;
 *
 * and after this pass:
 *
 * 0: TXP TEMP[2], INPUT[4].xyyw, texture[0], 2D;
 */
int
glsl_to_tgsi_visitor::eliminate_dead_code(void)
{
   glsl_to_tgsi_instruction **writes = rzalloc_array(mem_ctx,
                                                     glsl_to_tgsi_instruction *,
                                                     this->next_temp * 4);
   int *write_level = rzalloc_array(mem_ctx, int, this->next_temp * 4);
   int level = 0;
   int removed = 0;

   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      assert(inst->dst[0].file != PROGRAM_TEMPORARY
             || inst->dst[0].index < this->next_temp);

      switch (inst->op) {
      case TGSI_OPCODE_BGNLOOP:
      case TGSI_OPCODE_ENDLOOP:
      case TGSI_OPCODE_CONT:
      case TGSI_OPCODE_BRK:
         /* End of a basic block, clear the write array entirely.
          *
          * This keeps us from killing dead code when the writes are
          * on either side of a loop, even when the register isn't touched
          * inside the loop.  However, glsl_to_tgsi_visitor doesn't seem to emit
          * dead code of this type, so it shouldn't make a difference as long as
          * the dead code elimination pass in the GLSL compiler does its job.
          */
         memset(writes, 0, sizeof(*writes) * this->next_temp * 4);
         break;

      case TGSI_OPCODE_ENDIF:
      case TGSI_OPCODE_ELSE:
         /* Promote the recorded level of all channels written inside the
          * preceding if or else block to the level above the if/else block.
          */
         for (int r = 0; r < this->next_temp; r++) {
            for (int c = 0; c < 4; c++) {
               if (!writes[4 * r + c])
                  continue;

               if (write_level[4 * r + c] == level)
                  write_level[4 * r + c] = level-1;
            }
         }
         if (inst->op == TGSI_OPCODE_ENDIF)
            --level;
         break;

      case TGSI_OPCODE_IF:
      case TGSI_OPCODE_UIF:
         ++level;
         /* fallthrough to default case to mark the condition as read */
      default:
         /* Continuing the block, clear any channels from the write array that
          * are read by this instruction.
          */
         for (unsigned i = 0; i < ARRAY_SIZE(inst->src); i++) {
            if (inst->src[i].file == PROGRAM_TEMPORARY && inst->src[i].reladdr){
               /* Any temporary might be read, so no dead code elimination
                * across this instruction.
                */
               memset(writes, 0, sizeof(*writes) * this->next_temp * 4);
            } else if (inst->src[i].file == PROGRAM_TEMPORARY) {
               /* Clear where it's used as src. */
               int src_chans = 1 << GET_SWZ(inst->src[i].swizzle, 0);
               src_chans |= 1 << GET_SWZ(inst->src[i].swizzle, 1);
               src_chans |= 1 << GET_SWZ(inst->src[i].swizzle, 2);
               src_chans |= 1 << GET_SWZ(inst->src[i].swizzle, 3);

               for (int c = 0; c < 4; c++) {
                  if (src_chans & (1 << c))
                     writes[4 * inst->src[i].index + c] = NULL;
               }
            }
            dead_code_handle_reladdr(writes, inst->src[i].reladdr);
            dead_code_handle_reladdr(writes, inst->src[i].reladdr2);
         }
         for (unsigned i = 0; i < inst->tex_offset_num_offset; i++) {
            if (inst->tex_offsets[i].file == PROGRAM_TEMPORARY && inst->tex_offsets[i].reladdr){
               /* Any temporary might be read, so no dead code elimination
                * across this instruction.
                */
               memset(writes, 0, sizeof(*writes) * this->next_temp * 4);
            } else if (inst->tex_offsets[i].file == PROGRAM_TEMPORARY) {
               /* Clear where it's used as src. */
               int src_chans = 1 << GET_SWZ(inst->tex_offsets[i].swizzle, 0);
               src_chans |= 1 << GET_SWZ(inst->tex_offsets[i].swizzle, 1);
               src_chans |= 1 << GET_SWZ(inst->tex_offsets[i].swizzle, 2);
               src_chans |= 1 << GET_SWZ(inst->tex_offsets[i].swizzle, 3);

               for (int c = 0; c < 4; c++) {
                  if (src_chans & (1 << c))
                     writes[4 * inst->tex_offsets[i].index + c] = NULL;
               }
            }
            dead_code_handle_reladdr(writes, inst->tex_offsets[i].reladdr);
            dead_code_handle_reladdr(writes, inst->tex_offsets[i].reladdr2);
         }

         if (inst->resource.file == PROGRAM_TEMPORARY) {
            int src_chans;

            src_chans  = 1 << GET_SWZ(inst->resource.swizzle, 0);
            src_chans |= 1 << GET_SWZ(inst->resource.swizzle, 1);
            src_chans |= 1 << GET_SWZ(inst->resource.swizzle, 2);
            src_chans |= 1 << GET_SWZ(inst->resource.swizzle, 3);

            for (int c = 0; c < 4; c++) {
               if (src_chans & (1 << c))
                  writes[4 * inst->resource.index + c] = NULL;
            }
         }
         dead_code_handle_reladdr(writes, inst->resource.reladdr);
         dead_code_handle_reladdr(writes, inst->resource.reladdr2);

         for (unsigned i = 0; i < ARRAY_SIZE(inst->dst); i++) {
            dead_code_handle_reladdr(writes, inst->dst[i].reladdr);
            dead_code_handle_reladdr(writes, inst->dst[i].reladdr2);
         }
         break;
      }

      /* If this instruction writes to a temporary, add it to the write array.
       * If there is already an instruction in the write array for one or more
       * of the channels, flag that channel write as dead.
       */
      for (unsigned i = 0; i < ARRAY_SIZE(inst->dst); i++) {
         if (inst->dst[i].file == PROGRAM_TEMPORARY &&
             !inst->dst[i].reladdr) {
            for (int c = 0; c < 4; c++) {
               if (inst->dst[i].writemask & (1 << c)) {
                  if (writes[4 * inst->dst[i].index + c]) {
                     if (write_level[4 * inst->dst[i].index + c] < level)
                        continue;
                     else
                        writes[4 * inst->dst[i].index + c]->dead_mask |= (1 << c);
                  }
                  writes[4 * inst->dst[i].index + c] = inst;
                  write_level[4 * inst->dst[i].index + c] = level;
               }
            }
         }
      }
   }

   /* Anything still in the write array at this point is dead code. */
   for (int r = 0; r < this->next_temp; r++) {
      for (int c = 0; c < 4; c++) {
         glsl_to_tgsi_instruction *inst = writes[4 * r + c];
         if (inst)
            inst->dead_mask |= (1 << c);
      }
   }

   /* Now actually remove the instructions that are completely dead and update
    * the writemask of other instructions with dead channels.
    */
   foreach_in_list_safe(glsl_to_tgsi_instruction, inst, &this->instructions) {
      if (!inst->dead_mask || !inst->dst[0].writemask)
         continue;
      /* No amount of dead masks should remove memory stores */
      if (inst->info->is_store)
         continue;

      if ((inst->dst[0].writemask & ~inst->dead_mask) == 0) {
         inst->remove();
         delete inst;
         removed++;
      } else {
         if (glsl_base_type_is_64bit(inst->dst[0].type)) {
            if (inst->dead_mask == WRITEMASK_XY ||
                inst->dead_mask == WRITEMASK_ZW)
               inst->dst[0].writemask &= ~(inst->dead_mask);
         } else
            inst->dst[0].writemask &= ~(inst->dead_mask);
      }
   }

   ralloc_free(write_level);
   ralloc_free(writes);

   return removed;
}

/* merge DFRACEXP instructions into one. */
void
glsl_to_tgsi_visitor::merge_two_dsts(void)
{
   /* We never delete inst, but we may delete its successor. */
   foreach_in_list(glsl_to_tgsi_instruction, inst, &this->instructions) {
      glsl_to_tgsi_instruction *inst2;
      unsigned defined;

      if (num_inst_dst_regs(inst) != 2)
         continue;

      if (inst->dst[0].file != PROGRAM_UNDEFINED &&
          inst->dst[1].file != PROGRAM_UNDEFINED)
         continue;

      assert(inst->dst[0].file != PROGRAM_UNDEFINED ||
             inst->dst[1].file != PROGRAM_UNDEFINED);

      if (inst->dst[0].file == PROGRAM_UNDEFINED)
         defined = 1;
      else
         defined = 0;

      inst2 = (glsl_to_tgsi_instruction *) inst->next;
      while (!inst2->is_tail_sentinel()) {
         if (inst->op == inst2->op &&
             inst2->dst[defined].file == PROGRAM_UNDEFINED &&
             inst->src[0].file == inst2->src[0].file &&
             inst->src[0].index == inst2->src[0].index &&
             inst->src[0].type == inst2->src[0].type &&
             inst->src[0].swizzle == inst2->src[0].swizzle)
            break;
         inst2 = (glsl_to_tgsi_instruction *) inst2->next;
      }

      if (inst2->is_tail_sentinel()) {
         /* Undefined destinations are not allowed, substitute with an unused
          * temporary register.
          */
         st_src_reg tmp = get_temp(glsl_type::vec4_type);
         inst->dst[defined ^ 1] = st_dst_reg(tmp);
         inst->dst[defined ^ 1].writemask = 0;
         continue;
      }

      inst->dst[defined ^ 1] = inst2->dst[defined ^ 1];
      inst2->remove();
      delete inst2;
   }
}

/* Merges temporary registers together where possible to reduce the number of
 * registers needed to run a program.
 *
 * Produces optimal code only after copy propagation and dead code elimination
 * have been run. */
void
glsl_to_tgsi_visitor::merge_registers(void)
{
   struct lifetime *lifetimes =
         rzalloc_array(mem_ctx, struct lifetime, this->next_temp);

   if (get_temp_registers_required_lifetimes(mem_ctx, &this->instructions,
                                             this->next_temp, lifetimes)) {
      struct rename_reg_pair *renames =
            rzalloc_array(mem_ctx, struct rename_reg_pair, this->next_temp);
      get_temp_registers_remapping(mem_ctx, this->next_temp, lifetimes, renames);
      rename_temp_registers(renames);
      ralloc_free(renames);
   }

   ralloc_free(lifetimes);
}

/* Reassign indices to temporary registers by reusing unused indices created
 * by optimization passes. */
void
glsl_to_tgsi_visitor::renumber_registers(void)
{
   int i = 0;
   int new_index = 0;
   int *first_writes = ralloc_array(mem_ctx, int, this->next_temp);
   struct rename_reg_pair *renames = rzalloc_array(mem_ctx, struct rename_reg_pair, this->next_temp);

   for (i = 0; i < this->next_temp; i++) {
      first_writes[i] = -1;
   }
   get_first_temp_write(first_writes);

   for (i = 0; i < this->next_temp; i++) {
      if (first_writes[i] < 0) continue;
      if (i != new_index) {
         renames[i].new_reg = new_index;
         renames[i].valid = true;
      }
      new_index++;
   }

   rename_temp_registers(renames);
   this->next_temp = new_index;
   ralloc_free(renames);
   ralloc_free(first_writes);
}

/* ------------------------- TGSI conversion stuff -------------------------- */

/**
 * Intermediate state used during shader translation.
 */
struct st_translate {
   struct ureg_program *ureg;

   unsigned temps_size;
   struct ureg_dst *temps;

   struct ureg_dst *arrays;
   unsigned num_temp_arrays;
   struct ureg_src *constants;
   int num_constants;
   struct ureg_src *immediates;
   int num_immediates;
   struct ureg_dst outputs[PIPE_MAX_SHADER_OUTPUTS];
   struct ureg_src inputs[PIPE_MAX_SHADER_INPUTS];
   struct ureg_dst address[3];
   struct ureg_src samplers[PIPE_MAX_SAMPLERS];
   struct ureg_src buffers[PIPE_MAX_SHADER_BUFFERS];
   struct ureg_src images[PIPE_MAX_SHADER_IMAGES];
   struct ureg_src systemValues[SYSTEM_VALUE_MAX];
   struct ureg_src hw_atomics[PIPE_MAX_HW_ATOMIC_BUFFERS];
   struct ureg_src shared_memory;
   unsigned *array_sizes;
   struct inout_decl *input_decls;
   unsigned num_input_decls;
   struct inout_decl *output_decls;
   unsigned num_output_decls;

   const ubyte *inputMapping;
   const ubyte *outputMapping;

   enum pipe_shader_type procType;  /**< PIPE_SHADER_VERTEX/FRAGMENT */
   bool need_uarl;
};

/** Map Mesa's SYSTEM_VALUE_x to TGSI_SEMANTIC_x */
enum tgsi_semantic
_mesa_sysval_to_semantic(unsigned sysval)
{
   switch (sysval) {
   /* Vertex shader */
   case SYSTEM_VALUE_VERTEX_ID:
      return TGSI_SEMANTIC_VERTEXID;
   case SYSTEM_VALUE_INSTANCE_ID:
      return TGSI_SEMANTIC_INSTANCEID;
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      return TGSI_SEMANTIC_VERTEXID_NOBASE;
   case SYSTEM_VALUE_BASE_VERTEX:
      return TGSI_SEMANTIC_BASEVERTEX;
   case SYSTEM_VALUE_BASE_INSTANCE:
      return TGSI_SEMANTIC_BASEINSTANCE;
   case SYSTEM_VALUE_DRAW_ID:
      return TGSI_SEMANTIC_DRAWID;

   /* Geometry shader */
   case SYSTEM_VALUE_INVOCATION_ID:
      return TGSI_SEMANTIC_INVOCATIONID;

   /* Fragment shader */
   case SYSTEM_VALUE_FRAG_COORD:
      return TGSI_SEMANTIC_POSITION;
   case SYSTEM_VALUE_FRONT_FACE:
      return TGSI_SEMANTIC_FACE;
   case SYSTEM_VALUE_SAMPLE_ID:
      return TGSI_SEMANTIC_SAMPLEID;
   case SYSTEM_VALUE_SAMPLE_POS:
      return TGSI_SEMANTIC_SAMPLEPOS;
   case SYSTEM_VALUE_SAMPLE_MASK_IN:
      return TGSI_SEMANTIC_SAMPLEMASK;
   case SYSTEM_VALUE_HELPER_INVOCATION:
      return TGSI_SEMANTIC_HELPER_INVOCATION;

   /* Tessellation shader */
   case SYSTEM_VALUE_TESS_COORD:
      return TGSI_SEMANTIC_TESSCOORD;
   case SYSTEM_VALUE_VERTICES_IN:
      return TGSI_SEMANTIC_VERTICESIN;
   case SYSTEM_VALUE_PRIMITIVE_ID:
      return TGSI_SEMANTIC_PRIMID;
   case SYSTEM_VALUE_TESS_LEVEL_OUTER:
      return TGSI_SEMANTIC_TESSOUTER;
   case SYSTEM_VALUE_TESS_LEVEL_INNER:
      return TGSI_SEMANTIC_TESSINNER;

   /* Compute shader */
   case SYSTEM_VALUE_LOCAL_INVOCATION_ID:
      return TGSI_SEMANTIC_THREAD_ID;
   case SYSTEM_VALUE_WORK_GROUP_ID:
      return TGSI_SEMANTIC_BLOCK_ID;
   case SYSTEM_VALUE_NUM_WORK_GROUPS:
      return TGSI_SEMANTIC_GRID_SIZE;
   case SYSTEM_VALUE_LOCAL_GROUP_SIZE:
      return TGSI_SEMANTIC_BLOCK_SIZE;

   /* ARB_shader_ballot */
   case SYSTEM_VALUE_SUBGROUP_SIZE:
      return TGSI_SEMANTIC_SUBGROUP_SIZE;
   case SYSTEM_VALUE_SUBGROUP_INVOCATION:
      return TGSI_SEMANTIC_SUBGROUP_INVOCATION;
   case SYSTEM_VALUE_SUBGROUP_EQ_MASK:
      return TGSI_SEMANTIC_SUBGROUP_EQ_MASK;
   case SYSTEM_VALUE_SUBGROUP_GE_MASK:
      return TGSI_SEMANTIC_SUBGROUP_GE_MASK;
   case SYSTEM_VALUE_SUBGROUP_GT_MASK:
      return TGSI_SEMANTIC_SUBGROUP_GT_MASK;
   case SYSTEM_VALUE_SUBGROUP_LE_MASK:
      return TGSI_SEMANTIC_SUBGROUP_LE_MASK;
   case SYSTEM_VALUE_SUBGROUP_LT_MASK:
      return TGSI_SEMANTIC_SUBGROUP_LT_MASK;

   /* Unhandled */
   case SYSTEM_VALUE_LOCAL_INVOCATION_INDEX:
   case SYSTEM_VALUE_GLOBAL_INVOCATION_ID:
   case SYSTEM_VALUE_VERTEX_CNT:
   default:
      assert(!"Unexpected SYSTEM_VALUE_ enum");
      return TGSI_SEMANTIC_COUNT;
   }
}

/**
 * Map a glsl_to_tgsi constant/immediate to a TGSI immediate.
 */
static struct ureg_src
emit_immediate(struct st_translate *t,
               gl_constant_value values[4],
               GLenum type, int size)
{
   struct ureg_program *ureg = t->ureg;

   switch (type) {
   case GL_FLOAT:
      return ureg_DECL_immediate(ureg, &values[0].f, size);
   case GL_DOUBLE:
      return ureg_DECL_immediate_f64(ureg, (double *)&values[0].f, size);
   case GL_INT64_ARB:
      return ureg_DECL_immediate_int64(ureg, (int64_t *)&values[0].f, size);
   case GL_UNSIGNED_INT64_ARB:
      return ureg_DECL_immediate_uint64(ureg, (uint64_t *)&values[0].f, size);
   case GL_INT:
      return ureg_DECL_immediate_int(ureg, &values[0].i, size);
   case GL_UNSIGNED_INT:
   case GL_BOOL:
      return ureg_DECL_immediate_uint(ureg, &values[0].u, size);
   default:
      assert(!"should not get here - type must be float, int, uint, or bool");
      return ureg_src_undef();
   }
}

/**
 * Map a glsl_to_tgsi dst register to a TGSI ureg_dst register.
 */
static struct ureg_dst
dst_register(struct st_translate *t, gl_register_file file, unsigned index,
             unsigned array_id)
{
   unsigned array;

   switch (file) {
   case PROGRAM_UNDEFINED:
      return ureg_dst_undef();

   case PROGRAM_TEMPORARY:
      /* Allocate space for temporaries on demand. */
      if (index >= t->temps_size) {
         const int inc = align(index - t->temps_size + 1, 4096);

         t->temps = (struct ureg_dst*)
                    realloc(t->temps,
                            (t->temps_size + inc) * sizeof(struct ureg_dst));
         if (!t->temps)
            return ureg_dst_undef();

         memset(t->temps + t->temps_size, 0, inc * sizeof(struct ureg_dst));
         t->temps_size += inc;
      }

      if (ureg_dst_is_undef(t->temps[index]))
         t->temps[index] = ureg_DECL_local_temporary(t->ureg);

      return t->temps[index];

   case PROGRAM_ARRAY:
      assert(array_id && array_id <= t->num_temp_arrays);
      array = array_id - 1;

      if (ureg_dst_is_undef(t->arrays[array]))
         t->arrays[array] = ureg_DECL_array_temporary(
            t->ureg, t->array_sizes[array], TRUE);

      return ureg_dst_array_offset(t->arrays[array], index);

   case PROGRAM_OUTPUT:
      if (!array_id) {
         if (t->procType == PIPE_SHADER_FRAGMENT)
            assert(index < 2 * FRAG_RESULT_MAX);
         else if (t->procType == PIPE_SHADER_TESS_CTRL ||
                  t->procType == PIPE_SHADER_TESS_EVAL)
            assert(index < VARYING_SLOT_TESS_MAX);
         else
            assert(index < VARYING_SLOT_MAX);

         assert(t->outputMapping[index] < ARRAY_SIZE(t->outputs));
         assert(t->outputs[t->outputMapping[index]].File != TGSI_FILE_NULL);
         return t->outputs[t->outputMapping[index]];
      }
      else {
         struct inout_decl *decl =
            find_inout_array(t->output_decls,
                             t->num_output_decls, array_id);
         unsigned mesa_index = decl->mesa_index;
         int slot = t->outputMapping[mesa_index];

         assert(slot != -1 && t->outputs[slot].File == TGSI_FILE_OUTPUT);

         struct ureg_dst dst = t->outputs[slot];
         dst.ArrayID = array_id;
         return ureg_dst_array_offset(dst, index - mesa_index);
      }

   case PROGRAM_ADDRESS:
      return t->address[index];

   default:
      assert(!"unknown dst register file");
      return ureg_dst_undef();
   }
}

static struct ureg_src
translate_src(struct st_translate *t, const st_src_reg *src_reg);

static struct ureg_src
translate_addr(struct st_translate *t, const st_src_reg *reladdr,
               unsigned addr_index)
{
   if (t->need_uarl || !reladdr->is_legal_tgsi_address_operand())
      return ureg_src(t->address[addr_index]);

   return translate_src(t, reladdr);
}

/**
 * Create a TGSI ureg_dst register from an st_dst_reg.
 */
static struct ureg_dst
translate_dst(struct st_translate *t,
              const st_dst_reg *dst_reg,
              bool saturate)
{
   struct ureg_dst dst = dst_register(t, dst_reg->file, dst_reg->index,
                                      dst_reg->array_id);

   if (dst.File == TGSI_FILE_NULL)
      return dst;

   dst = ureg_writemask(dst, dst_reg->writemask);

   if (saturate)
      dst = ureg_saturate(dst);

   if (dst_reg->reladdr != NULL) {
      assert(dst_reg->file != PROGRAM_TEMPORARY);
      dst = ureg_dst_indirect(dst, translate_addr(t, dst_reg->reladdr, 0));
   }

   if (dst_reg->has_index2) {
      if (dst_reg->reladdr2)
         dst = ureg_dst_dimension_indirect(dst,
                                           translate_addr(t, dst_reg->reladdr2, 1),
                                           dst_reg->index2D);
      else
         dst = ureg_dst_dimension(dst, dst_reg->index2D);
   }

   return dst;
}

/**
 * Create a TGSI ureg_src register from an st_src_reg.
 */
static struct ureg_src
translate_src(struct st_translate *t, const st_src_reg *src_reg)
{
   struct ureg_src src;
   int index = src_reg->index;
   int double_reg2 = src_reg->double_reg2 ? 1 : 0;

   switch (src_reg->file) {
   case PROGRAM_UNDEFINED:
      src = ureg_imm4f(t->ureg, 0, 0, 0, 0);
      break;

   case PROGRAM_TEMPORARY:
   case PROGRAM_ARRAY:
      src = ureg_src(dst_register(t, src_reg->file, src_reg->index,
                                  src_reg->array_id));
      break;

   case PROGRAM_OUTPUT: {
      struct ureg_dst dst = dst_register(t, src_reg->file, src_reg->index,
                                         src_reg->array_id);
      assert(dst.WriteMask != 0);
      unsigned shift = ffs(dst.WriteMask) - 1;
      src = ureg_swizzle(ureg_src(dst),
                         shift,
                         MIN2(shift + 1, 3),
                         MIN2(shift + 2, 3),
                         MIN2(shift + 3, 3));
      break;
   }

   case PROGRAM_UNIFORM:
      assert(src_reg->index >= 0);
      src = src_reg->index < t->num_constants ?
               t->constants[src_reg->index] : ureg_imm4f(t->ureg, 0, 0, 0, 0);
      break;
   case PROGRAM_STATE_VAR:
   case PROGRAM_CONSTANT:       /* ie, immediate */
      if (src_reg->has_index2)
         src = ureg_src_register(TGSI_FILE_CONSTANT, src_reg->index);
      else
         src = src_reg->index >= 0 && src_reg->index < t->num_constants ?
                  t->constants[src_reg->index] : ureg_imm4f(t->ureg, 0, 0, 0, 0);
      break;

   case PROGRAM_IMMEDIATE:
      assert(src_reg->index >= 0 && src_reg->index < t->num_immediates);
      src = t->immediates[src_reg->index];
      break;

   case PROGRAM_INPUT:
      /* GLSL inputs are 64-bit containers, so we have to
       * map back to the original index and add the offset after
       * mapping. */
      index -= double_reg2;
      if (!src_reg->array_id) {
         assert(t->inputMapping[index] < ARRAY_SIZE(t->inputs));
         assert(t->inputs[t->inputMapping[index]].File != TGSI_FILE_NULL);
         src = t->inputs[t->inputMapping[index] + double_reg2];
      }
      else {
         struct inout_decl *decl = find_inout_array(t->input_decls,
                                                    t->num_input_decls,
                                                    src_reg->array_id);
         unsigned mesa_index = decl->mesa_index;
         int slot = t->inputMapping[mesa_index];

         assert(slot != -1 && t->inputs[slot].File == TGSI_FILE_INPUT);

         src = t->inputs[slot];
         src.ArrayID = src_reg->array_id;
         src = ureg_src_array_offset(src, index + double_reg2 - mesa_index);
      }
      break;

   case PROGRAM_ADDRESS:
      src = ureg_src(t->address[src_reg->index]);
      break;

   case PROGRAM_SYSTEM_VALUE:
      assert(src_reg->index < (int) ARRAY_SIZE(t->systemValues));
      src = t->systemValues[src_reg->index];
      break;

   case PROGRAM_HW_ATOMIC:
      src = ureg_src_array_register(TGSI_FILE_HW_ATOMIC, src_reg->index,
                                    src_reg->array_id);
      break;

   default:
      assert(!"unknown src register file");
      return ureg_src_undef();
   }

   if (src_reg->has_index2) {
      /* 2D indexes occur with geometry shader inputs (attrib, vertex)
       * and UBO constant buffers (buffer, position).
       */
      if (src_reg->reladdr2)
         src = ureg_src_dimension_indirect(src,
                                           translate_addr(t, src_reg->reladdr2, 1),
                                           src_reg->index2D);
      else
         src = ureg_src_dimension(src, src_reg->index2D);
   }

   src = ureg_swizzle(src,
                      GET_SWZ(src_reg->swizzle, 0) & 0x3,
                      GET_SWZ(src_reg->swizzle, 1) & 0x3,
                      GET_SWZ(src_reg->swizzle, 2) & 0x3,
                      GET_SWZ(src_reg->swizzle, 3) & 0x3);

   if (src_reg->abs)
      src = ureg_abs(src);

   if ((src_reg->negate & 0xf) == NEGATE_XYZW)
      src = ureg_negate(src);

   if (src_reg->reladdr != NULL) {
      assert(src_reg->file != PROGRAM_TEMPORARY);
      src = ureg_src_indirect(src, translate_addr(t, src_reg->reladdr, 0));
   }

   return src;
}

static struct tgsi_texture_offset
translate_tex_offset(struct st_translate *t,
                     const st_src_reg *in_offset)
{
   struct tgsi_texture_offset offset;
   struct ureg_src src = translate_src(t, in_offset);

   offset.File = src.File;
   offset.Index = src.Index;
   offset.SwizzleX = src.SwizzleX;
   offset.SwizzleY = src.SwizzleY;
   offset.SwizzleZ = src.SwizzleZ;
   offset.Padding = 0;

   assert(!src.Indirect);
   assert(!src.DimIndirect);
   assert(!src.Dimension);
   assert(!src.Absolute); /* those shouldn't be used with integers anyway */
   assert(!src.Negate);

   return offset;
}

static void
compile_tgsi_instruction(struct st_translate *t,
                         const glsl_to_tgsi_instruction *inst)
{
   struct ureg_program *ureg = t->ureg;
   int i;
   struct ureg_dst dst[2];
   struct ureg_src src[4];
   struct tgsi_texture_offset texoffsets[MAX_GLSL_TEXTURE_OFFSET];

   int num_dst;
   int num_src;
   enum tgsi_texture_type tex_target = TGSI_TEXTURE_BUFFER;

   num_dst = num_inst_dst_regs(inst);
   num_src = num_inst_src_regs(inst);

   for (i = 0; i < num_dst; i++)
      dst[i] = translate_dst(t,
                             &inst->dst[i],
                             inst->saturate);

   for (i = 0; i < num_src; i++)
      src[i] = translate_src(t, &inst->src[i]);

   switch (inst->op) {
   case TGSI_OPCODE_BGNLOOP:
   case TGSI_OPCODE_ELSE:
   case TGSI_OPCODE_ENDLOOP:
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_UIF:
      assert(num_dst == 0);
      ureg_insn(ureg, inst->op, NULL, 0, src, num_src, inst->precise);
      return;

   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TEX_LZ:
   case TGSI_OPCODE_TXB:
   case TGSI_OPCODE_TXD:
   case TGSI_OPCODE_TXL:
   case TGSI_OPCODE_TXP:
   case TGSI_OPCODE_TXQ:
   case TGSI_OPCODE_TXQS:
   case TGSI_OPCODE_TXF:
   case TGSI_OPCODE_TXF_LZ:
   case TGSI_OPCODE_TEX2:
   case TGSI_OPCODE_TXB2:
   case TGSI_OPCODE_TXL2:
   case TGSI_OPCODE_TG4:
   case TGSI_OPCODE_LODQ:
      if (inst->resource.file == PROGRAM_SAMPLER) {
         src[num_src] = t->samplers[inst->resource.index];
      } else {
         /* Bindless samplers. */
         src[num_src] = translate_src(t, &inst->resource);
      }
      assert(src[num_src].File != TGSI_FILE_NULL);
      if (inst->resource.reladdr)
         src[num_src] =
            ureg_src_indirect(src[num_src],
                              translate_addr(t, inst->resource.reladdr, 2));
      num_src++;
      for (i = 0; i < (int)inst->tex_offset_num_offset; i++) {
         texoffsets[i] = translate_tex_offset(t, &inst->tex_offsets[i]);
      }
      tex_target = st_translate_texture_target(inst->tex_target, inst->tex_shadow);

      ureg_tex_insn(ureg,
                    inst->op,
                    dst, num_dst,
                    tex_target,
                    st_translate_texture_type(inst->tex_type),
                    texoffsets, inst->tex_offset_num_offset,
                    src, num_src);
      return;

   case TGSI_OPCODE_RESQ:
   case TGSI_OPCODE_LOAD:
   case TGSI_OPCODE_ATOMUADD:
   case TGSI_OPCODE_ATOMXCHG:
   case TGSI_OPCODE_ATOMCAS:
   case TGSI_OPCODE_ATOMAND:
   case TGSI_OPCODE_ATOMOR:
   case TGSI_OPCODE_ATOMXOR:
   case TGSI_OPCODE_ATOMUMIN:
   case TGSI_OPCODE_ATOMUMAX:
   case TGSI_OPCODE_ATOMIMIN:
   case TGSI_OPCODE_ATOMIMAX:
      for (i = num_src - 1; i >= 0; i--)
         src[i + 1] = src[i];
      num_src++;
      if (inst->resource.file == PROGRAM_MEMORY) {
         src[0] = t->shared_memory;
      } else if (inst->resource.file == PROGRAM_BUFFER) {
         src[0] = t->buffers[inst->resource.index];
      } else if (inst->resource.file == PROGRAM_HW_ATOMIC) {
         src[0] = translate_src(t, &inst->resource);
      } else if (inst->resource.file == PROGRAM_CONSTANT) {
         assert(inst->resource.has_index2);
         src[0] = ureg_src_register(TGSI_FILE_CONSTBUF, inst->resource.index);
      } else {
         assert(inst->resource.file != PROGRAM_UNDEFINED);
         if (inst->resource.file == PROGRAM_IMAGE) {
            src[0] = t->images[inst->resource.index];
         } else {
            /* Bindless images. */
            src[0] = translate_src(t, &inst->resource);
         }
         tex_target = st_translate_texture_target(inst->tex_target, inst->tex_shadow);
      }
      if (inst->resource.reladdr)
         src[0] = ureg_src_indirect(src[0],
                                    translate_addr(t, inst->resource.reladdr, 2));
      assert(src[0].File != TGSI_FILE_NULL);
      ureg_memory_insn(ureg, inst->op, dst, num_dst, src, num_src,
                       inst->buffer_access,
                       tex_target, inst->image_format);
      break;

   case TGSI_OPCODE_STORE:
      if (inst->resource.file == PROGRAM_MEMORY) {
         dst[0] = ureg_dst(t->shared_memory);
      } else if (inst->resource.file == PROGRAM_BUFFER) {
         dst[0] = ureg_dst(t->buffers[inst->resource.index]);
      } else {
         if (inst->resource.file == PROGRAM_IMAGE) {
            dst[0] = ureg_dst(t->images[inst->resource.index]);
         } else {
            /* Bindless images. */
            dst[0] = ureg_dst(translate_src(t, &inst->resource));
         }
         tex_target = st_translate_texture_target(inst->tex_target, inst->tex_shadow);
      }
      dst[0] = ureg_writemask(dst[0], inst->dst[0].writemask);
      if (inst->resource.reladdr)
         dst[0] = ureg_dst_indirect(dst[0],
                                    translate_addr(t, inst->resource.reladdr, 2));
      assert(dst[0].File != TGSI_FILE_NULL);
      ureg_memory_insn(ureg, inst->op, dst, num_dst, src, num_src,
                       inst->buffer_access,
                       tex_target, inst->image_format);
      break;

   default:
      ureg_insn(ureg,
                inst->op,
                dst, num_dst,
                src, num_src, inst->precise);
      break;
   }
}

/**
 * Emit the TGSI instructions for inverting and adjusting WPOS.
 * This code is unavoidable because it also depends on whether
 * a FBO is bound (STATE_FB_WPOS_Y_TRANSFORM).
 */
static void
emit_wpos_adjustment(struct gl_context *ctx,
                     struct st_translate *t,
                     int wpos_transform_const,
                     boolean invert,
                     GLfloat adjX, GLfloat adjY[2])
{
   struct ureg_program *ureg = t->ureg;

   assert(wpos_transform_const >= 0);

   /* Fragment program uses fragment position input.
    * Need to replace instances of INPUT[WPOS] with temp T
    * where T = INPUT[WPOS] is inverted by Y.
    */
   struct ureg_src wpostrans = ureg_DECL_constant(ureg, wpos_transform_const);
   struct ureg_dst wpos_temp = ureg_DECL_temporary(ureg);
   struct ureg_src *wpos =
      ctx->Const.GLSLFragCoordIsSysVal ?
         &t->systemValues[SYSTEM_VALUE_FRAG_COORD] :
         &t->inputs[t->inputMapping[VARYING_SLOT_POS]];
   struct ureg_src wpos_input = *wpos;

   /* First, apply the coordinate shift: */
   if (adjX || adjY[0] || adjY[1]) {
      if (adjY[0] != adjY[1]) {
         /* Adjust the y coordinate by adjY[1] or adjY[0] respectively
          * depending on whether inversion is actually going to be applied
          * or not, which is determined by testing against the inversion
          * state variable used below, which will be either +1 or -1.
          */
         struct ureg_dst adj_temp = ureg_DECL_local_temporary(ureg);

         ureg_CMP(ureg, adj_temp,
                  ureg_scalar(wpostrans, invert ? 2 : 0),
                  ureg_imm4f(ureg, adjX, adjY[0], 0.0f, 0.0f),
                  ureg_imm4f(ureg, adjX, adjY[1], 0.0f, 0.0f));
         ureg_ADD(ureg, wpos_temp, wpos_input, ureg_src(adj_temp));
      } else {
         ureg_ADD(ureg, wpos_temp, wpos_input,
                  ureg_imm4f(ureg, adjX, adjY[0], 0.0f, 0.0f));
      }
      wpos_input = ureg_src(wpos_temp);
   } else {
      /* MOV wpos_temp, input[wpos]
       */
      ureg_MOV(ureg, wpos_temp, wpos_input);
   }

   /* Now the conditional y flip: STATE_FB_WPOS_Y_TRANSFORM.xy/zw will be
    * inversion/identity, or the other way around if we're drawing to an FBO.
    */
   if (invert) {
      /* MAD wpos_temp.y, wpos_input, wpostrans.xxxx, wpostrans.yyyy
       */
      ureg_MAD(ureg,
               ureg_writemask(wpos_temp, TGSI_WRITEMASK_Y),
               wpos_input,
               ureg_scalar(wpostrans, 0),
               ureg_scalar(wpostrans, 1));
   } else {
      /* MAD wpos_temp.y, wpos_input, wpostrans.zzzz, wpostrans.wwww
       */
      ureg_MAD(ureg,
               ureg_writemask(wpos_temp, TGSI_WRITEMASK_Y),
               wpos_input,
               ureg_scalar(wpostrans, 2),
               ureg_scalar(wpostrans, 3));
   }

   /* Use wpos_temp as position input from here on:
    */
   *wpos = ureg_src(wpos_temp);
}


/**
 * Emit fragment position/ooordinate code.
 */
static void
emit_wpos(struct st_context *st,
          struct st_translate *t,
          const struct gl_program *program,
          struct ureg_program *ureg,
          int wpos_transform_const)
{
   struct pipe_screen *pscreen = st->pipe->screen;
   GLfloat adjX = 0.0f;
   GLfloat adjY[2] = { 0.0f, 0.0f };
   boolean invert = FALSE;

   /* Query the pixel center conventions supported by the pipe driver and set
    * adjX, adjY to help out if it cannot handle the requested one internally.
    *
    * The bias of the y-coordinate depends on whether y-inversion takes place
    * (adjY[1]) or not (adjY[0]), which is in turn dependent on whether we are
    * drawing to an FBO (causes additional inversion), and whether the pipe
    * driver origin and the requested origin differ (the latter condition is
    * stored in the 'invert' variable).
    *
    * For height = 100 (i = integer, h = half-integer, l = lower, u = upper):
    *
    * center shift only:
    * i -> h: +0.5
    * h -> i: -0.5
    *
    * inversion only:
    * l,i -> u,i: ( 0.0 + 1.0) * -1 + 100 = 99
    * l,h -> u,h: ( 0.5 + 0.0) * -1 + 100 = 99.5
    * u,i -> l,i: (99.0 + 1.0) * -1 + 100 = 0
    * u,h -> l,h: (99.5 + 0.0) * -1 + 100 = 0.5
    *
    * inversion and center shift:
    * l,i -> u,h: ( 0.0 + 0.5) * -1 + 100 = 99.5
    * l,h -> u,i: ( 0.5 + 0.5) * -1 + 100 = 99
    * u,i -> l,h: (99.0 + 0.5) * -1 + 100 = 0.5
    * u,h -> l,i: (99.5 + 0.5) * -1 + 100 = 0
    */
   if (program->OriginUpperLeft) {
      /* Fragment shader wants origin in upper-left */
      if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT)) {
         /* the driver supports upper-left origin */
      }
      else if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT)) {
         /* the driver supports lower-left origin, need to invert Y */
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                       TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
         invert = TRUE;
      }
      else
         assert(0);
   }
   else {
      /* Fragment shader wants origin in lower-left */
      if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT))
         /* the driver supports lower-left origin */
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                       TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
      else if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT))
         /* the driver supports upper-left origin, need to invert Y */
         invert = TRUE;
      else
         assert(0);
   }

   if (program->PixelCenterInteger) {
      /* Fragment shader wants pixel center integer */
      if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER)) {
         /* the driver supports pixel center integer */
         adjY[1] = 1.0f;
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                       TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
      }
      else if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER)) {
         /* the driver supports pixel center half integer, need to bias X,Y */
         adjX = -0.5f;
         adjY[0] = -0.5f;
         adjY[1] = 0.5f;
      }
      else
         assert(0);
   }
   else {
      /* Fragment shader wants pixel center half integer */
      if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER)) {
         /* the driver supports pixel center half integer */
      }
      else if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER)) {
         /* the driver supports pixel center integer, need to bias X,Y */
         adjX = adjY[0] = adjY[1] = 0.5f;
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                       TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
      }
      else
         assert(0);
   }

   /* we invert after adjustment so that we avoid the MOV to temporary,
    * and reuse the adjustment ADD instead */
   emit_wpos_adjustment(st->ctx, t, wpos_transform_const, invert, adjX, adjY);
}

/**
 * OpenGL's fragment gl_FrontFace input is 1 for front-facing, 0 for back.
 * TGSI uses +1 for front, -1 for back.
 * This function converts the TGSI value to the GL value.  Simply clamping/
 * saturating the value to [0,1] does the job.
 */
static void
emit_face_var(struct gl_context *ctx, struct st_translate *t)
{
   struct ureg_program *ureg = t->ureg;
   struct ureg_dst face_temp = ureg_DECL_temporary(ureg);
   struct ureg_src face_input = t->inputs[t->inputMapping[VARYING_SLOT_FACE]];

   if (ctx->Const.NativeIntegers) {
      ureg_FSGE(ureg, face_temp, face_input, ureg_imm1f(ureg, 0));
   }
   else {
      /* MOV_SAT face_temp, input[face] */
      ureg_MOV(ureg, ureg_saturate(face_temp), face_input);
   }

   /* Use face_temp as face input from here on: */
   t->inputs[t->inputMapping[VARYING_SLOT_FACE]] = ureg_src(face_temp);
}

static void
emit_compute_block_size(const struct gl_program *prog,
                        struct ureg_program *ureg) {
   ureg_property(ureg, TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH,
                 prog->info.cs.local_size[0]);
   ureg_property(ureg, TGSI_PROPERTY_CS_FIXED_BLOCK_HEIGHT,
                 prog->info.cs.local_size[1]);
   ureg_property(ureg, TGSI_PROPERTY_CS_FIXED_BLOCK_DEPTH,
                 prog->info.cs.local_size[2]);
}

struct sort_inout_decls {
   bool operator()(const struct inout_decl &a, const struct inout_decl &b) const {
      return mapping[a.mesa_index] < mapping[b.mesa_index];
   }

   const ubyte *mapping;
};

/* Sort the given array of decls by the corresponding slot (TGSI file index).
 *
 * This is for the benefit of older drivers which are broken when the
 * declarations aren't sorted in this way.
 */
static void
sort_inout_decls_by_slot(struct inout_decl *decls,
                         unsigned count,
                         const ubyte mapping[])
{
   sort_inout_decls sorter;
   sorter.mapping = mapping;
   std::sort(decls, decls + count, sorter);
}

static enum tgsi_interpolate_mode
st_translate_interp(enum glsl_interp_mode glsl_qual, GLuint varying)
{
   switch (glsl_qual) {
   case INTERP_MODE_NONE:
      if (varying == VARYING_SLOT_COL0 || varying == VARYING_SLOT_COL1)
         return TGSI_INTERPOLATE_COLOR;
      return TGSI_INTERPOLATE_PERSPECTIVE;
   case INTERP_MODE_SMOOTH:
      return TGSI_INTERPOLATE_PERSPECTIVE;
   case INTERP_MODE_FLAT:
      return TGSI_INTERPOLATE_CONSTANT;
   case INTERP_MODE_NOPERSPECTIVE:
      return TGSI_INTERPOLATE_LINEAR;
   default:
      assert(0 && "unexpected interp mode in st_translate_interp()");
      return TGSI_INTERPOLATE_PERSPECTIVE;
   }
}

/**
 * Translate intermediate IR (glsl_to_tgsi_instruction) to TGSI format.
 * \param program  the program to translate
 * \param numInputs  number of input registers used
 * \param inputMapping  maps Mesa fragment program inputs to TGSI generic
 *                      input indexes
 * \param inputSemanticName  the TGSI_SEMANTIC flag for each input
 * \param inputSemanticIndex  the semantic index (ex: which texcoord) for
 *                            each input
 * \param interpMode  the TGSI_INTERPOLATE_LINEAR/PERSP mode for each input
 * \param numOutputs  number of output registers used
 * \param outputMapping  maps Mesa fragment program outputs to TGSI
 *                       generic outputs
 * \param outputSemanticName  the TGSI_SEMANTIC flag for each output
 * \param outputSemanticIndex  the semantic index (ex: which texcoord) for
 *                             each output
 *
 * \return  PIPE_OK or PIPE_ERROR_OUT_OF_MEMORY
 */
extern "C" enum pipe_error
st_translate_program(
   struct gl_context *ctx,
   enum pipe_shader_type procType,
   struct ureg_program *ureg,
   glsl_to_tgsi_visitor *program,
   const struct gl_program *proginfo,
   GLuint numInputs,
   const ubyte inputMapping[],
   const ubyte inputSlotToAttr[],
   const ubyte inputSemanticName[],
   const ubyte inputSemanticIndex[],
   const ubyte interpMode[],
   GLuint numOutputs,
   const ubyte outputMapping[],
   const ubyte outputSemanticName[],
   const ubyte outputSemanticIndex[])
{
   struct pipe_screen *screen = st_context(ctx)->pipe->screen;
   struct st_translate *t;
   unsigned i;
   struct gl_program_constants *frag_const =
      &ctx->Const.Program[MESA_SHADER_FRAGMENT];
   enum pipe_error ret = PIPE_OK;

   assert(numInputs <= ARRAY_SIZE(t->inputs));
   assert(numOutputs <= ARRAY_SIZE(t->outputs));

   ASSERT_BITFIELD_SIZE(st_src_reg, type, GLSL_TYPE_ERROR);
   ASSERT_BITFIELD_SIZE(st_dst_reg, type, GLSL_TYPE_ERROR);
   ASSERT_BITFIELD_SIZE(glsl_to_tgsi_instruction, tex_type, GLSL_TYPE_ERROR);
   ASSERT_BITFIELD_SIZE(glsl_to_tgsi_instruction, image_format, PIPE_FORMAT_COUNT);
   ASSERT_BITFIELD_SIZE(glsl_to_tgsi_instruction, tex_target,
                        (gl_texture_index) (NUM_TEXTURE_TARGETS - 1));
   ASSERT_BITFIELD_SIZE(glsl_to_tgsi_instruction, image_format,
                        (enum pipe_format) (PIPE_FORMAT_COUNT - 1));
   ASSERT_BITFIELD_SIZE(glsl_to_tgsi_instruction, op,
                        (enum tgsi_opcode) (TGSI_OPCODE_LAST - 1));

   t = CALLOC_STRUCT(st_translate);
   if (!t) {
      ret = PIPE_ERROR_OUT_OF_MEMORY;
      goto out;
   }

   t->procType = procType;
   t->need_uarl = !screen->get_param(screen, PIPE_CAP_TGSI_ANY_REG_AS_ADDRESS);
   t->inputMapping = inputMapping;
   t->outputMapping = outputMapping;
   t->ureg = ureg;
   t->num_temp_arrays = program->next_array;
   if (t->num_temp_arrays)
      t->arrays = (struct ureg_dst*)
                  calloc(t->num_temp_arrays, sizeof(t->arrays[0]));

   /*
    * Declare input attributes.
    */
   switch (procType) {
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_TESS_EVAL:
   case PIPE_SHADER_TESS_CTRL:
      sort_inout_decls_by_slot(program->inputs, program->num_inputs, inputMapping);

      for (i = 0; i < program->num_inputs; ++i) {
         struct inout_decl *decl = &program->inputs[i];
         unsigned slot = inputMapping[decl->mesa_index];
         struct ureg_src src;
         ubyte tgsi_usage_mask = decl->usage_mask;

         if (glsl_base_type_is_64bit(decl->base_type)) {
            if (tgsi_usage_mask == 1)
               tgsi_usage_mask = TGSI_WRITEMASK_XY;
            else if (tgsi_usage_mask == 2)
               tgsi_usage_mask = TGSI_WRITEMASK_ZW;
            else
               tgsi_usage_mask = TGSI_WRITEMASK_XYZW;
         }

         enum tgsi_interpolate_mode interp_mode = TGSI_INTERPOLATE_CONSTANT;
         enum tgsi_interpolate_loc interp_location = TGSI_INTERPOLATE_LOC_CENTER;
         if (procType == PIPE_SHADER_FRAGMENT) {
            assert(interpMode);
            interp_mode = interpMode[slot] != TGSI_INTERPOLATE_COUNT ?
               (enum tgsi_interpolate_mode) interpMode[slot] :
               st_translate_interp(decl->interp, inputSlotToAttr[slot]);

            interp_location = (enum tgsi_interpolate_loc) decl->interp_loc;
         }

         src = ureg_DECL_fs_input_cyl_centroid_layout(ureg,
                  (enum tgsi_semantic) inputSemanticName[slot],
                  inputSemanticIndex[slot],
                  interp_mode, 0, interp_location, slot, tgsi_usage_mask,
                  decl->array_id, decl->size);

         for (unsigned j = 0; j < decl->size; ++j) {
            if (t->inputs[slot + j].File != TGSI_FILE_INPUT) {
               /* The ArrayID is set up in dst_register */
               t->inputs[slot + j] = src;
               t->inputs[slot + j].ArrayID = 0;
               t->inputs[slot + j].Index += j;
            }
         }
      }
      break;
   case PIPE_SHADER_VERTEX:
      for (i = 0; i < numInputs; i++) {
         t->inputs[i] = ureg_DECL_vs_input(ureg, i);
      }
      break;
   case PIPE_SHADER_COMPUTE:
      break;
   default:
      assert(0);
   }

   /*
    * Declare output attributes.
    */
   switch (procType) {
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_COMPUTE:
      break;
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_TESS_EVAL:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_VERTEX:
      sort_inout_decls_by_slot(program->outputs, program->num_outputs, outputMapping);

      for (i = 0; i < program->num_outputs; ++i) {
         struct inout_decl *decl = &program->outputs[i];
         unsigned slot = outputMapping[decl->mesa_index];
         struct ureg_dst dst;
         ubyte tgsi_usage_mask = decl->usage_mask;

         if (glsl_base_type_is_64bit(decl->base_type)) {
            if (tgsi_usage_mask == 1)
               tgsi_usage_mask = TGSI_WRITEMASK_XY;
            else if (tgsi_usage_mask == 2)
               tgsi_usage_mask = TGSI_WRITEMASK_ZW;
            else
               tgsi_usage_mask = TGSI_WRITEMASK_XYZW;
         }

         dst = ureg_DECL_output_layout(ureg,
                     (enum tgsi_semantic) outputSemanticName[slot],
                     outputSemanticIndex[slot],
                     decl->gs_out_streams,
                     slot, tgsi_usage_mask, decl->array_id, decl->size);

         for (unsigned j = 0; j < decl->size; ++j) {
            if (t->outputs[slot + j].File != TGSI_FILE_OUTPUT) {
               /* The ArrayID is set up in dst_register */
               t->outputs[slot + j] = dst;
               t->outputs[slot + j].ArrayID = 0;
               t->outputs[slot + j].Index += j;
            }
         }
      }
      break;
   default:
      assert(0);
   }

   if (procType == PIPE_SHADER_FRAGMENT) {
      if (program->shader->Program->info.fs.early_fragment_tests ||
          program->shader->Program->info.fs.post_depth_coverage) {
         ureg_property(ureg, TGSI_PROPERTY_FS_EARLY_DEPTH_STENCIL, 1);

         if (program->shader->Program->info.fs.post_depth_coverage)
            ureg_property(ureg, TGSI_PROPERTY_FS_POST_DEPTH_COVERAGE, 1);
      }

      if (proginfo->info.inputs_read & VARYING_BIT_POS) {
          /* Must do this after setting up t->inputs. */
          emit_wpos(st_context(ctx), t, proginfo, ureg,
                    program->wpos_transform_const);
      }

      if (proginfo->info.inputs_read & VARYING_BIT_FACE)
         emit_face_var(ctx, t);

      for (i = 0; i < numOutputs; i++) {
         switch (outputSemanticName[i]) {
         case TGSI_SEMANTIC_POSITION:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_POSITION, /* Z/Depth */
                                             outputSemanticIndex[i]);
            t->outputs[i] = ureg_writemask(t->outputs[i], TGSI_WRITEMASK_Z);
            break;
         case TGSI_SEMANTIC_STENCIL:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_STENCIL, /* Stencil */
                                             outputSemanticIndex[i]);
            t->outputs[i] = ureg_writemask(t->outputs[i], TGSI_WRITEMASK_Y);
            break;
         case TGSI_SEMANTIC_COLOR:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_COLOR,
                                             outputSemanticIndex[i]);
            break;
         case TGSI_SEMANTIC_SAMPLEMASK:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_SAMPLEMASK,
                                             outputSemanticIndex[i]);
            /* TODO: If we ever support more than 32 samples, this will have
             * to become an array.
             */
            t->outputs[i] = ureg_writemask(t->outputs[i], TGSI_WRITEMASK_X);
            break;
         default:
            assert(!"fragment shader outputs must be POSITION/STENCIL/COLOR");
            ret = PIPE_ERROR_BAD_INPUT;
            goto out;
         }
      }
   }
   else if (procType == PIPE_SHADER_VERTEX) {
      for (i = 0; i < numOutputs; i++) {
         if (outputSemanticName[i] == TGSI_SEMANTIC_FOG) {
            /* force register to contain a fog coordinate in the form (F, 0, 0, 1). */
            ureg_MOV(ureg,
                     ureg_writemask(t->outputs[i], TGSI_WRITEMASK_YZW),
                     ureg_imm4f(ureg, 0.0f, 0.0f, 0.0f, 1.0f));
            t->outputs[i] = ureg_writemask(t->outputs[i], TGSI_WRITEMASK_X);
         }
      }
   }

   if (procType == PIPE_SHADER_COMPUTE) {
      emit_compute_block_size(proginfo, ureg);
   }

   /* Declare address register.
    */
   if (program->num_address_regs > 0) {
      assert(program->num_address_regs <= 3);
      for (int i = 0; i < program->num_address_regs; i++)
         t->address[i] = ureg_DECL_address(ureg);
   }

   /* Declare misc input registers
    */
   {
      GLbitfield64 sysInputs = proginfo->info.system_values_read;

      for (i = 0; sysInputs; i++) {
         if (sysInputs & (1ull << i)) {
            enum tgsi_semantic semName = _mesa_sysval_to_semantic(i);

            t->systemValues[i] = ureg_DECL_system_value(ureg, semName, 0);

            if (semName == TGSI_SEMANTIC_INSTANCEID ||
                semName == TGSI_SEMANTIC_VERTEXID) {
               /* From Gallium perspective, these system values are always
                * integer, and require native integer support.  However, if
                * native integer is supported on the vertex stage but not the
                * pixel stage (e.g, i915g + draw), Mesa will generate IR that
                * assumes these system values are floats. To resolve the
                * inconsistency, we insert a U2F.
                */
               struct st_context *st = st_context(ctx);
               struct pipe_screen *pscreen = st->pipe->screen;
               assert(procType == PIPE_SHADER_VERTEX);
               assert(pscreen->get_shader_param(pscreen, PIPE_SHADER_VERTEX, PIPE_SHADER_CAP_INTEGERS));
               (void) pscreen;
               if (!ctx->Const.NativeIntegers) {
                  struct ureg_dst temp = ureg_DECL_local_temporary(t->ureg);
                  ureg_U2F(t->ureg, ureg_writemask(temp, TGSI_WRITEMASK_X),
                           t->systemValues[i]);
                  t->systemValues[i] = ureg_scalar(ureg_src(temp), 0);
               }
            }

            if (procType == PIPE_SHADER_FRAGMENT &&
                semName == TGSI_SEMANTIC_POSITION)
               emit_wpos(st_context(ctx), t, proginfo, ureg,
                         program->wpos_transform_const);

            sysInputs &= ~(1ull << i);
         }
      }
   }

   t->array_sizes = program->array_sizes;
   t->input_decls = program->inputs;
   t->num_input_decls = program->num_inputs;
   t->output_decls = program->outputs;
   t->num_output_decls = program->num_outputs;

   /* Emit constants and uniforms.  TGSI uses a single index space for these,
    * so we put all the translated regs in t->constants.
    */
   if (proginfo->Parameters) {
      t->constants = (struct ureg_src *)
         calloc(proginfo->Parameters->NumParameters, sizeof(t->constants[0]));
      if (t->constants == NULL) {
         ret = PIPE_ERROR_OUT_OF_MEMORY;
         goto out;
      }
      t->num_constants = proginfo->Parameters->NumParameters;

      for (i = 0; i < proginfo->Parameters->NumParameters; i++) {
         unsigned pvo = proginfo->Parameters->ParameterValueOffset[i];

         switch (proginfo->Parameters->Parameters[i].Type) {
         case PROGRAM_STATE_VAR:
         case PROGRAM_UNIFORM:
            t->constants[i] = ureg_DECL_constant(ureg, i);
            break;

         /* Emit immediates for PROGRAM_CONSTANT only when there's no indirect
          * addressing of the const buffer.
          * FIXME: Be smarter and recognize param arrays:
          * indirect addressing is only valid within the referenced
          * array.
          */
         case PROGRAM_CONSTANT:
            if (program->indirect_addr_consts)
               t->constants[i] = ureg_DECL_constant(ureg, i);
            else
               t->constants[i] = emit_immediate(t,
                                                proginfo->Parameters->ParameterValues + pvo,
                                                proginfo->Parameters->Parameters[i].DataType,
                                                4);
            break;
         default:
            break;
         }
      }
   }

   for (i = 0; i < proginfo->info.num_ubos; i++) {
      unsigned size = proginfo->sh.UniformBlocks[i]->UniformBufferSize;
      unsigned num_const_vecs = (size + 15) / 16;
      unsigned first, last;
      assert(num_const_vecs > 0);
      first = 0;
      last = num_const_vecs > 0 ? num_const_vecs - 1 : 0;
      ureg_DECL_constant2D(t->ureg, first, last, i + 1);
   }

   /* Emit immediate values.
    */
   t->immediates = (struct ureg_src *)
      calloc(program->num_immediates, sizeof(struct ureg_src));
   if (t->immediates == NULL) {
      ret = PIPE_ERROR_OUT_OF_MEMORY;
      goto out;
   }
   t->num_immediates = program->num_immediates;

   i = 0;
   foreach_in_list(immediate_storage, imm, &program->immediates) {
      assert(i < program->num_immediates);
      t->immediates[i++] = emit_immediate(t, imm->values, imm->type, imm->size32);
   }
   assert(i == program->num_immediates);

   /* texture samplers */
   for (i = 0; i < frag_const->MaxTextureImageUnits; i++) {
      if (program->samplers_used & (1u << i)) {
         enum tgsi_return_type type =
            st_translate_texture_type(program->sampler_types[i]);

         t->samplers[i] = ureg_DECL_sampler(ureg, i);

         ureg_DECL_sampler_view(ureg, i, program->sampler_targets[i],
                                type, type, type, type);
      }
   }

   /* Declare atomic and shader storage buffers. */
   {
      struct gl_program *prog = program->prog;

      if (!st_context(ctx)->has_hw_atomics) {
         for (i = 0; i < prog->info.num_abos; i++) {
            unsigned index = prog->sh.AtomicBuffers[i]->Binding;
            assert(index < frag_const->MaxAtomicBuffers);
            t->buffers[index] = ureg_DECL_buffer(ureg, index, true);
         }
      } else {
         for (i = 0; i < program->num_atomics; i++) {
            struct hwatomic_decl *ainfo = &program->atomic_info[i];
            gl_uniform_storage *uni_storage = &prog->sh.data->UniformStorage[ainfo->location];
            int base = uni_storage->offset / ATOMIC_COUNTER_SIZE;
            ureg_DECL_hw_atomic(ureg, base, base + ainfo->size - 1, ainfo->binding,
                                ainfo->array_id);
         }
      }

      assert(prog->info.num_ssbos <= frag_const->MaxShaderStorageBlocks);
      for (i = 0; i < prog->info.num_ssbos; i++) {
         unsigned index = i;
         if (!st_context(ctx)->has_hw_atomics)
            index += frag_const->MaxAtomicBuffers;

         t->buffers[index] = ureg_DECL_buffer(ureg, index, false);
      }
   }

   if (program->use_shared_memory)
      t->shared_memory = ureg_DECL_memory(ureg, TGSI_MEMORY_TYPE_SHARED);

   for (i = 0; i < program->shader->Program->info.num_images; i++) {
      if (program->images_used & (1 << i)) {
         t->images[i] = ureg_DECL_image(ureg, i,
                                        program->image_targets[i],
                                        program->image_formats[i],
                                        true, false);
      }
   }

   /* Emit each instruction in turn:
    */
   foreach_in_list(glsl_to_tgsi_instruction, inst, &program->instructions)
      compile_tgsi_instruction(t, inst);

   /* Set the next shader stage hint for VS and TES. */
   switch (procType) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_TESS_EVAL:
      if (program->shader_program->SeparateShader)
         break;

      for (i = program->shader->Stage+1; i <= MESA_SHADER_FRAGMENT; i++) {
         if (program->shader_program->_LinkedShaders[i]) {
            ureg_set_next_shader_processor(
                  ureg, pipe_shader_type_from_mesa((gl_shader_stage)i));
            break;
         }
      }
      break;
   default:
      ; /* nothing - silence compiler warning */
   }

out:
   if (t) {
      free(t->arrays);
      free(t->temps);
      free(t->constants);
      t->num_constants = 0;
      free(t->immediates);
      t->num_immediates = 0;
      FREE(t);
   }

   return ret;
}
/* ----------------------------- End TGSI code ------------------------------ */


/**
 * Convert a shader's GLSL IR into a Mesa gl_program, although without
 * generating Mesa IR.
 */
static struct gl_program *
get_mesa_program_tgsi(struct gl_context *ctx,
                      struct gl_shader_program *shader_program,
                      struct gl_linked_shader *shader)
{
   glsl_to_tgsi_visitor* v;
   struct gl_program *prog;
   struct gl_shader_compiler_options *options =
         &ctx->Const.ShaderCompilerOptions[shader->Stage];
   struct pipe_screen *pscreen = ctx->st->pipe->screen;
   enum pipe_shader_type ptarget = pipe_shader_type_from_mesa(shader->Stage);
   unsigned skip_merge_registers;

   validate_ir_tree(shader->ir);

   prog = shader->Program;

   prog->Parameters = _mesa_new_parameter_list();
   v = new glsl_to_tgsi_visitor();
   v->ctx = ctx;
   v->prog = prog;
   v->shader_program = shader_program;
   v->shader = shader;
   v->options = options;
   v->native_integers = ctx->Const.NativeIntegers;

   v->have_sqrt = pscreen->get_shader_param(pscreen, ptarget,
                                            PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED);
   v->have_fma = pscreen->get_shader_param(pscreen, ptarget,
                                           PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED);
   v->has_tex_txf_lz = pscreen->get_param(pscreen,
                                          PIPE_CAP_TGSI_TEX_TXF_LZ);
   v->need_uarl = !pscreen->get_param(pscreen, PIPE_CAP_TGSI_ANY_REG_AS_ADDRESS);

   v->variables = _mesa_hash_table_create(v->mem_ctx, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   skip_merge_registers =
      pscreen->get_shader_param(pscreen, ptarget,
                                PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS);

   _mesa_generate_parameters_list_for_uniforms(ctx, shader_program, shader,
                                               prog->Parameters);

   /* Remove reads from output registers. */
   if (!pscreen->get_param(pscreen, PIPE_CAP_TGSI_CAN_READ_OUTPUTS))
      lower_output_reads(shader->Stage, shader->ir);

   /* Emit intermediate IR for main(). */
   visit_exec_list(shader->ir, v);

#if 0
   /* Print out some information (for debugging purposes) used by the
    * optimization passes. */
   {
      int i;
      int *first_writes = ralloc_array(v->mem_ctx, int, v->next_temp);
      int *first_reads = ralloc_array(v->mem_ctx, int, v->next_temp);
      int *last_writes = ralloc_array(v->mem_ctx, int, v->next_temp);
      int *last_reads = ralloc_array(v->mem_ctx, int, v->next_temp);

      for (i = 0; i < v->next_temp; i++) {
         first_writes[i] = -1;
         first_reads[i] = -1;
         last_writes[i] = -1;
         last_reads[i] = -1;
      }
      v->get_first_temp_read(first_reads);
      v->get_last_temp_read_first_temp_write(last_reads, first_writes);
      v->get_last_temp_write(last_writes);
      for (i = 0; i < v->next_temp; i++)
         printf("Temp %d: FR=%3d FW=%3d LR=%3d LW=%3d\n", i, first_reads[i],
                first_writes[i],
                last_reads[i],
                last_writes[i]);
      ralloc_free(first_writes);
      ralloc_free(first_reads);
      ralloc_free(last_writes);
      ralloc_free(last_reads);
   }
#endif

   /* Perform optimizations on the instructions in the glsl_to_tgsi_visitor. */
   v->simplify_cmp();
   v->copy_propagate();

   while (v->eliminate_dead_code());

   v->merge_two_dsts();
   if (!skip_merge_registers)
      v->merge_registers();
   v->renumber_registers();

   /* Write the END instruction. */
   v->emit_asm(NULL, TGSI_OPCODE_END);

   if (ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("GLSL IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(shader->Stage),
             shader_program->Name);
      _mesa_print_ir(_mesa_get_log_file(), shader->ir, NULL);
      _mesa_log("\n\n");
   }

   do_set_program_inouts(shader->ir, prog, shader->Stage);
   _mesa_copy_linked_program_data(shader_program, shader);
   shrink_array_declarations(v->inputs, v->num_inputs,
                             &prog->info.inputs_read,
                             prog->info.vs.double_inputs_read,
                             &prog->info.patch_inputs_read);
   shrink_array_declarations(v->outputs, v->num_outputs,
                             &prog->info.outputs_written, 0ULL,
                             &prog->info.patch_outputs_written);
   count_resources(v, prog);

   /* The GLSL IR won't be needed anymore. */
   ralloc_free(shader->ir);
   shader->ir = NULL;

   /* This must be done before the uniform storage is associated. */
   if (shader->Stage == MESA_SHADER_FRAGMENT &&
       (prog->info.inputs_read & VARYING_BIT_POS ||
        prog->info.system_values_read & (1ull << SYSTEM_VALUE_FRAG_COORD))) {
      static const gl_state_index16 wposTransformState[STATE_LENGTH] = {
         STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM
      };

      v->wpos_transform_const = _mesa_add_state_reference(prog->Parameters,
                                                          wposTransformState);
   }

   /* Avoid reallocation of the program parameter list, because the uniform
    * storage is only associated with the original parameter list.
    * This should be enough for Bitmap and DrawPixels constants.
    */
   _mesa_reserve_parameter_storage(prog->Parameters, 8);

   /* This has to be done last.  Any operation the can cause
    * prog->ParameterValues to get reallocated (e.g., anything that adds a
    * program constant) has to happen before creating this linkage.
    */
   _mesa_associate_uniform_storage(ctx, shader_program, prog, true);
   if (!shader_program->data->LinkStatus) {
      free_glsl_to_tgsi_visitor(v);
      _mesa_reference_program(ctx, &shader->Program, NULL);
      return NULL;
   }

   struct st_vertex_program *stvp;
   struct st_fragment_program *stfp;
   struct st_common_program *stp;
   struct st_compute_program *stcp;

   switch (shader->Stage) {
   case MESA_SHADER_VERTEX:
      stvp = (struct st_vertex_program *)prog;
      stvp->glsl_to_tgsi = v;
      break;
   case MESA_SHADER_FRAGMENT:
      stfp = (struct st_fragment_program *)prog;
      stfp->glsl_to_tgsi = v;
      break;
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      stp = st_common_program(prog);
      stp->glsl_to_tgsi = v;
      break;
   case MESA_SHADER_COMPUTE:
      stcp = (struct st_compute_program *)prog;
      stcp->glsl_to_tgsi = v;
      break;
   default:
      assert(!"should not be reached");
      return NULL;
   }

   return prog;
}

/* See if there are unsupported control flow statements. */
class ir_control_flow_info_visitor : public ir_hierarchical_visitor {
private:
   const struct gl_shader_compiler_options *options;
public:
   ir_control_flow_info_visitor(const struct gl_shader_compiler_options *options)
      : options(options),
        unsupported(false)
   {
   }

   virtual ir_visitor_status visit_enter(ir_function *ir)
   {
      /* Other functions are skipped (same as glsl_to_tgsi). */
      if (strcmp(ir->name, "main") == 0)
         return visit_continue;

      return visit_continue_with_parent;
   }

   virtual ir_visitor_status visit_enter(ir_call *ir)
   {
      if (!ir->callee->is_intrinsic()) {
         unsupported = true; /* it's a function call */
         return visit_stop;
      }
      return visit_continue;
   }

   virtual ir_visitor_status visit_enter(ir_return *ir)
   {
      if (options->EmitNoMainReturn) {
         unsupported = true;
         return visit_stop;
      }
      return visit_continue;
   }

   bool unsupported;
};

static bool
has_unsupported_control_flow(exec_list *ir,
                             const struct gl_shader_compiler_options *options)
{
   ir_control_flow_info_visitor visitor(options);
   visit_list_elements(&visitor, ir);
   return visitor.unsupported;
}

extern "C" {

/**
 * Link a shader.
 * Called via ctx->Driver.LinkShader()
 * This actually involves converting GLSL IR into an intermediate TGSI-like IR
 * with code lowering and other optimizations.
 */
GLboolean
st_link_shader(struct gl_context *ctx, struct gl_shader_program *prog)
{
   struct pipe_screen *pscreen = ctx->st->pipe->screen;

   enum pipe_shader_ir preferred_ir = (enum pipe_shader_ir)
      pscreen->get_shader_param(pscreen, PIPE_SHADER_VERTEX,
                                PIPE_SHADER_CAP_PREFERRED_IR);
   bool use_nir = preferred_ir == PIPE_SHADER_IR_NIR;

   /* Return early if we are loading the shader from on-disk cache */
   if (st_load_ir_from_disk_cache(ctx, prog, use_nir)) {
      return GL_TRUE;
   }

   assert(prog->data->LinkStatus);

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      struct gl_linked_shader *shader = prog->_LinkedShaders[i];
      exec_list *ir = shader->ir;
      gl_shader_stage stage = shader->Stage;
      const struct gl_shader_compiler_options *options =
            &ctx->Const.ShaderCompilerOptions[stage];
      enum pipe_shader_type ptarget = pipe_shader_type_from_mesa(stage);
      bool have_dround = pscreen->get_shader_param(pscreen, ptarget,
                                                   PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED);
      bool have_dfrexp = pscreen->get_shader_param(pscreen, ptarget,
                                                   PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED);
      bool have_ldexp = pscreen->get_shader_param(pscreen, ptarget,
                                                  PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED);
      unsigned if_threshold = pscreen->get_shader_param(pscreen, ptarget,
                                                        PIPE_SHADER_CAP_LOWER_IF_THRESHOLD);

      /* If there are forms of indirect addressing that the driver
       * cannot handle, perform the lowering pass.
       */
      if (options->EmitNoIndirectInput || options->EmitNoIndirectOutput ||
          options->EmitNoIndirectTemp || options->EmitNoIndirectUniform) {
         lower_variable_index_to_cond_assign(stage, ir,
                                             options->EmitNoIndirectInput,
                                             options->EmitNoIndirectOutput,
                                             options->EmitNoIndirectTemp,
                                             options->EmitNoIndirectUniform);
      }

      if (!pscreen->get_param(pscreen, PIPE_CAP_INT64_DIVMOD))
         lower_64bit_integer_instructions(ir, DIV64 | MOD64);

      if (ctx->Extensions.ARB_shading_language_packing) {
         unsigned lower_inst = LOWER_PACK_SNORM_2x16 |
                               LOWER_UNPACK_SNORM_2x16 |
                               LOWER_PACK_UNORM_2x16 |
                               LOWER_UNPACK_UNORM_2x16 |
                               LOWER_PACK_SNORM_4x8 |
                               LOWER_UNPACK_SNORM_4x8 |
                               LOWER_UNPACK_UNORM_4x8 |
                               LOWER_PACK_UNORM_4x8;

         if (ctx->Extensions.ARB_gpu_shader5)
            lower_inst |= LOWER_PACK_USE_BFI |
                          LOWER_PACK_USE_BFE;
         if (!ctx->st->has_half_float_packing)
            lower_inst |= LOWER_PACK_HALF_2x16 |
                          LOWER_UNPACK_HALF_2x16;

         lower_packing_builtins(ir, lower_inst);
      }

      if (!pscreen->get_param(pscreen, PIPE_CAP_TEXTURE_GATHER_OFFSETS))
         lower_offset_arrays(ir);
      do_mat_op_to_vec(ir);

      if (stage == MESA_SHADER_FRAGMENT)
         lower_blend_equation_advanced(
            shader, ctx->Extensions.KHR_blend_equation_advanced_coherent);

      lower_instructions(ir,
                         MOD_TO_FLOOR |
                         FDIV_TO_MUL_RCP |
                         EXP_TO_EXP2 |
                         LOG_TO_LOG2 |
                         (have_ldexp ? 0 : LDEXP_TO_ARITH) |
                         (have_dfrexp ? 0 : DFREXP_DLDEXP_TO_ARITH) |
                         CARRY_TO_ARITH |
                         BORROW_TO_ARITH |
                         (have_dround ? 0 : DOPS_TO_DFRAC) |
                         (options->EmitNoPow ? POW_TO_EXP2 : 0) |
                         (!ctx->Const.NativeIntegers ? INT_DIV_TO_MUL_RCP : 0) |
                         (options->EmitNoSat ? SAT_TO_CLAMP : 0) |
                         (ctx->Const.ForceGLSLAbsSqrt ? SQRT_TO_ABS_SQRT : 0) |
                         /* Assume that if ARB_gpu_shader5 is not supported
                          * then all of the extended integer functions need
                          * lowering.  It may be necessary to add some caps
                          * for individual instructions.
                          */
                         (!ctx->Extensions.ARB_gpu_shader5
                          ? BIT_COUNT_TO_MATH |
                            EXTRACT_TO_SHIFTS |
                            INSERT_TO_SHIFTS |
                            REVERSE_TO_SHIFTS |
                            FIND_LSB_TO_FLOAT_CAST |
                            FIND_MSB_TO_FLOAT_CAST |
                            IMUL_HIGH_TO_MUL
                          : 0));

      do_vec_index_to_cond_assign(ir);
      lower_vector_insert(ir, true);
      lower_quadop_vector(ir, false);
      lower_noise(ir);
      if (options->MaxIfDepth == 0) {
         lower_discard(ir);
      }

      if (ctx->Const.GLSLOptimizeConservatively) {
         /* Do it once and repeat only if there's unsupported control flow. */
         do {
            do_common_optimization(ir, true, true, options,
                                   ctx->Const.NativeIntegers);
            lower_if_to_cond_assign((gl_shader_stage)i, ir,
                                    options->MaxIfDepth, if_threshold);
         } while (has_unsupported_control_flow(ir, options));
      } else {
         /* Repeat it until it stops making changes. */
         bool progress;
         do {
            progress = do_common_optimization(ir, true, true, options,
                                              ctx->Const.NativeIntegers);
            progress |= lower_if_to_cond_assign((gl_shader_stage)i, ir,
                                                options->MaxIfDepth, if_threshold);
         } while (progress);
      }

      /* Do this again to lower ir_binop_vector_extract introduced
       * by optimization passes.
       */
      do_vec_index_to_cond_assign(ir);

      validate_ir_tree(ir);
   }

   build_program_resource_list(ctx, prog);

   if (use_nir)
      return st_link_nir(ctx, prog);

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *shader = prog->_LinkedShaders[i];
      if (shader == NULL)
         continue;

      struct gl_program *linked_prog =
         get_mesa_program_tgsi(ctx, prog, shader);
      st_set_prog_affected_state_flags(linked_prog);

      if (linked_prog) {
         if (!ctx->Driver.ProgramStringNotify(ctx,
                                              _mesa_shader_stage_to_program(i),
                                              linked_prog)) {
            _mesa_reference_program(ctx, &shader->Program, NULL);
            return GL_FALSE;
         }
      }
   }

   return GL_TRUE;
}

void
st_translate_stream_output_info(glsl_to_tgsi_visitor *glsl_to_tgsi,
                                const ubyte outputMapping[],
                                struct pipe_stream_output_info *so)
{
   if (!glsl_to_tgsi->shader_program->last_vert_prog)
      return;

   struct gl_transform_feedback_info *info =
      glsl_to_tgsi->shader_program->last_vert_prog->sh.LinkedTransformFeedback;
   st_translate_stream_output_info2(info, outputMapping, so);
}

void
st_translate_stream_output_info2(struct gl_transform_feedback_info *info,
                                const ubyte outputMapping[],
                                struct pipe_stream_output_info *so)
{
   unsigned i;

   for (i = 0; i < info->NumOutputs; i++) {
      so->output[i].register_index =
         outputMapping[info->Outputs[i].OutputRegister];
      so->output[i].start_component = info->Outputs[i].ComponentOffset;
      so->output[i].num_components = info->Outputs[i].NumComponents;
      so->output[i].output_buffer = info->Outputs[i].OutputBuffer;
      so->output[i].dst_offset = info->Outputs[i].DstOffset;
      so->output[i].stream = info->Outputs[i].StreamId;
   }

   for (i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
      so->stride[i] = info->Buffers[i].Stride;
   }
   so->num_outputs = info->NumOutputs;
}

} /* extern "C" */
