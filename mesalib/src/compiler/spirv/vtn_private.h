/*
 * Copyright © 2015 Intel Corporation
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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_array.h"
#include "nir_spirv.h"
#include "spirv.h"

struct vtn_builder;
struct vtn_decoration;

enum vtn_value_type {
   vtn_value_type_invalid = 0,
   vtn_value_type_undef,
   vtn_value_type_string,
   vtn_value_type_decoration_group,
   vtn_value_type_type,
   vtn_value_type_constant,
   vtn_value_type_access_chain,
   vtn_value_type_function,
   vtn_value_type_block,
   vtn_value_type_ssa,
   vtn_value_type_extension,
   vtn_value_type_image_pointer,
   vtn_value_type_sampled_image,
};

enum vtn_branch_type {
   vtn_branch_type_none,
   vtn_branch_type_switch_break,
   vtn_branch_type_switch_fallthrough,
   vtn_branch_type_loop_break,
   vtn_branch_type_loop_continue,
   vtn_branch_type_discard,
   vtn_branch_type_return,
};

enum vtn_cf_node_type {
   vtn_cf_node_type_block,
   vtn_cf_node_type_if,
   vtn_cf_node_type_loop,
   vtn_cf_node_type_switch,
};

struct vtn_cf_node {
   struct list_head link;
   enum vtn_cf_node_type type;
};

struct vtn_loop {
   struct vtn_cf_node node;

   /* The main body of the loop */
   struct list_head body;

   /* The "continue" part of the loop.  This gets executed after the body
    * and is where you go when you hit a continue.
    */
   struct list_head cont_body;

   SpvLoopControlMask control;
};

struct vtn_if {
   struct vtn_cf_node node;

   uint32_t condition;

   enum vtn_branch_type then_type;
   struct list_head then_body;

   enum vtn_branch_type else_type;
   struct list_head else_body;

   SpvSelectionControlMask control;
};

struct vtn_case {
   struct list_head link;

   struct list_head body;

   /* The block that starts this case */
   struct vtn_block *start_block;

   /* The fallthrough case, if any */
   struct vtn_case *fallthrough;

   /* The uint32_t values that map to this case */
   nir_array values;

   /* True if this is the default case */
   bool is_default;

   /* Initialized to false; used when sorting the list of cases */
   bool visited;
};

struct vtn_switch {
   struct vtn_cf_node node;

   uint32_t selector;

   struct list_head cases;
};

struct vtn_block {
   struct vtn_cf_node node;

   /** A pointer to the label instruction */
   const uint32_t *label;

   /** A pointer to the merge instruction (or NULL if non exists) */
   const uint32_t *merge;

   /** A pointer to the branch instruction that ends this block */
   const uint32_t *branch;

   enum vtn_branch_type branch_type;

   /** Points to the loop that this block starts (if it starts a loop) */
   struct vtn_loop *loop;

   /** Points to the switch case started by this block (if any) */
   struct vtn_case *switch_case;

   /** Every block ends in a nop intrinsic so that we can find it again */
   nir_intrinsic_instr *end_nop;
};

struct vtn_function {
   struct exec_node node;

   nir_function_impl *impl;
   struct vtn_block *start_block;

   struct list_head body;

   const uint32_t *end;

   SpvFunctionControlMask control;
};

typedef bool (*vtn_instruction_handler)(struct vtn_builder *, uint32_t,
                                        const uint32_t *, unsigned);

void vtn_build_cfg(struct vtn_builder *b, const uint32_t *words,
                   const uint32_t *end);
void vtn_function_emit(struct vtn_builder *b, struct vtn_function *func,
                       vtn_instruction_handler instruction_handler);

const uint32_t *
vtn_foreach_instruction(struct vtn_builder *b, const uint32_t *start,
                        const uint32_t *end, vtn_instruction_handler handler);

struct vtn_ssa_value {
   union {
      nir_ssa_def *def;
      struct vtn_ssa_value **elems;
   };

   /* For matrices, if this is non-NULL, then this value is actually the
    * transpose of some other value.  The value that `transposed` points to
    * always dominates this value.
    */
   struct vtn_ssa_value *transposed;

   const struct glsl_type *type;
};

struct vtn_type {
   const struct glsl_type *type;

   /* The value that declares this type.  Used for finding decorations */
   struct vtn_value *val;

   /* for matrices, whether the matrix is stored row-major */
   bool row_major;

   /* for structs, the offset of each member */
   unsigned *offsets;

