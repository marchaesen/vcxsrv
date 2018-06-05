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

#ifndef _VTN_PRIVATE_H_
#define _VTN_PRIVATE_H_

#include <setjmp.h>

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "util/u_dynarray.h"
#include "nir_spirv.h"
#include "spirv.h"

struct vtn_builder;
struct vtn_decoration;

void vtn_log(struct vtn_builder *b, enum nir_spirv_debug_level level,
             size_t spirv_offset, const char *message);

void vtn_logf(struct vtn_builder *b, enum nir_spirv_debug_level level,
              size_t spirv_offset, const char *fmt, ...) PRINTFLIKE(4, 5);

#define vtn_info(...) vtn_logf(b, NIR_SPIRV_DEBUG_LEVEL_INFO, 0, __VA_ARGS__)

void _vtn_warn(struct vtn_builder *b, const char *file, unsigned line,
               const char *fmt, ...) PRINTFLIKE(4, 5);
#define vtn_warn(...) _vtn_warn(b, __FILE__, __LINE__, __VA_ARGS__)

void _vtn_err(struct vtn_builder *b, const char *file, unsigned line,
               const char *fmt, ...) PRINTFLIKE(4, 5);
#define vtn_err(...) _vtn_err(b, __FILE__, __LINE__, __VA_ARGS__)

/** Fail SPIR-V parsing
 *
 * This function logs an error and then bails out of the shader compile using
 * longjmp.  This being safe relies on two things:
 *
 *  1) We must guarantee that setjmp is called after allocating the builder
 *     and setting up b->debug (so that logging works) but before before any
 *     errors have a chance to occur.
 *
 *  2) While doing the SPIR-V -> NIR conversion, we need to be careful to
 *     ensure that all heap allocations happen through ralloc and are parented
 *     to the builder.  This way they will get properly cleaned up on error.
 *
 *  3) We must ensure that _vtn_fail is never called while a mutex lock or a
 *     reference to any other resource is held with the exception of ralloc
 *     objects which are parented to the builder.
 *
 * So long as these two things continue to hold, we can easily longjmp back to
 * spirv_to_nir(), clean up the builder, and return NULL.
 */
NORETURN void
_vtn_fail(struct vtn_builder *b, const char *file, unsigned line,
             const char *fmt, ...) PRINTFLIKE(4, 5);

#define vtn_fail(...) _vtn_fail(b, __FILE__, __LINE__, __VA_ARGS__)

/** Fail if the given expression evaluates to true */
#define vtn_fail_if(expr, ...) \
   do { \
      if (unlikely(expr)) \
         vtn_fail(__VA_ARGS__); \
   } while (0)

/** Assert that a condition is true and, if it isn't, vtn_fail
 *
 * This macro is transitional only and should not be used in new code.  Use
 * vtn_fail_if and provide a real message instead.
 */
