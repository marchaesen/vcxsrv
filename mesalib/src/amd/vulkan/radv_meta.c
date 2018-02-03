/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
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

#include "radv_meta.h"

#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>

void
radv_meta_save(struct radv_meta_saved_state *state,
	       struct radv_cmd_buffer *cmd_buffer, uint32_t flags)
{
	VkPipelineBindPoint bind_point =
		flags & RADV_META_SAVE_GRAPHICS_PIPELINE ?
			VK_PIPELINE_BIND_POINT_GRAPHICS :
			VK_PIPELINE_BIND_POINT_COMPUTE;
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);

	assert(flags & (RADV_META_SAVE_GRAPHICS_PIPELINE |
			RADV_META_SAVE_COMPUTE_PIPELINE));

	state->flags = flags;

	if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE) {
		assert(!(state->flags & RADV_META_SAVE_COMPUTE_PIPELINE));

		state->old_pipeline = cmd_buffer->state.pipeline;

		/* Save all viewports. */
		state->viewport.count = cmd_buffer->state.dynamic.viewport.count;
		typed_memcpy(state->viewport.viewports,
			     cmd_buffer->state.dynamic.viewport.viewports,
			     MAX_VIEWPORTS);

		/* Save all scissors. */
		state->scissor.count = cmd_buffer->state.dynamic.scissor.count;
		typed_memcpy(state->scissor.scissors,
			     cmd_buffer->state.dynamic.scissor.scissors,
			     MAX_SCISSORS);

		/* The most common meta operations all want to have the
		 * viewport reset and any scissors disabled. The rest of the
		 * dynamic state should have no effect.
		 */
		cmd_buffer->state.dynamic.viewport.count = 0;
		cmd_buffer->state.dynamic.scissor.count = 0;
		cmd_buffer->state.dirty |= 1 << VK_DYNAMIC_STATE_VIEWPORT |
					   1 << VK_DYNAMIC_STATE_SCISSOR;
	}

	if (state->flags & RADV_META_SAVE_COMPUTE_PIPELINE) {
		assert(!(state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE));

		state->old_pipeline = cmd_buffer->state.compute_pipeline;
	}

	if (state->flags & RADV_META_SAVE_DESCRIPTORS) {
		if (descriptors_state->valid & (1 << 0))
			state->old_descriptor_set0 = descriptors_state->sets[0];
		else
			state->old_descriptor_set0 = NULL;
	}

	if (state->flags & RADV_META_SAVE_CONSTANTS) {
		memcpy(state->push_constants, cmd_buffer->push_constants,
		       MAX_PUSH_CONSTANTS_SIZE);
	}

	if (state->flags & RADV_META_SAVE_PASS) {
		state->pass = cmd_buffer->state.pass;
		state->subpass = cmd_buffer->state.subpass;
		state->framebuffer = cmd_buffer->state.framebuffer;
		state->attachments = cmd_buffer->state.attachments;
		state->render_area = cmd_buffer->state.render_area;
	}
}

void
radv_meta_restore(const struct radv_meta_saved_state *state,
		  struct radv_cmd_buffer *cmd_buffer)
{
	VkPipelineBindPoint bind_point =
		state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE ?
			VK_PIPELINE_BIND_POINT_GRAPHICS :
			VK_PIPELINE_BIND_POINT_COMPUTE;

