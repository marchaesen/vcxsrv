/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <ctype.h>
#include "nir.h"
#include "nir_builder.h"
#include "nir_serialize.h"

/*
 * This file contains helpers for precompiling OpenCL kernels with a Mesa driver
 * and dispatching them from within the driver. It is a grab bag of utility
 * functions, rather than an all-in-one solution, to give drivers flexibility to
 * customize the compile pipeline. See asahi_clc for how the pieces fit
 * together, and see libagx for real world examples of this infrastructure.
 *
 * Why OpenCL C?
 *
 * 1. Mesa drivers are generally written in C. OpenCL C is close enough to C11
 *    that we can share driver code between host and device. This is the "killer
 *    feature" and enables implementing device-generated commands in a sane way.
 *    Both generated (e.g. GenXML) headers and entire complex driver logic may
 *    be shared for a major maintenance win.
 *
 * 2. OpenCL C has significant better ergonomics than GLSL, particularly around
 *    raw pointers. Plainly, GLSL was never designed as a systems language. What
 *    we need for implementing driver features on-device is a systems language,
 *    not a shading language.
 *
 * 3. OpenCL is the compute standard, and it is supported in Mesa via rusticl.
 *    Using OpenCL in our drivers is a way of "eating our own dog food". If Mesa
 *    based OpenCL isn't good enough for us, it's not good enough for our users
 *    either.
 *
 * 4. OpenCL C has enough affordances for GPUs that it is suitable for GPU use,
 *    unlike pure C11.
 *
 * Why precompile?
 *
 * 1. Precompiling lets us do build-time reflection on internal shaders to
 *    generate data layouts and dispatch macros automatically. The precompile
 *    pipeline implemented in this file offers significantly better ergonomics
 *    than handrolling kernels at runtime.
 *
 * 2. Compiling internal shaders at draw-time can introduce jank. Compiling
 *    internal shaders with application shaders slows down application shader
 *    compile time (and might still introduce jank in a hash-and-cache scheme).
 *    Compiling shaders at device creation time slows down initialization. The
 *    only time we can compile with no performance impact is when building the
 *    driver ahead-of-time.
 *
 * 3. Mesa is built (on developer and packager machines) far less often than it
 *    is run (on user machines). Compiling at build-time is simply more
 *    efficient in a global sense.
 *
 * 4. Compiling /all/ internal shaders with the Mesa build can turn runtime
 *    assertion fails into build failures, allowing for backend compilers to be
 *    smoke-tested without hardware testing and hence allowing regressions to be
 *    caught sooner.
 *
 * At a high level, a library of kernels is compiled to SPIR-V. That SPIR-V is
 * then translated to NIR and optimized, leaving many entrypoints. Each NIR
 * entrypoint represents one `kernel` to be precompiled.
 *
 * Kernels generally have arguments. Arguments may be either scalars or
 * pointers. It is not necessary to explicitly define a data layout for the
 * arguments. You simply declare arguments to the OpenCL side kernel:
 *
 *    KERNEL(1) void foo(int x, int y) { .. }
 *
 * The data layout is automatically derived from the function signature
 * (nir_precomp_derive_layout). The data layout is exposed to the CPU as
 * structures (nir_precomp_print_layout_struct).
 *
 *    struct foo_args {
 *       uint32_t x;
 *       uint32_t y;
 *    } PACKED;
 *
 * The data is expected to be mapped to something like Vulkan push constants in
 * the hardware. The driver defines a callback to load an argument given a byte
 * offset (e.g. via load_push_constant intrinsics). When building a variant,
 * nir_precomp_build_variant will load the arguments according to the chosen
 * layout:
 *
 *    %0 = load_push_constant 0
 *    %1 = load_push_constant 4
 *    ...
 *
 * This ensures that data layouts match between CPU and GPU, without any
 * boilerplate, while giving drivers control over exactly how arguments are
 * passed. (This can save an indirection compared to stuffing in a UBO.)
 *
 * To dispatch kernels from the driver, the kernel is "called" like a function:
 *
 *    foo(cmdbuf, grid(4, 4, 1), x, y);
 *
 * This resolves to generated dispatch macros
 * (nir_precomp_print_dispatch_macros), which lay out their arguments according
 * to the derived layout and then call the driver-specific dispatch. To
 * implement that mechanism, a driver must implement the following function
 * signature:
 *
 *    MESA_DISPATCH_PRECOMP(context, grid, barrier, kernel index,
 *                          argument pointer, size of arguments)
 *
 * The exact types used are determined by the driver. context is something like
 * a Vulkan command buffer. grid represents the 3D dispatch size. barrier
 * describes the synchronization and cache flushing required before and after
 * the dispatch. kernel index is the index of the precompiled kernel
 * (nir_precomp_index). argument pointer is a host pointer to the sized argument
 * structure, which the driver must upload and bind (e.g. as push constants).
 *
 * Because the types are ambiguous here, the same mechanism works for both
 * Gallium and Vulkan drivers.
 *
 * Although the generated header could be consumed by OpenCL code,
 * MESA_DISPATCH_PRECOMP is not intended to be implemented on the device side.
 * Instead, an analogous mechanism can be implemented for device-side enqueue
 * with automatic data layout handling. Device-side enqueue of precompiled
 * kernels has various applications, most obviously for implementing
 * device-generated commands.
 *
 * All precompiled kernels for a given target are zero-indexed and referenced in
 * an array of binaries. These indices are enum values, generated by
 * nir_precomp_print_program_enum. The array of kernels is generated by
 * nir_precomp_print_binary_map. There is generally an array for each hardware
 * target supported by a driver. On device creation, the driver would select the
 * array of binaries for the probed hardware.
 *
 * Sometimes a single binary can be used for multiple targets. In this case, the
 * driver should compile it only once and remap the binary arrays with the
 * callback passed to nir_precomp_print_binary_map.
 *
 * A single entrypoint may have multiple variants, as a small shader key. To
 * support this, kernel parameters suffixed with __n will automatically vary
 * from 0 to n - 1. This mechanism is controlled by
 * nir_precomp_parse_variant_param. For example:
 *
 *    KERNEL(1) void bar(uchar *x, int variant__4) {
 *       for (uint i = 0; i <= variant__4; ++i)
 *          x[i]++;
 *    }
 *
 * will generate 4 binaries with 1, 2, 3, and 4 additions respectively. This
 * mechanism (sigil suffixing) is kinda ugly, but I can't figure out a nicer way
 * to attach metadata to the argument in standard OpenCL.
 *
 * Internally, all variants of a given kernel have a flat index. The bijection
 * between n variant parameters and 1 flat index is given in the
 * nir_precomp_decode_variant_index comment.
 *
 * Kernels must declare their workgroup size with
 * __attribute__((reqd_work_group_size(...))) for two reasons. First, variable
 * workgroup sizes have tricky register allocation problems in several backends,
 * avoided here. Second, it makes more sense to attach the workgroup size to the
 * kernel than to the caller so this improves ergonomics of the dispatch macros.
 */

