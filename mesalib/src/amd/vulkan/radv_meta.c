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
	       const struct radv_cmd_buffer *cmd_buffer,
	       uint32_t dynamic_mask)
{
	state->old_pipeline = cmd_buffer->state.pipeline;
	state->old_descriptor_set0 = cmd_buffer->state.descriptors[0];
	memcpy(state->old_vertex_bindings, cmd_buffer->state.vertex_bindings,
	       sizeof(state->old_vertex_bindings));

	state->dynamic_mask = dynamic_mask;
	radv_dynamic_state_copy(&state->dynamic, &cmd_buffer->state.dynamic,
				dynamic_mask);

	memcpy(state->push_constants, cmd_buffer->push_constants, MAX_PUSH_CONSTANTS_SIZE);
}

void
radv_meta_restore(const struct radv_meta_saved_state *state,
		  struct radv_cmd_buffer *cmd_buffer)
{
	cmd_buffer->state.pipeline = state->old_pipeline;
	radv_bind_descriptor_set(cmd_buffer, state->old_descriptor_set0, 0);
	memcpy(cmd_buffer->state.vertex_bindings, state->old_vertex_bindings,
	       sizeof(state->old_vertex_bindings));

	cmd_buffer->state.vb_dirty |= (1 << RADV_META_VERTEX_BINDING_COUNT) - 1;
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_PIPELINE;

	radv_dynamic_state_copy(&cmd_buffer->state.dynamic, &state->dynamic,
				state->dynamic_mask);
	cmd_buffer->state.dirty |= state->dynamic_mask;

	memcpy(cmd_buffer->push_constants, state->push_constants, MAX_PUSH_CONSTANTS_SIZE);
	cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
}

void
radv_meta_save_pass(struct radv_meta_saved_pass_state *state,
                    const struct radv_cmd_buffer *cmd_buffer)
{
	state->pass = cmd_buffer->state.pass;
	state->subpass = cmd_buffer->state.subpass;
	state->framebuffer = cmd_buffer->state.framebuffer;
	state->attachments = cmd_buffer->state.attachments;
	state->render_area = cmd_buffer->state.render_area;
}

void
radv_meta_restore_pass(const struct radv_meta_saved_pass_state *state,
                       struct radv_cmd_buffer *cmd_buffer)
{
	cmd_buffer->state.pass = state->pass;
	cmd_buffer->state.subpass = state->subpass;
	cmd_buffer->state.framebuffer = state->framebuffer;
	cmd_buffer->state.attachments = state->attachments;
	cmd_buffer->state.render_area = state->render_area;
	if (state->subpass)
		radv_emit_framebuffer_state(cmd_buffer);
}

void
radv_meta_save_compute(struct radv_meta_saved_compute_state *state,
                       const struct radv_cmd_buffer *cmd_buffer,
                       unsigned push_constant_size)
{
	state->old_pipeline = cmd_buffer->state.compute_pipeline;
	state->old_descriptor_set0 = cmd_buffer->state.descriptors[0];

	if (push_constant_size)
		memcpy(state->push_constants, cmd_buffer->push_constants, push_constant_size);
}

void
radv_meta_restore_compute(const struct radv_meta_saved_compute_state *state,
                          struct radv_cmd_buffer *cmd_buffer,
                          unsigned push_constant_size)
{
	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
			     radv_pipeline_to_handle(state->old_pipeline));
	radv_bind_descriptor_set(cmd_buffer, state->old_descriptor_set0, 0);

	if (push_constant_size) {
		memcpy(cmd_buffer->push_constants, state->push_constants, push_constant_size);
		cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_COMPUTE_BIT;
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

	result = radv_device_init_meta_fast_clear_flush_state(device);
	if (result != VK_SUCCESS)
		goto fail_fast_clear;

	result = radv_device_init_meta_resolve_compute_state(device);
	if (result != VK_SUCCESS)
		goto fail_resolve_compute;
	return VK_SUCCESS;

fail_resolve_compute:
	radv_device_finish_meta_fast_clear_flush_state(device);
fail_fast_clear:
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
	radv_device_finish_meta_buffer_state(device);
	radv_device_finish_meta_fast_clear_flush_state(device);
	radv_device_finish_meta_resolve_compute_state(device);

	radv_store_meta_pipeline(device);
	radv_pipeline_cache_finish(&device->meta_state.cache);
}

/*
 * The most common meta operations all want to have the viewport
 * reset and any scissors disabled. The rest of the dynamic state
 * should have no effect.
 */
void
radv_meta_save_graphics_reset_vport_scissor(struct radv_meta_saved_state *saved_state,
					    struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t dirty_state = (1 << VK_DYNAMIC_STATE_VIEWPORT) | (1 << VK_DYNAMIC_STATE_SCISSOR);
	radv_meta_save(saved_state, cmd_buffer, dirty_state);
	cmd_buffer->state.dynamic.viewport.count = 0;
	cmd_buffer->state.dynamic.scissor.count = 0;
	cmd_buffer->state.dirty |= dirty_state;
}