   /* for structs, whether it was decorated as a "non-SSBO-like" block */
   bool block;

   /* for structs, whether it was decorated as an "SSBO-like" block */
   bool buffer_block;

   /* for structs with block == true, whether this is a builtin block (i.e. a
    * block that contains only builtins).
    */
   bool builtin_block;

   /* Image format for image_load_store type images */
   unsigned image_format;

   /* Access qualifier for storage images */
   SpvAccessQualifier access_qualifier;

   /* for arrays and matrices, the array stride */
   unsigned stride;

   /* for arrays, the vtn_type for the elements of the array */
   struct vtn_type *array_element;

   /* for structures, the vtn_type for each member */
   struct vtn_type **members;

   /* Whether this type, or a parent type, has been decorated as a builtin */
   bool is_builtin;

   SpvBuiltIn builtin;
};

struct vtn_variable;

enum vtn_access_mode {
   vtn_access_mode_id,
   vtn_access_mode_literal,
};

struct vtn_access_link {
   enum vtn_access_mode mode;
   uint32_t id;
};

struct vtn_access_chain {
   struct vtn_variable *var;

   uint32_t length;

   /* Struct elements and array offsets */
   struct vtn_access_link link[0];
};

enum vtn_variable_mode {
   vtn_variable_mode_local,
   vtn_variable_mode_global,
   vtn_variable_mode_param,
   vtn_variable_mode_ubo,
   vtn_variable_mode_ssbo,
   vtn_variable_mode_push_constant,
   vtn_variable_mode_image,
   vtn_variable_mode_sampler,
   vtn_variable_mode_workgroup,
   vtn_variable_mode_input,
   vtn_variable_mode_output,
};

struct vtn_variable {
   enum vtn_variable_mode mode;

   struct vtn_type *type;

   unsigned descriptor_set;
   unsigned binding;
   unsigned input_attachment_index;
   bool patch;

   nir_variable *var;
   nir_variable **members;

   struct vtn_access_chain chain;
};

struct vtn_image_pointer {
   struct vtn_access_chain *image;
   nir_ssa_def *coord;
   nir_ssa_def *sample;
};

struct vtn_sampled_image {
   struct vtn_access_chain *image; /* Image or array of images */
   struct vtn_access_chain *sampler; /* Sampler */
};

struct vtn_value {
   enum vtn_value_type value_type;
   const char *name;
   struct vtn_decoration *decoration;
   union {
      void *ptr;
      char *str;
      struct vtn_type *type;
      struct {
         nir_constant *constant;
         const struct glsl_type *const_type;
      };
      struct vtn_access_chain *access_chain;
      struct vtn_image_pointer *image;
      struct vtn_sampled_image *sampled_image;
      struct vtn_function *func;
      struct vtn_block *block;
      struct vtn_ssa_value *ssa;
      vtn_instruction_handler ext_handler;
   };
};

#define VTN_DEC_DECORATION -1
#define VTN_DEC_EXECUTION_MODE -2
#define VTN_DEC_STRUCT_MEMBER0 0

struct vtn_decoration {
   struct vtn_decoration *next;

   /* Specifies how to apply this decoration.  Negative values represent a
    * decoration or execution mode. (See the VTN_DEC_ #defines above.)
    * Non-negative values specify that it applies to a structure member.
    */
   int scope;

   const uint32_t *literals;
   struct vtn_value *group;

   union {
      SpvDecoration decoration;
      SpvExecutionMode exec_mode;
   };
};

struct vtn_builder {
   nir_builder nb;

   nir_shader *shader;
   nir_function_impl *impl;
   const struct nir_spirv_supported_extensions *ext;
   struct vtn_block *block;

   /* Current file, line, and column.  Useful for debugging.  Set
    * automatically by vtn_foreach_instruction.
    */
   char *file;
   int line, col;

   /*
    * In SPIR-V, constants are global, whereas in NIR, the load_const
    * instruction we use is per-function. So while we parse each function, we
    * keep a hash table of constants we've resolved to nir_ssa_value's so
    * far, and we lazily resolve them when we see them used in a function.
    */
   struct hash_table *const_table;

   /*
    * Map from phi instructions (pointer to the start of the instruction)
    * to the variable corresponding to it.
    */
   struct hash_table *phi_table;

   unsigned num_specializations;
   struct nir_spirv_specialization *specializations;

   unsigned value_id_bound;
   struct vtn_value *values;