	if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_GRAPHICS,
				     radv_pipeline_to_handle(state->old_pipeline));

		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_PIPELINE;

		/* Restore all viewports. */
		cmd_buffer->state.dynamic.viewport.count = state->viewport.count;
		typed_memcpy(cmd_buffer->state.dynamic.viewport.viewports,
			     state->viewport.viewports,
			     MAX_VIEWPORTS);

		/* Restore all scissors. */
		cmd_buffer->state.dynamic.scissor.count = state->scissor.count;
		typed_memcpy(cmd_buffer->state.dynamic.scissor.scissors,
			     state->scissor.scissors,
			     MAX_SCISSORS);

		cmd_buffer->state.dirty |= 1 << VK_DYNAMIC_STATE_VIEWPORT |
					   1 << VK_DYNAMIC_STATE_SCISSOR;
	}

	if (state->flags & RADV_META_SAVE_COMPUTE_PIPELINE) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_COMPUTE,
				     radv_pipeline_to_handle(state->old_pipeline));
	}

	if (state->flags & RADV_META_SAVE_DESCRIPTORS) {
		radv_set_descriptor_set(cmd_buffer, bind_point,
					state->old_descriptor_set0, 0);
	}

	if (state->flags & RADV_META_SAVE_CONSTANTS) {
		memcpy(cmd_buffer->push_constants, state->push_constants,
		       MAX_PUSH_CONSTANTS_SIZE);
		cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_COMPUTE_BIT;

		if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE) {
			cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_ALL_GRAPHICS;
		}
	}

	if (state->flags & RADV_META_SAVE_PASS) {
		cmd_buffer->state.pass = state->pass;
		cmd_buffer->state.subpass = state->subpass;
		cmd_buffer->state.framebuffer = state->framebuffer;
		cmd_buffer->state.attachments = state->attachments;
		cmd_buffer->state.render_area = state->render_area;
		if (state->subpass)
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_FRAMEBUFFER;
	}
}

VkImageViewType
radv_meta_get_view_type(const struct radv_image *image)
{
	switch (image->type) {
	case VK_IMAGE_TYPE_1D: return VK_IMAGE_VIEW_TYPE_1D;
	case VK_IMAGE_TYPE_2D: return VK_IMAGE_VIEW_TYPE_2D;
	case VK_IMAGE_TYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
	default:
		unreachable("bad VkImageViewType");
	}
}

/**
 * When creating a destination VkImageView, this function provides the needed
 * VkImageViewCreateInfo::subresourceRange::baseArrayLayer.
 */
uint32_t
radv_meta_get_iview_layer(const struct radv_image *dest_image,
			  const VkImageSubresourceLayers *dest_subresource,
			  const VkOffset3D *dest_offset)
{
	switch (dest_image->type) {
	case VK_IMAGE_TYPE_1D:
	case VK_IMAGE_TYPE_2D:
		return dest_subresource->baseArrayLayer;
	case VK_IMAGE_TYPE_3D:
		/* HACK: Vulkan does not allow attaching a 3D image to a framebuffer,
		 * but meta does it anyway. When doing so, we translate the
		 * destination's z offset into an array offset.
		 */
		return dest_offset->z;
	default:
		assert(!"bad VkImageType");
		return 0;
	}
}