#define NIR_PRECOMP_MAX_ARGS (64)

struct nir_precomp_opts {
   /* If nonzero, minimum (power-of-two) alignment required for kernel
    * arguments. Kernel arguments will be naturally aligned regardless, but this
    * models a minimum alignment required by some hardware.
    */
   unsigned arg_align_B;
};

struct nir_precomp_layout {
   unsigned size_B;
   unsigned offset_B[NIR_PRECOMP_MAX_ARGS];
   bool prepadded[NIR_PRECOMP_MAX_ARGS];
};

static inline unsigned
nir_precomp_parse_variant_param(const nir_function *f, unsigned p)
{
   assert(p < f->num_params);

   const char *token = "__";
   const char *q = strstr(f->params[p].name, token);
   if (q == NULL)
      return 0;

   int n = atoi(q + strlen(token));

   /* Ensure the number is something reasonable */
   assert(n > 1 && n < 32 && "sanity check");
   return n;
}

static inline bool
nir_precomp_is_variant_param(const nir_function *f, unsigned p)
{
   return nir_precomp_parse_variant_param(f, p) != 0;
}

#define nir_precomp_foreach_arg(f, p)           \
   for (unsigned p = 0; p < f->num_params; ++p) \
      if (!nir_precomp_is_variant_param(f, p))