   gl_shader_stage entry_point_stage;
   const char *entry_point_name;
   struct vtn_value *entry_point;
   bool origin_upper_left;
   bool pixel_center_integer;

   struct vtn_function *func;
   struct exec_list functions;

   /* Current function parameter index */
   unsigned func_param_idx;

   bool has_loop_continue;
};

static inline struct vtn_value *
vtn_push_value(struct vtn_builder *b, uint32_t value_id,
               enum vtn_value_type value_type)
{
   assert(value_id < b->value_id_bound);
   assert(b->values[value_id].value_type == vtn_value_type_invalid);

   b->values[value_id].value_type = value_type;

   return &b->values[value_id];
}

static inline struct vtn_value *
vtn_untyped_value(struct vtn_builder *b, uint32_t value_id)
{
   assert(value_id < b->value_id_bound);
   return &b->values[value_id];
}

static inline struct vtn_value *
vtn_value(struct vtn_builder *b, uint32_t value_id,
          enum vtn_value_type value_type)
{
   struct vtn_value *val = vtn_untyped_value(b, value_id);
   assert(val->value_type == value_type);
   return val;
}

void _vtn_warn(const char *file, int line, const char *msg, ...);
#define vtn_warn(...) _vtn_warn(__FILE__, __LINE__, __VA_ARGS__)

struct vtn_ssa_value *vtn_ssa_value(struct vtn_builder *b, uint32_t value_id);

struct vtn_ssa_value *vtn_create_ssa_value(struct vtn_builder *b,
                                           const struct glsl_type *type);

struct vtn_ssa_value *vtn_ssa_transpose(struct vtn_builder *b,
                                        struct vtn_ssa_value *src);

nir_ssa_def *vtn_vector_extract(struct vtn_builder *b, nir_ssa_def *src,
                                unsigned index);
nir_ssa_def *vtn_vector_extract_dynamic(struct vtn_builder *b, nir_ssa_def *src,
                                        nir_ssa_def *index);
nir_ssa_def *vtn_vector_insert(struct vtn_builder *b, nir_ssa_def *src,
                               nir_ssa_def *insert, unsigned index);
nir_ssa_def *vtn_vector_insert_dynamic(struct vtn_builder *b, nir_ssa_def *src,
                                       nir_ssa_def *insert, nir_ssa_def *index);

nir_deref_var *vtn_nir_deref(struct vtn_builder *b, uint32_t id);

nir_deref_var *vtn_access_chain_to_deref(struct vtn_builder *b,
                                         struct vtn_access_chain *chain);
nir_ssa_def *
vtn_access_chain_to_offset(struct vtn_builder *b,
                           struct vtn_access_chain *chain,
                           nir_ssa_def **index_out, struct vtn_type **type_out,
                           unsigned *end_idx_out, bool stop_at_matrix);

struct vtn_ssa_value *vtn_local_load(struct vtn_builder *b, nir_deref_var *src);

void vtn_local_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                     nir_deref_var *dest);

struct vtn_ssa_value *
vtn_variable_load(struct vtn_builder *b, struct vtn_access_chain *src);

void vtn_variable_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                        struct vtn_access_chain *dest);

void vtn_handle_variables(struct vtn_builder *b, SpvOp opcode,
                          const uint32_t *w, unsigned count);


typedef void (*vtn_decoration_foreach_cb)(struct vtn_builder *,
                                          struct vtn_value *,
                                          int member,
                                          const struct vtn_decoration *,
                                          void *);

void vtn_foreach_decoration(struct vtn_builder *b, struct vtn_value *value,
                            vtn_decoration_foreach_cb cb, void *data);

typedef void (*vtn_execution_mode_foreach_cb)(struct vtn_builder *,
                                              struct vtn_value *,
                                              const struct vtn_decoration *,
                                              void *);

void vtn_foreach_execution_mode(struct vtn_builder *b, struct vtn_value *value,
                                vtn_execution_mode_foreach_cb cb, void *data);

nir_op vtn_nir_alu_op_for_spirv_opcode(SpvOp opcode, bool *swap,
                                       nir_alu_type src, nir_alu_type dst);

void vtn_handle_alu(struct vtn_builder *b, SpvOp opcode,
                    const uint32_t *w, unsigned count);

bool vtn_handle_glsl450_instruction(struct vtn_builder *b, uint32_t ext_opcode,
                                    const uint32_t *words, unsigned count);

static inline uint64_t
vtn_u64_literal(const uint32_t *w)
{
   return (uint64_t)w[1] << 32 | w[0];
}
