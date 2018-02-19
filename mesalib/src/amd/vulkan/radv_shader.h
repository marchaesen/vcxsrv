/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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
 */

#ifndef RADV_SHADER_H
#define RADV_SHADER_H

#include "radv_debug.h"
#include "radv_private.h"

#include "nir/nir.h"

struct radv_shader_module {
	struct nir_shader *nir;
	unsigned char sha1[20];
	uint32_t size;
	char data[0];
};

struct radv_shader_variant {
	uint32_t ref_count;

	struct radeon_winsys_bo *bo;
	uint64_t bo_offset;
	struct ac_shader_config config;
	uint32_t code_size;
	struct ac_shader_variant_info info;
	unsigned rsrc1;
	unsigned rsrc2;

	/* debug only */
	uint32_t *spirv;
	uint32_t spirv_size;
	struct nir_shader *nir;
	char *disasm_string;

	struct list_head slab_list;
};

struct radv_shader_slab {
	struct list_head slabs;
	struct list_head shaders;
	struct radeon_winsys_bo *bo;
	uint64_t size;
	char *ptr;
};

void
radv_optimize_nir(struct nir_shader *shader);

nir_shader *
radv_shader_compile_to_nir(struct radv_device *device,
			   struct radv_shader_module *module,
			   const char *entrypoint_name,
			   gl_shader_stage stage,
			   const VkSpecializationInfo *spec_info);

void *
radv_alloc_shader_memory(struct radv_device *device,
			  struct radv_shader_variant *shader);

void
radv_destroy_shader_slabs(struct radv_device *device);

struct radv_shader_variant *
radv_shader_variant_create(struct radv_device *device,
			   struct radv_shader_module *module,
			   struct nir_shader *const *shaders,
			   int shader_count,
			   struct radv_pipeline_layout *layout,
			   const struct ac_shader_variant_key *key,
			   void **code_out,
			   unsigned *code_size_out);

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device, struct nir_shader *nir,
			   void **code_out, unsigned *code_size_out,
			   bool multiview);

void
radv_shader_variant_destroy(struct radv_device *device,
			    struct radv_shader_variant *variant);

bool
radv_lower_indirect_derefs(struct nir_shader *nir,
                           struct radv_physical_device *device);

const char *
radv_get_shader_name(struct radv_shader_variant *var, gl_shader_stage stage);

void
radv_shader_dump_stats(struct radv_device *device,
		       struct radv_shader_variant *variant,
		       gl_shader_stage stage,
		       FILE *file);

static inline bool
radv_can_dump_shader(struct radv_device *device,
		     struct radv_shader_module *module)
{
	/* Only dump non-meta shaders, useful for debugging purposes. */
	return device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS &&
	       module && !module->nir;
}

static inline bool
radv_can_dump_shader_stats(struct radv_device *device,
			   struct radv_shader_module *module)
{
	/* Only dump non-meta shader stats. */
	return device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS &&
	       module && !module->nir;
}

#endif