#define nir_precomp_foreach_variant_param(f, p) \
   for (unsigned p = 0; p < f->num_params; ++p) \
      if (nir_precomp_is_variant_param(f, p))

static inline unsigned
nir_precomp_nr_variants(const nir_function *f)
{
   unsigned nr = 1;

   nir_precomp_foreach_variant_param(f, p) {
      nr *= nir_precomp_parse_variant_param(f, p);
   }

   return nr;
}

static inline bool
nir_precomp_has_variants(const nir_function *f)
{
   return nir_precomp_nr_variants(f) > 1;
}

static inline struct nir_precomp_layout
nir_precomp_derive_layout(const struct nir_precomp_opts *opt,
                          const nir_function *f)
{
   struct nir_precomp_layout l = { 0 };

   nir_precomp_foreach_arg(f, a) {
      nir_parameter param = f->params[a];
      assert(a < ARRAY_SIZE(l.offset_B));

      /* Align members naturally */
      l.offset_B[a] = ALIGN_POT(l.size_B, param.bit_size / 8);

      /* Align arguments to driver minimum */
      if (opt->arg_align_B) {
         l.offset_B[a] = ALIGN_POT(l.offset_B[a], opt->arg_align_B);
      }

      l.prepadded[a] = (l.offset_B[a] != l.size_B);
      l.size_B = l.offset_B[a] + (param.num_components * param.bit_size) / 8;
   }

   return l;
}

static inline unsigned
nir_precomp_index(const nir_shader *lib, const nir_function *func)
{
   unsigned i = 0;

   nir_foreach_entrypoint(candidate, lib) {
      if (candidate == func)
         return i;

      i += nir_precomp_nr_variants(candidate);
   }

   unreachable("function must be in library");
}

static inline void
nir_print_uppercase(FILE *fp, const char *str)
{
   for (unsigned i = 0; i < strlen(str); ++i) {
      fputc(toupper(str[i]), fp);
   }
}

static inline void
nir_precomp_print_enum_value(FILE *fp, const nir_function *func)
{
   nir_print_uppercase(fp, func->name);
}

static inline void
nir_precomp_print_enum_variant_value(FILE *fp, const nir_function *func, unsigned v)
{
   nir_precomp_print_enum_value(fp, func);

   if (nir_precomp_has_variants(func)) {
      fprintf(fp, "_%u", v);
   } else {
      assert(v == 0);
   }
}

static inline void
nir_precomp_print_variant_params(FILE *fp, nir_function *func, bool with_types)
{
   if (nir_precomp_has_variants(func)) {
      fprintf(fp, "(");

      bool first = true;
      nir_precomp_foreach_variant_param(func, p) {
         fprintf(fp, "%s%s%s", first ? "" : ", ", with_types ? "unsigned " : "",
                 func->params[p].name);
         first = false;
      }

      fprintf(fp, ")");
   }
}

/*
 * Given a flattened 1D index, extract the i'th coordinate of the original N-D
 * vector. The forward map is:
 *
 *    I = sum(t=1...n) [x_t product(j=1...(t-1)) [k_j]]
 *
 * It can be shown that
 *
 *    I < product_(j=1...n)[k_j]
 *
 *    x_i = floor(I / product(j=1...(i-1)) [k_j]) mod k_i
 *
 * The inequality is by induction on n. The equivalence follows from the
 * inequality by splitting the sum of I at t=i, showing the smaller terms get
 * killed by the floor and the higher terms get killed by the modulus leaving
 * just x_i.
 *
 * The forward map is emitted in nir_precomp_print_program_enum. The inverse is
 * calculated here.
 */
static inline unsigned
nir_precomp_decode_variant_index(const nir_function *func, unsigned I,
                                 unsigned i)
{
   unsigned product = 1;

   nir_precomp_foreach_variant_param(func, j) {
      if (j >= i)
         break;

      unsigned k_j = nir_precomp_parse_variant_param(func, j);
      product *= k_j;
   }

   unsigned k_i = nir_precomp_parse_variant_param(func, i);
   return (I / product) % k_i;
}