static void *
meta_alloc(void* _device, size_t size, size_t alignment,
           VkSystemAllocationScope allocationScope)
{
	struct radv_device *device = _device;
	return device->alloc.pfnAllocation(device->alloc.pUserData, size, alignment,
					   VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static void *
meta_realloc(void* _device, void *original, size_t size, size_t alignment,
             VkSystemAllocationScope allocationScope)
{
	struct radv_device *device = _device;
	return device->alloc.pfnReallocation(device->alloc.pUserData, original,
					     size, alignment,
					     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static void
meta_free(void* _device, void *data)
{
	struct radv_device *device = _device;
	return device->alloc.pfnFree(device->alloc.pUserData, data);
}

static bool
radv_builtin_cache_path(char *path)
{
	char *xdg_cache_home = getenv("XDG_CACHE_HOME");
	const char *suffix = "/radv_builtin_shaders";
	const char *suffix2 = "/.cache/radv_builtin_shaders";
	struct passwd pwd, *result;
	char path2[PATH_MAX + 1]; /* PATH_MAX is not a real max,but suffices here. */

	if (xdg_cache_home) {

		if (strlen(xdg_cache_home) + strlen(suffix) > PATH_MAX)
			return false;

		strcpy(path, xdg_cache_home);
		strcat(path, suffix);
		return true;
	}

	getpwuid_r(getuid(), &pwd, path2, PATH_MAX - strlen(suffix2), &result);
	if (!result)
		return false;

	strcpy(path, pwd.pw_dir);
	strcat(path, "/.cache");
	mkdir(path, 0755);

	strcat(path, suffix);
	return true;
}

static void
radv_load_meta_pipeline(struct radv_device *device)
{
	char path[PATH_MAX + 1];
	struct stat st;
	void *data = NULL;

	if (!radv_builtin_cache_path(path))
		return;

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	if (fstat(fd, &st))
		goto fail;
	data = malloc(st.st_size);
	if (!data)
		goto fail;
	if(read(fd, data, st.st_size) == -1)
		goto fail;

	radv_pipeline_cache_load(&device->meta_state.cache, data, st.st_size);
fail:
	free(data);
	close(fd);
}

static void
radv_store_meta_pipeline(struct radv_device *device)
{
	char path[PATH_MAX + 1], path2[PATH_MAX + 7];
	size_t size;
	void *data = NULL;

	if (!device->meta_state.cache.modified)
		return;

	if (radv_GetPipelineCacheData(radv_device_to_handle(device),
				      radv_pipeline_cache_to_handle(&device->meta_state.cache),
				      &size, NULL))
		return;

	if (!radv_builtin_cache_path(path))
		return;

	strcpy(path2, path);
	strcat(path2, "XXXXXX");
	int fd = mkstemp(path2);//open(path, O_WRONLY | O_CREAT, 0600);
	if (fd < 0)
		return;
	data = malloc(size);
	if (!data)
		goto fail;

	if (radv_GetPipelineCacheData(radv_device_to_handle(device),
				      radv_pipeline_cache_to_handle(&device->meta_state.cache),
				      &size, data))
		goto fail;
	if(write(fd, data, size) == -1)
		goto fail;

	rename(path2, path);
fail:
	free(data);
	close(fd);
	unlink(path2);
}

VkResult
radv_device_init_meta(struct radv_device *device)
{
	VkResult result;

	device->meta_state.alloc = (VkAllocationCallbacks) {
		.pUserData = device,
		.pfnAllocation = meta_alloc,
		.pfnReallocation = meta_realloc,
		.pfnFree = meta_free,
	};

	device->meta_state.cache.alloc = device->meta_state.alloc;
	radv_pipeline_cache_init(&device->meta_state.cache, device);
	radv_load_meta_pipeline(device);

	result = radv_device_init_meta_clear_state(device);
	if (result != VK_SUCCESS)
		goto fail_clear;

	result = radv_device_init_meta_resolve_state(device);
	if (result != VK_SUCCESS)
		goto fail_resolve;

	result = radv_device_init_meta_blit_state(device);
	if (result != VK_SUCCESS)
		goto fail_blit;

	result = radv_device_init_meta_blit2d_state(device);
	if (result != VK_SUCCESS)
		goto fail_blit2d;

	result = radv_device_init_meta_bufimage_state(device);
	if (result != VK_SUCCESS)
		goto fail_bufimage;

	result = radv_device_init_meta_depth_decomp_state(device);
	if (result != VK_SUCCESS)
		goto fail_depth_decomp;

	result = radv_device_init_meta_buffer_state(device);
	if (result != VK_SUCCESS)
		goto fail_buffer;

	result = radv_device_init_meta_query_state(device);
	if (result != VK_SUCCESS)
		goto fail_query;

	result = radv_device_init_meta_fast_clear_flush_state(device);
	if (result != VK_SUCCESS)
		goto fail_fast_clear;

	result = radv_device_init_meta_resolve_compute_state(device);
	if (result != VK_SUCCESS)
		goto fail_resolve_compute;

	result = radv_device_init_meta_resolve_fragment_state(device);
	if (result != VK_SUCCESS)
		goto fail_resolve_fragment;
	return VK_SUCCESS;

fail_resolve_fragment:
	radv_device_finish_meta_resolve_compute_state(device);
fail_resolve_compute:
	radv_device_finish_meta_fast_clear_flush_state(device);
fail_fast_clear:
	radv_device_finish_meta_query_state(device);
fail_query:
	radv_device_finish_meta_buffer_state(device);
fail_buffer:
	radv_device_finish_meta_depth_decomp_state(device);
fail_depth_decomp:
	radv_device_finish_meta_bufimage_state(device);
fail_bufimage:
	radv_device_finish_meta_blit2d_state(device);
fail_blit2d:
	radv_device_finish_meta_blit_state(device);
fail_blit:
	radv_device_finish_meta_resolve_state(device);
fail_resolve:
	radv_device_finish_meta_clear_state(device);
fail_clear:
	radv_pipeline_cache_finish(&device->meta_state.cache);
	return result;
}

void
radv_device_finish_meta(struct radv_device *device)
{
	radv_device_finish_meta_clear_state(device);
	radv_device_finish_meta_resolve_state(device);
	radv_device_finish_meta_blit_state(device);
	radv_device_finish_meta_blit2d_state(device);
	radv_device_finish_meta_bufimage_state(device);
	radv_device_finish_meta_depth_decomp_state(device);
	radv_device_finish_meta_query_state(device);
	radv_device_finish_meta_buffer_state(device);
	radv_device_finish_meta_fast_clear_flush_state(device);
	radv_device_finish_meta_resolve_compute_state(device);
	radv_device_finish_meta_resolve_fragment_state(device);

	radv_store_meta_pipeline(device);
	radv_pipeline_cache_finish(&device->meta_state.cache);
}

nir_ssa_def *radv_meta_gen_rect_vertices_comp2(nir_builder *vs_b, nir_ssa_def *comp2)
{

	nir_intrinsic_instr *vertex_id = nir_intrinsic_instr_create(vs_b->shader, nir_intrinsic_load_vertex_id_zero_base);
	nir_ssa_dest_init(&vertex_id->instr, &vertex_id->dest, 1, 32, "vertexid");
	nir_builder_instr_insert(vs_b, &vertex_id->instr);

	/* vertex 0 - -1.0, -1.0 */
	/* vertex 1 - -1.0, 1.0 */
	/* vertex 2 - 1.0, -1.0 */
	/* so channel 0 is vertex_id != 2 ? -1.0 : 1.0
	   channel 1 is vertex id != 1 ? -1.0 : 1.0 */

	nir_ssa_def *c0cmp = nir_ine(vs_b, &vertex_id->dest.ssa,
				     nir_imm_int(vs_b, 2));
	nir_ssa_def *c1cmp = nir_ine(vs_b, &vertex_id->dest.ssa,
				     nir_imm_int(vs_b, 1));

	nir_ssa_def *comp[4];
	comp[0] = nir_bcsel(vs_b, c0cmp,
			    nir_imm_float(vs_b, -1.0),
			    nir_imm_float(vs_b, 1.0));

	comp[1] = nir_bcsel(vs_b, c1cmp,
			    nir_imm_float(vs_b, -1.0),
			    nir_imm_float(vs_b, 1.0));
	comp[2] = comp2;
	comp[3] = nir_imm_float(vs_b, 1.0);
	nir_ssa_def *outvec = nir_vec(vs_b, comp, 4);

	return outvec;
}

nir_ssa_def *radv_meta_gen_rect_vertices(nir_builder *vs_b)
{
	return radv_meta_gen_rect_vertices_comp2(vs_b, nir_imm_float(vs_b, 0.0));
}

/* vertex shader that generates vertices */
nir_shader *
radv_meta_build_nir_vs_generate_vertices(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();

	nir_builder b;
	nir_variable *v_position;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "meta_vs_gen_verts");

	nir_ssa_def *outvec = radv_meta_gen_rect_vertices(&b);

	v_position = nir_variable_create(b.shader, nir_var_shader_out, vec4,
					 "gl_Position");
	v_position->data.location = VARYING_SLOT_POS;

	nir_store_var(&b, v_position, outvec, 0xf);

	return b.shader;
}

nir_shader *
radv_meta_build_nir_fs_noop(void)
{
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info.name = ralloc_asprintf(b.shader,
					       "meta_noop_fs");

	return b.shader;
}

void radv_meta_build_resolve_shader_core(nir_builder *b,
					 bool is_integer,
					 int samples,
					 nir_variable *input_img,
					 nir_variable *color,
					 nir_ssa_def *img_coord)
{
	/* do a txf_ms on each sample */
	nir_ssa_def *tmp;
	nir_if *outer_if = NULL;

	nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
	tex->op = nir_texop_txf_ms;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(img_coord);
	tex->src[1].src_type = nir_tex_src_ms_index;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(b, &tex->instr);

	tmp = &tex->dest.ssa;

	if (!is_integer && samples > 1) {
		nir_tex_instr *tex_all_same = nir_tex_instr_create(b->shader, 1);
		tex_all_same->sampler_dim = GLSL_SAMPLER_DIM_MS;
		tex_all_same->op = nir_texop_samples_identical;
		tex_all_same->src[0].src_type = nir_tex_src_coord;
		tex_all_same->src[0].src = nir_src_for_ssa(img_coord);
		tex_all_same->dest_type = nir_type_float;
		tex_all_same->is_array = false;
		tex_all_same->coord_components = 2;
		tex_all_same->texture = nir_deref_var_create(tex_all_same, input_img);
		tex_all_same->sampler = NULL;

		nir_ssa_dest_init(&tex_all_same->instr, &tex_all_same->dest, 1, 32, "tex");
		nir_builder_instr_insert(b, &tex_all_same->instr);

		nir_ssa_def *all_same = nir_ieq(b, &tex_all_same->dest.ssa, nir_imm_int(b, 0));
		nir_if *if_stmt = nir_if_create(b->shader);
		if_stmt->condition = nir_src_for_ssa(all_same);
		nir_cf_node_insert(b->cursor, &if_stmt->cf_node);

		b->cursor = nir_after_cf_list(&if_stmt->then_list);
		for (int i = 1; i < samples; i++) {
			nir_tex_instr *tex_add = nir_tex_instr_create(b->shader, 2);
			tex_add->sampler_dim = GLSL_SAMPLER_DIM_MS;
			tex_add->op = nir_texop_txf_ms;
			tex_add->src[0].src_type = nir_tex_src_coord;
			tex_add->src[0].src = nir_src_for_ssa(img_coord);
			tex_add->src[1].src_type = nir_tex_src_ms_index;
			tex_add->src[1].src = nir_src_for_ssa(nir_imm_int(b, i));
			tex_add->dest_type = nir_type_float;
			tex_add->is_array = false;
			tex_add->coord_components = 2;
			tex_add->texture = nir_deref_var_create(tex_add, input_img);
			tex_add->sampler = NULL;

			nir_ssa_dest_init(&tex_add->instr, &tex_add->dest, 4, 32, "tex");
			nir_builder_instr_insert(b, &tex_add->instr);

			tmp = nir_fadd(b, tmp, &tex_add->dest.ssa);
		}

		tmp = nir_fdiv(b, tmp, nir_imm_float(b, samples));
		nir_store_var(b, color, tmp, 0xf);
		b->cursor = nir_after_cf_list(&if_stmt->else_list);
		outer_if = if_stmt;
	}
	nir_store_var(b, color, &tex->dest.ssa, 0xf);

	if (outer_if)
		b->cursor = nir_after_cf_node(&outer_if->cf_node);
}