#define vtn_assert(expr) \
   do { \
      if (!likely(expr)) \
         vtn_fail("%s", #expr); \
   } while (0)

enum vtn_value_type {
   vtn_value_type_invalid = 0,
   vtn_value_type_undef,
   vtn_value_type_string,
   vtn_value_type_decoration_group,
   vtn_value_type_type,
   vtn_value_type_constant,
   vtn_value_type_pointer,
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
   struct util_dynarray values;

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

   bool referenced;
   bool emitted;

   nir_function_impl *impl;
   struct vtn_block *start_block;

   struct list_head body;

   const uint32_t *end;

   SpvFunctionControlMask control;
};

typedef bool (*vtn_instruction_handler)(struct vtn_builder *, SpvOp,
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

enum vtn_base_type {
   vtn_base_type_void,
   vtn_base_type_scalar,
   vtn_base_type_vector,
   vtn_base_type_matrix,
   vtn_base_type_array,
   vtn_base_type_struct,
   vtn_base_type_pointer,
   vtn_base_type_image,
   vtn_base_type_sampler,
   vtn_base_type_sampled_image,
   vtn_base_type_function,
};

struct vtn_type {
   enum vtn_base_type base_type;

   const struct glsl_type *type;

   /* The SPIR-V id of the given type. */
   uint32_t id;

   /* Specifies the length of complex types.
    *
    * For Workgroup pointers, this is the size of the referenced type.
    */
   unsigned length;

   /* for arrays, matrices and pointers, the array stride */
   unsigned stride;

   union {
      /* Members for scalar, vector, and array-like types */
      struct {
         /* for arrays, the vtn_type for the elements of the array */
         struct vtn_type *array_element;

         /* for matrices, whether the matrix is stored row-major */
         bool row_major:1;

         /* Whether this type, or a parent type, has been decorated as a
          * builtin
          */
         bool is_builtin:1;

         /* Which built-in to use */
         SpvBuiltIn builtin;
      };

      /* Members for struct types */
      struct {
         /* for structures, the vtn_type for each member */
         struct vtn_type **members;

         /* for structs, the offset of each member */
         unsigned *offsets;

         /* for structs, whether it was decorated as a "non-SSBO-like" block */
         bool block:1;

         /* for structs, whether it was decorated as an "SSBO-like" block */
         bool buffer_block:1;

         /* for structs with block == true, whether this is a builtin block
          * (i.e. a block that contains only builtins).
          */
         bool builtin_block:1;
      };

      /* Members for pointer types */
      struct {
         /* For pointers, the vtn_type for dereferenced type */
         struct vtn_type *deref;

         /* Storage class for pointers */
         SpvStorageClass storage_class;

         /* Required alignment for pointers */
         uint32_t align;
      };

      /* Members for image types */
      struct {
         /* For images, indicates whether it's sampled or storage */
         bool sampled;

         /* Image format for image_load_store type images */
         unsigned image_format;

         /* Access qualifier for storage images */
         SpvAccessQualifier access_qualifier;
      };

      /* Members for sampled image types */
      struct {
         /* For sampled images, the image type */
         struct vtn_type *image;
      };

      /* Members for function types */
      struct {
         /* For functions, the vtn_type for each parameter */
         struct vtn_type **params;

         /* Return type for functions */
         struct vtn_type *return_type;
      };
   };
};

bool vtn_types_compatible(struct vtn_builder *b,
                          struct vtn_type *t1, struct vtn_type *t2);

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
   uint32_t length;

   /** Whether or not to treat the base pointer as an array.  This is only
    * true if this access chain came from an OpPtrAccessChain.
    */
   bool ptr_as_array;

   /** Struct elements and array offsets.
    *
    * This is an array of 1 so that it can conveniently be created on the
    * stack but the real length is given by the length field.
    */
   struct vtn_access_link link[1];
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

struct vtn_pointer {
   /** The variable mode for the referenced data */
   enum vtn_variable_mode mode;

   /** The dereferenced type of this pointer */
   struct vtn_type *type;

   /** The pointer type of this pointer
    *
    * This may be NULL for some temporary pointers constructed as part of a
    * large load, store, or copy.  It MUST be valid for all pointers which are
    * stored as SPIR-V SSA values.
    */
   struct vtn_type *ptr_type;

   /** The referenced variable, if known
    *
    * This field may be NULL if the pointer uses a (block_index, offset) pair
    * instead of an access chain.
    */
   struct vtn_variable *var;

   /** An access chain describing how to get from var to the referenced data
    *
    * This field may be NULL if the pointer references the entire variable or
    * if a (block_index, offset) pair is used instead of an access chain.
    */
   struct vtn_access_chain *chain;

   /** A (block_index, offset) pair representing a UBO or SSBO position. */
   struct nir_ssa_def *block_index;
   struct nir_ssa_def *offset;
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

   int shared_location;

   /**
    * In some early released versions of GLSLang, it implemented all function
    * calls by making copies of all parameters into temporary variables and
    * passing those variables into the function.  It even did so for samplers
    * and images which violates the SPIR-V spec.  Unfortunately, two games
    * (Talos Principle and Doom) shipped with this old version of GLSLang and
    * also happen to pass samplers into functions.  Talos Principle received
    * an update fairly shortly after release with an updated GLSLang.  Doom,
    * on the other hand, has never received an update so we need to work
    * around this GLSLang issue in SPIR-V -> NIR.  Hopefully, we can drop this
    * hack at some point in the future.
    */
   struct vtn_pointer *copy_prop_sampler;
};

struct vtn_image_pointer {
   struct vtn_pointer *image;
   nir_ssa_def *coord;
   nir_ssa_def *sample;
};

struct vtn_sampled_image {
   struct vtn_type *type;
   struct vtn_pointer *image; /* Image or array of images */
   struct vtn_pointer *sampler; /* Sampler */
};

struct vtn_value {
   enum vtn_value_type value_type;
   const char *name;
   struct vtn_decoration *decoration;
   struct vtn_type *type;
   union {
      void *ptr;
      char *str;
      nir_constant *constant;
      struct vtn_pointer *pointer;
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

   /* Used by vtn_fail to jump back to the beginning of SPIR-V compilation */
   jmp_buf fail_jump;

   const uint32_t *spirv;
   size_t spirv_word_count;

   nir_shader *shader;
   const struct spirv_to_nir_options *options;
   struct vtn_block *block;

   /* Current offset, file, line, and column.  Useful for debugging.  Set
    * automatically by vtn_foreach_instruction.
    */
   size_t spirv_offset;
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

nir_ssa_def *
vtn_pointer_to_ssa(struct vtn_builder *b, struct vtn_pointer *ptr);
struct vtn_pointer *
vtn_pointer_from_ssa(struct vtn_builder *b, nir_ssa_def *ssa,
                     struct vtn_type *ptr_type);

static inline struct vtn_value *
vtn_untyped_value(struct vtn_builder *b, uint32_t value_id)
{
   vtn_fail_if(value_id >= b->value_id_bound,
               "SPIR-V id %u is out-of-bounds", value_id);
   return &b->values[value_id];
}

static inline struct vtn_value *
vtn_push_value(struct vtn_builder *b, uint32_t value_id,
               enum vtn_value_type value_type)
{
   struct vtn_value *val = vtn_untyped_value(b, value_id);

   vtn_fail_if(val->value_type != vtn_value_type_invalid,
               "SPIR-V id %u has already been written by another instruction",
               value_id);

   val->value_type = value_type;
   return &b->values[value_id];
}

static inline struct vtn_value *
vtn_push_ssa(struct vtn_builder *b, uint32_t value_id,
             struct vtn_type *type, struct vtn_ssa_value *ssa)
{
   struct vtn_value *val;
   if (type->base_type == vtn_base_type_pointer) {
      val = vtn_push_value(b, value_id, vtn_value_type_pointer);
      val->pointer = vtn_pointer_from_ssa(b, ssa->def, type);
   } else {
      val = vtn_push_value(b, value_id, vtn_value_type_ssa);
      val->ssa = ssa;
   }
   return val;
}

static inline struct vtn_value *
vtn_value(struct vtn_builder *b, uint32_t value_id,
          enum vtn_value_type value_type)
{
   struct vtn_value *val = vtn_untyped_value(b, value_id);
   vtn_fail_if(val->value_type != value_type,
               "SPIR-V id %u is the wrong kind of value", value_id);
   return val;
}

bool
vtn_set_instruction_result_type(struct vtn_builder *b, SpvOp opcode,
                                const uint32_t *w, unsigned count);

static inline nir_constant *
vtn_constant_value(struct vtn_builder *b, uint32_t value_id)
{
   return vtn_value(b, value_id, vtn_value_type_constant)->constant;
}

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

struct vtn_pointer *vtn_pointer_for_variable(struct vtn_builder *b,
                                             struct vtn_variable *var,
                                             struct vtn_type *ptr_type);

nir_deref_var *vtn_pointer_to_deref(struct vtn_builder *b,
                                    struct vtn_pointer *ptr);
nir_ssa_def *
vtn_pointer_to_offset(struct vtn_builder *b, struct vtn_pointer *ptr,
                      nir_ssa_def **index_out, unsigned *end_idx_out);

struct vtn_ssa_value *vtn_local_load(struct vtn_builder *b, nir_deref_var *src);

void vtn_local_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                     nir_deref_var *dest);

struct vtn_ssa_value *
vtn_variable_load(struct vtn_builder *b, struct vtn_pointer *src);

void vtn_variable_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                        struct vtn_pointer *dest);

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

nir_op vtn_nir_alu_op_for_spirv_opcode(struct vtn_builder *b,
                                       SpvOp opcode, bool *swap,
                                       unsigned src_bit_size, unsigned dst_bit_size);

void vtn_handle_alu(struct vtn_builder *b, SpvOp opcode,
                    const uint32_t *w, unsigned count);

void vtn_handle_subgroup(struct vtn_builder *b, SpvOp opcode,
                         const uint32_t *w, unsigned count);

bool vtn_handle_glsl450_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                                    const uint32_t *words, unsigned count);

struct vtn_builder* vtn_create_builder(const uint32_t *words, size_t word_count,
                                       gl_shader_stage stage, const char *entry_point_name,
                                       const struct spirv_to_nir_options *options);

void vtn_handle_entry_point(struct vtn_builder *b, const uint32_t *w,
                            unsigned count);

void vtn_handle_decoration(struct vtn_builder *b, SpvOp opcode,
                           const uint32_t *w, unsigned count);

static inline uint32_t
vtn_align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -((int32_t) a)));
   return (v + a - 1) & ~(a - 1);
}

static inline uint64_t
vtn_u64_literal(const uint32_t *w)
{
   return (uint64_t)w[1] << 32 | w[0];
}

bool vtn_handle_amd_gcn_shader_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                                           const uint32_t *words, unsigned count);

bool vtn_handle_amd_shader_trinary_minmax_instruction(struct vtn_builder *b, SpvOp ext_opcode,
						      const uint32_t *words, unsigned count);
#endif /* _VTN_PRIVATE_H_ */