static inline void
nir_precomp_print_program_enum(FILE *fp, const nir_shader *lib, const char *prefix)
{
   /* Generate an enum indexing all binaries */
   fprintf(fp, "enum %s_program {\n", prefix);
   nir_foreach_entrypoint(func, lib) {
      unsigned index = nir_precomp_index(lib, func);

      for (unsigned v = 0; v < nir_precomp_nr_variants(func); ++v) {
         fprintf(fp, "    ");
         nir_precomp_print_enum_variant_value(fp, func, v);
         fprintf(fp, " = %u,\n", index + v);
      }
   }
   fprintf(fp, "    ");
   nir_print_uppercase(fp, prefix);
   fprintf(fp, "_NUM_PROGRAMS,\n");
   fprintf(fp, "};\n\n");

   /* Generate indexing variants */
   nir_foreach_entrypoint(func, lib) {
      if (nir_precomp_has_variants(func)) {
         fprintf(fp, "static inline unsigned\n");
         nir_precomp_print_enum_value(fp, func);
         nir_precomp_print_variant_params(fp, func, true);
         fprintf(fp, "\n");
         fprintf(fp, "{\n");

         nir_precomp_foreach_variant_param(func, p) {
            /* Assert indices are in bounds. These provides some safety. */
            fprintf(fp, "   assert(%s < %u);\n", func->params[p].name,
                    nir_precomp_parse_variant_param(func, p));
         }

         /* Flatten an N-D index into a 1D index using the standard mapping.
          *
          * We iterate parameters backwards so we can do a single multiply-add
          * each step for simplicity (similar to Horner's method).
          */
         fprintf(fp, "\n");
         bool first = true;
         for (signed p = func->num_params - 1; p >= 0; --p) {
            if (!nir_precomp_is_variant_param(func, p))
               continue;

            if (first) {
               fprintf(fp, "   unsigned idx = %s;\n", func->params[p].name);
            } else {
               fprintf(fp, "   idx = (idx * %u) + %s;\n",
                       nir_precomp_parse_variant_param(func, p),
                       func->params[p].name);
            }

            first = false;
         }

         /* Post-condition: flattened index is in bounds. */
         fprintf(fp, "\n");
         fprintf(fp, "   assert(idx < %u);\n", nir_precomp_nr_variants(func));

         fprintf(fp, "   return ");
         nir_precomp_print_enum_variant_value(fp, func, 0);
         fprintf(fp, " + idx;\n");
         fprintf(fp, "}\n\n");
      }
   }
   fprintf(fp, "\n");
}

static inline void
nir_precomp_print_layout_struct(FILE *fp, const struct nir_precomp_opts *opt,
                                const nir_function *func)
{
   struct nir_precomp_layout layout = nir_precomp_derive_layout(opt, func);

   /* Generate a C struct matching the data layout we chose. This is how
    * the CPU will pack arguments.
    */
   unsigned offset_B = 0;

   fprintf(fp, "struct %s_args {\n", func->name);
   nir_precomp_foreach_arg(func, a) {
      nir_parameter param = func->params[a];
      assert(param.name != NULL && "kernel args must be named");

      assert(layout.offset_B[a] >= offset_B);
      unsigned pad = layout.offset_B[a] - offset_B;
      assert((pad > 0) == layout.prepadded[a]);

      if (pad > 0) {
         fprintf(fp, "   uint8_t _pad%u[%u];\n", a, pad);
         offset_B += pad;
      }

      /* After padding, the layout will match. */
      assert(layout.offset_B[a] == offset_B);

      fprintf(fp, "   uint%u_t %s", param.bit_size, param.name);
      if (param.num_components > 1) {
         fprintf(fp, "[%u]", param.num_components);
      }
      fprintf(fp, ";\n");

      offset_B += param.num_components * (param.bit_size / 8);
   }
   fprintf(fp, "} PACKED;\n\n");

   /* Assert that the layout on the CPU matches the layout on the GPU. Because
    * of the asserts above, these are mostly just sanity checking the compiler.
    * But better err on the side of defensive because alignment bugs are REALLY
    * painful to track down and we don't pay by the static assert.
    */
   nir_precomp_foreach_arg(func, a) {
      nir_parameter param = func->params[a];

      fprintf(fp, "static_assert(offsetof(struct %s_args, %s) == %u, \"\");\n",
              func->name, param.name, layout.offset_B[a]);
   }
   fprintf(fp, "static_assert(sizeof(struct %s_args) == %u, \"\");\n",
           func->name, layout.size_B);

   fprintf(fp, "\n");
}

static inline void
nir_precomp_print_dispatch_macros(FILE *fp, const struct nir_precomp_opts *opt,
                                  const nir_shader *nir)
{
   nir_foreach_entrypoint(func, nir) {
      struct nir_precomp_layout layout = nir_precomp_derive_layout(opt, func);

      for (unsigned i = 0; i < 2; ++i) {
         bool is_struct = i == 0;

         fprintf(fp, "#define %s%s(_context, _grid, _barrier%s", func->name,
                 is_struct ? "_struct" : "", is_struct ? ", _data" : "");

         /* Add the arguments, including variant parameters. For struct macros,
          * we include only the variant parameters; the kernel arguments are
          * taken from the struct.
          */
         for (unsigned p = 0; p < func->num_params; ++p) {
            if (!is_struct || nir_precomp_is_variant_param(func, p))
               fprintf(fp, ", %s", func->params[p].name);
         }

         fprintf(fp, ") do { \\\n");

         fprintf(fp, "   struct %s_args _args = ", func->name);

         if (is_struct) {
            fprintf(fp, "_data");
         } else {
            fprintf(fp, "{");

            nir_precomp_foreach_arg(func, a) {
               /* We need to zero out the padding between members. We cannot use
                * a designated initializer without prefixing the macro
                * arguments, which would add noise to the macro signature
                * reported in IDEs (which should ideally match the actual
                * signature as close as possible).
                */
               if (layout.prepadded[a]) {
                  assert(a > 0 && "first argument is never prepadded");
                  fprintf(fp, ", {0}");
               }

               fprintf(fp, "%s%s", a == 0 ? "" : ", ", func->params[a].name);
            }

            fprintf(fp, "}");
         }

         fprintf(fp, ";\\\n");

         /* Dispatch via MESA_DISPATCH_PRECOMP, which the driver must #define
          * suitably before #include-ing this file.
          */
         fprintf(fp, "   MESA_DISPATCH_PRECOMP(_context, _grid, _barrier, ");
         nir_precomp_print_enum_value(fp, func);
         nir_precomp_print_variant_params(fp, func, false);
         fprintf(fp, ", &_args, sizeof(_args)); \\\n");
         fprintf(fp, "} while(0);\n\n");
      }
   }
   fprintf(fp, "\n");
}

static inline void
nir_precomp_print_extern_binary_map(FILE *fp,
                                    const char *prefix, const char *target)
{
   fprintf(fp, "extern const uint32_t *%s_%s[", prefix, target);
   nir_print_uppercase(fp, prefix);
   fprintf(fp, "_NUM_PROGRAMS];\n");
}

static inline void
nir_precomp_print_binary_map(FILE *fp, const nir_shader *nir,
                             const char *prefix, const char *target,
                             const char *(*map)(nir_function *func,
                                                unsigned variant,
                                                const char *target))
{
   fprintf(fp, "const uint32_t *%s_%s[", prefix, target);
   nir_print_uppercase(fp, prefix);
   fprintf(fp, "_NUM_PROGRAMS] = {\n");

   nir_foreach_entrypoint(func, nir) {
      for (unsigned v = 0; v < nir_precomp_nr_variants(func); ++v) {
         fprintf(fp, "    [");
         nir_precomp_print_enum_variant_value(fp, func, v);
         fprintf(fp, "] = %s_%u_%s,\n", func->name, v,
                 map ? map(func, v, target) : target);
      }
   }

   fprintf(fp, "};\n\n");
}

static inline nir_shader *
nir_precompiled_build_variant(const nir_function *libfunc, unsigned variant,
                              const nir_shader_compiler_options *opts,
                              const struct nir_precomp_opts *precomp_opt,
                              nir_def *(*load_arg)(nir_builder *b,
                                                   unsigned num_components,
                                                   unsigned bit_size,
                                                   unsigned offset_B))
{
   bool has_variants = nir_precomp_has_variants(libfunc);
   struct nir_precomp_layout layout =
      nir_precomp_derive_layout(precomp_opt, libfunc);

   nir_builder b;
   if (has_variants) {
      b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, opts,
                                         "%s variant %u", libfunc->name,
                                         variant);
   } else {
      b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, opts, "%s",
                                         libfunc->name);
   }

   assert(libfunc->workgroup_size[0] != 0 && "must set workgroup size");

   b.shader->info.workgroup_size[0] = libfunc->workgroup_size[0];
   b.shader->info.workgroup_size[1] = libfunc->workgroup_size[1];
   b.shader->info.workgroup_size[2] = libfunc->workgroup_size[2];

   nir_function *func = nir_function_clone(b.shader, libfunc);
   func->is_entrypoint = false;

   nir_def *args[NIR_PRECOMP_MAX_ARGS] = { NULL };

   /* Some parameters are variant indices and others are kernel arguments */
   for (unsigned a = 0; a < libfunc->num_params; ++a) {
      nir_parameter p = func->params[a];

      if (nir_precomp_is_variant_param(libfunc, a)) {
         unsigned idx = nir_precomp_decode_variant_index(libfunc, variant, a);
         args[a] = nir_imm_intN_t(&b, idx, p.bit_size);
      } else {
         args[a] = load_arg(&b, p.num_components, p.bit_size, layout.offset_B[a]);
      }
   }

   nir_build_call(&b, func, func->num_params, args);
   return b.shader;
}

static inline void
nir_precomp_print_blob(FILE *fp, const char *arr_name, const char *suffix,
                       uint32_t variant, const uint32_t *data, size_t len, bool is_static)
{
   fprintf(fp, "%sconst uint32_t %s_%u_%s[%zu] = {", is_static ? "static " : "", arr_name, variant, suffix,
           DIV_ROUND_UP(len, 4));
   for (unsigned i = 0; i < (len / 4); i++) {
      if (i % 4 == 0)
         fprintf(fp, "\n   ");

      fprintf(fp, " 0x%08" PRIx32 ",", data[i]);
   }

   if (len % 4) {
      const uint8_t *data_u8 = (const uint8_t *)data;
      uint32_t last = 0;
      unsigned last_offs = ROUND_DOWN_TO(len, 4);
      for (unsigned i = 0; i < len % 4; ++i) {
         last |= (uint32_t)data_u8[last_offs + i] << (i * 8);
      }

      fprintf(fp, " 0x%08" PRIx32 ",", last);
   }

   fprintf(fp, "\n};\n");
}

static inline void
nir_precomp_print_nir(FILE *fp_c, FILE *fp_h, const nir_shader *nir,
                      const char *name, const char *suffix)
{
   struct blob blob;
   blob_init(&blob);
   nir_serialize(&blob, nir, true /* strip */);

   nir_precomp_print_blob(fp_c, name, suffix, 0, (const uint32_t *)blob.data,
                          blob.size, false);

   fprintf(fp_h, "extern const uint32_t %s_0_%s[%zu];\n", name, suffix,
           DIV_ROUND_UP(blob.size, 4));

   blob_finish(&blob);
}

static inline void
nir_precomp_print_header(FILE *fp_c, FILE *fp_h, const char *copyright,
                         const char *h_name)
{
   for (unsigned i = 0; i < 2; ++i) {
      FILE *fp = i ? fp_c : fp_h;
      fprintf(fp, "/*\n");
      fprintf(fp, " * Copyright %s\n", copyright);
      fprintf(fp, " * SPDX-License-Identifier: MIT\n");
      fprintf(fp, " *\n");
      fprintf(fp, " * Autogenerated file, do not edit\n");
      fprintf(fp, " */\n\n");

      /* uint32_t types are used throughout */
      fprintf(fp, "#include <stdint.h>\n\n");
   }

   /* The generated C code depends on the header we will generate */
   fprintf(fp_c, "#include \"%s\"\n", h_name);

   /* Include guard the header. This relies on a grown up compiler. If you're
    * doing precompiled, you have one.
    */
   fprintf(fp_h, "#pragma once\n");

   /* The generated header uses unprefixed static_assert which needs an #include
    * seemingly.
    */
   fprintf(fp_h, "#include \"util/macros.h\"\n\n");
}
