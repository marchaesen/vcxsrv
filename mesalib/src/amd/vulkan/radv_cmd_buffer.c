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

#include "radv_private.h"
#include "radv_radeon_winsys.h"
#include "radv_cs.h"
#include "sid.h"
#include "vk_format.h"
#include "radv_meta.h"

static void radv_handle_image_transition(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageLayout src_layout,
					 VkImageLayout dst_layout,
					 VkImageSubresourceRange range,
					 VkImageAspectFlags pending_clears);

const struct radv_dynamic_state default_dynamic_state = {
	.viewport = {
		.count = 0,
	},
	.scissor = {
		.count = 0,
	},
	.line_width = 1.0f,
	.depth_bias = {
		.bias = 0.0f,
		.clamp = 0.0f,
		.slope = 0.0f,
	},
	.blend_constants = { 0.0f, 0.0f, 0.0f, 0.0f },
	.depth_bounds = {
		.min = 0.0f,
		.max = 1.0f,
	},
	.stencil_compare_mask = {
		.front = ~0u,
		.back = ~0u,
	},
	.stencil_write_mask = {
		.front = ~0u,
		.back = ~0u,
	},
	.stencil_reference = {
		.front = 0u,
		.back = 0u,
	},
};

void
radv_dynamic_state_copy(struct radv_dynamic_state *dest,
			const struct radv_dynamic_state *src,
			uint32_t copy_mask)
{
	if (copy_mask & (1 << VK_DYNAMIC_STATE_VIEWPORT)) {
		dest->viewport.count = src->viewport.count;
		typed_memcpy(dest->viewport.viewports, src->viewport.viewports,
			     src->viewport.count);
	}

	if (copy_mask & (1 << VK_DYNAMIC_STATE_SCISSOR)) {
		dest->scissor.count = src->scissor.count;
		typed_memcpy(dest->scissor.scissors, src->scissor.scissors,
			     src->scissor.count);
	}

	if (copy_mask & (1 << VK_DYNAMIC_STATE_LINE_WIDTH))
		dest->line_width = src->line_width;

	if (copy_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BIAS))
		dest->depth_bias = src->depth_bias;

	if (copy_mask & (1 << VK_DYNAMIC_STATE_BLEND_CONSTANTS))
		typed_memcpy(dest->blend_constants, src->blend_constants, 4);

	if (copy_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BOUNDS))
		dest->depth_bounds = src->depth_bounds;

	if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK))
		dest->stencil_compare_mask = src->stencil_compare_mask;

	if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK))
		dest->stencil_write_mask = src->stencil_write_mask;

	if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE))
		dest->stencil_reference = src->stencil_reference;
}

static VkResult radv_create_cmd_buffer(
	struct radv_device *                         device,
	struct radv_cmd_pool *                       pool,
	VkCommandBufferLevel                        level,
	VkCommandBuffer*                            pCommandBuffer)
{
	struct radv_cmd_buffer *cmd_buffer;
	VkResult result;

	cmd_buffer = vk_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
				VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (cmd_buffer == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(cmd_buffer, 0, sizeof(*cmd_buffer));
	cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	cmd_buffer->device = device;
	cmd_buffer->pool = pool;
	cmd_buffer->level = level;

	if (pool) {
		list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
	} else {
		/* Init the pool_link so we can safefly call list_del when we destroy
		 * the command buffer
		 */
		list_inithead(&cmd_buffer->pool_link);
	}

	cmd_buffer->cs = device->ws->cs_create(device->ws, RING_GFX);
	if (!cmd_buffer->cs) {
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	*pCommandBuffer = radv_cmd_buffer_to_handle(cmd_buffer);

	cmd_buffer->upload.offset = 0;
	cmd_buffer->upload.size = 0;
	list_inithead(&cmd_buffer->upload.list);

	return VK_SUCCESS;

fail:
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer);

	return result;
}

static bool
radv_cmd_buffer_resize_upload_buf(struct radv_cmd_buffer *cmd_buffer,
				  uint64_t min_needed)
{
	uint64_t new_size;
	struct radeon_winsys_bo *bo;
	struct radv_cmd_buffer_upload *upload;
	struct radv_device *device = cmd_buffer->device;

	new_size = MAX2(min_needed, 16 * 1024);
	new_size = MAX2(new_size, 2 * cmd_buffer->upload.size);

	bo = device->ws->buffer_create(device->ws,
				       new_size, 4096,
				       RADEON_DOMAIN_GTT,
				       RADEON_FLAG_CPU_ACCESS);

	if (!bo) {
		cmd_buffer->record_fail = true;
		return false;
	}

	device->ws->cs_add_buffer(cmd_buffer->cs, bo, 8);
	if (cmd_buffer->upload.upload_bo) {
		upload = malloc(sizeof(*upload));

		if (!upload) {
			cmd_buffer->record_fail = true;
			device->ws->buffer_destroy(bo);
			return false;
		}

		memcpy(upload, &cmd_buffer->upload, sizeof(*upload));
		list_add(&upload->list, &cmd_buffer->upload.list);
	}

	cmd_buffer->upload.upload_bo = bo;
	cmd_buffer->upload.size = new_size;
	cmd_buffer->upload.offset = 0;
	cmd_buffer->upload.map = device->ws->buffer_map(cmd_buffer->upload.upload_bo);

	if (!cmd_buffer->upload.map) {
		cmd_buffer->record_fail = true;
		return false;
	}

	return true;
}

bool
radv_cmd_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer,
			     unsigned size,
			     unsigned alignment,
			     unsigned *out_offset,
			     void **ptr)
{
	uint64_t offset = align(cmd_buffer->upload.offset, alignment);
	if (offset + size > cmd_buffer->upload.size) {
		if (!radv_cmd_buffer_resize_upload_buf(cmd_buffer, size))
			return false;
		offset = 0;
	}

	*out_offset = offset;
	*ptr = cmd_buffer->upload.map + offset;

	cmd_buffer->upload.offset = offset + size;
	return true;
}

bool
radv_cmd_buffer_upload_data(struct radv_cmd_buffer *cmd_buffer,
			    unsigned size, unsigned alignment,
			    const void *data, unsigned *out_offset)
{
	uint8_t *ptr;

	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, size, alignment,
					  out_offset, (void **)&ptr))
		return false;

	if (ptr)
		memcpy(ptr, data, size);

	return true;
}

static void
radv_emit_graphics_blend_state(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_pipeline *pipeline)
{
	radeon_set_context_reg_seq(cmd_buffer->cs, R_028780_CB_BLEND0_CONTROL, 8);
	radeon_emit_array(cmd_buffer->cs, pipeline->graphics.blend.cb_blend_control,
			  8);
	radeon_set_context_reg(cmd_buffer->cs, R_028808_CB_COLOR_CONTROL, pipeline->graphics.blend.cb_color_control);
	radeon_set_context_reg(cmd_buffer->cs, R_028B70_DB_ALPHA_TO_MASK, pipeline->graphics.blend.db_alpha_to_mask);
}

static void
radv_emit_graphics_depth_stencil_state(struct radv_cmd_buffer *cmd_buffer,
				       struct radv_pipeline *pipeline)
{
	struct radv_depth_stencil_state *ds = &pipeline->graphics.ds;
	radeon_set_context_reg(cmd_buffer->cs, R_028800_DB_DEPTH_CONTROL, ds->db_depth_control);
	radeon_set_context_reg(cmd_buffer->cs, R_02842C_DB_STENCIL_CONTROL, ds->db_stencil_control);

	radeon_set_context_reg(cmd_buffer->cs, R_028000_DB_RENDER_CONTROL, ds->db_render_control);
	radeon_set_context_reg(cmd_buffer->cs, R_028010_DB_RENDER_OVERRIDE2, ds->db_render_override2);
}

/* 12.4 fixed-point */
static unsigned radv_pack_float_12p4(float x)
{
	return x <= 0    ? 0 :
	       x >= 4096 ? 0xffff : x * 16;
}

static void
radv_update_multisample_state(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_pipeline *pipeline)
{
	int num_samples = pipeline->graphics.ms.num_samples;
	struct radv_multisample_state *ms = &pipeline->graphics.ms;
	struct radv_pipeline *old_pipeline = cmd_buffer->state.emitted_pipeline;

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_aa_mask[0]);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_aa_mask[1]);

	if (old_pipeline && num_samples == old_pipeline->graphics.ms.num_samples)
		return;

	radeon_set_context_reg_seq(cmd_buffer->cs, CM_R_028BDC_PA_SC_LINE_CNTL, 2);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_line_cntl);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_aa_config);

	radeon_set_context_reg(cmd_buffer->cs, CM_R_028804_DB_EQAA, ms->db_eqaa);
	radeon_set_context_reg(cmd_buffer->cs, EG_R_028A4C_PA_SC_MODE_CNTL_1, ms->pa_sc_mode_cntl_1);

	radv_cayman_emit_msaa_sample_locs(cmd_buffer->cs, num_samples);

	uint32_t samples_offset;
	void *samples_ptr;
	void *src;
	radv_cmd_buffer_upload_alloc(cmd_buffer, num_samples * 4 * 2, 256, &samples_offset,
				     &samples_ptr);
	switch (num_samples) {
	case 1:
		src = cmd_buffer->device->sample_locations_1x;
		break;
	case 2:
		src = cmd_buffer->device->sample_locations_2x;
		break;
	case 4:
		src = cmd_buffer->device->sample_locations_4x;
		break;
	case 8:
		src = cmd_buffer->device->sample_locations_8x;
		break;
	case 16:
		src = cmd_buffer->device->sample_locations_16x;
		break;
	}
	memcpy(samples_ptr, src, num_samples * 4 * 2);

	uint64_t va = cmd_buffer->device->ws->buffer_get_va(cmd_buffer->upload.upload_bo);
	va += samples_offset;

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B030_SPI_SHADER_USER_DATA_PS_0 + AC_USERDATA_PS_SAMPLE_POS * 4, 2);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
}

static void
radv_emit_graphics_raster_state(struct radv_cmd_buffer *cmd_buffer,
				struct radv_pipeline *pipeline)
{
	struct radv_raster_state *raster = &pipeline->graphics.raster;

	radeon_set_context_reg(cmd_buffer->cs, R_028810_PA_CL_CLIP_CNTL,
			       raster->pa_cl_clip_cntl);

	radeon_set_context_reg(cmd_buffer->cs, R_0286D4_SPI_INTERP_CONTROL_0,
			       raster->spi_interp_control);

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028A00_PA_SU_POINT_SIZE, 2);
	radeon_emit(cmd_buffer->cs, 0);
	radeon_emit(cmd_buffer->cs, S_028A04_MIN_SIZE(radv_pack_float_12p4(0)) |
		    S_028A04_MAX_SIZE(radv_pack_float_12p4(8192/2))); /* R_028A04_PA_SU_POINT_MINMAX */

	radeon_set_context_reg(cmd_buffer->cs, R_028BE4_PA_SU_VTX_CNTL,
			       raster->pa_su_vtx_cntl);

	radeon_set_context_reg(cmd_buffer->cs, R_028814_PA_SU_SC_MODE_CNTL,
			       raster->pa_su_sc_mode_cntl);
}

static void
radv_emit_vertex_shader(struct radv_cmd_buffer *cmd_buffer,
			struct radv_pipeline *pipeline)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radv_shader_variant *vs;
	uint64_t va;
	unsigned export_count;
	unsigned clip_dist_mask, cull_dist_mask, total_mask;

	assert (pipeline->shaders[MESA_SHADER_VERTEX]);

	vs = pipeline->shaders[MESA_SHADER_VERTEX];
	va = ws->buffer_get_va(vs->bo);
	ws->cs_add_buffer(cmd_buffer->cs, vs->bo, 8);

	clip_dist_mask = vs->info.vs.clip_dist_mask;
	cull_dist_mask = vs->info.vs.cull_dist_mask;
	total_mask = clip_dist_mask | cull_dist_mask;
	radeon_set_context_reg(cmd_buffer->cs, R_028A40_VGT_GS_MODE, 0);
	radeon_set_context_reg(cmd_buffer->cs, R_028A84_VGT_PRIMITIVEID_EN, 0);

	export_count = MAX2(1, vs->info.vs.param_exports);
	radeon_set_context_reg(cmd_buffer->cs, R_0286C4_SPI_VS_OUT_CONFIG,
			       S_0286C4_VS_EXPORT_COUNT(export_count - 1));
	radeon_set_context_reg(cmd_buffer->cs, R_02870C_SPI_SHADER_POS_FORMAT,
			       S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
			       S_02870C_POS1_EXPORT_FORMAT(vs->info.vs.pos_exports > 1 ?
							   V_02870C_SPI_SHADER_4COMP :
							   V_02870C_SPI_SHADER_NONE) |
			       S_02870C_POS2_EXPORT_FORMAT(vs->info.vs.pos_exports > 2 ?
							   V_02870C_SPI_SHADER_4COMP :
							   V_02870C_SPI_SHADER_NONE) |
			       S_02870C_POS3_EXPORT_FORMAT(vs->info.vs.pos_exports > 3 ?
							   V_02870C_SPI_SHADER_4COMP :
							   V_02870C_SPI_SHADER_NONE));

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B120_SPI_SHADER_PGM_LO_VS, 4);
	radeon_emit(cmd_buffer->cs, va >> 8);
	radeon_emit(cmd_buffer->cs, va >> 40);
	radeon_emit(cmd_buffer->cs, vs->rsrc1);
	radeon_emit(cmd_buffer->cs, vs->rsrc2);

	radeon_set_context_reg(cmd_buffer->cs, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_W0_FMT(1) |
			       S_028818_VPORT_X_SCALE_ENA(1) | S_028818_VPORT_X_OFFSET_ENA(1) |
			       S_028818_VPORT_Y_SCALE_ENA(1) | S_028818_VPORT_Y_OFFSET_ENA(1) |
			       S_028818_VPORT_Z_SCALE_ENA(1) | S_028818_VPORT_Z_OFFSET_ENA(1));

	radeon_set_context_reg(cmd_buffer->cs, R_02881C_PA_CL_VS_OUT_CNTL,
			       S_02881C_USE_VTX_POINT_SIZE(vs->info.vs.writes_pointsize) |
			       S_02881C_VS_OUT_MISC_VEC_ENA(vs->info.vs.writes_pointsize) |
			       S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
			       S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
			       pipeline->graphics.raster.pa_cl_vs_out_cntl |
			       cull_dist_mask << 8 |
			       clip_dist_mask);

}



static void
radv_emit_fragment_shader(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_pipeline *pipeline)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radv_shader_variant *ps, *vs;
	uint64_t va;
	unsigned spi_baryc_cntl = S_0286E0_FRONT_FACE_ALL_BITS(1);
	struct radv_blend_state *blend = &pipeline->graphics.blend;
	unsigned ps_offset = 0;
	unsigned z_order;
	assert (pipeline->shaders[MESA_SHADER_FRAGMENT]);

	ps = pipeline->shaders[MESA_SHADER_FRAGMENT];
	vs = pipeline->shaders[MESA_SHADER_VERTEX];
	va = ws->buffer_get_va(ps->bo);
	ws->cs_add_buffer(cmd_buffer->cs, ps->bo, 8);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B020_SPI_SHADER_PGM_LO_PS, 4);
	radeon_emit(cmd_buffer->cs, va >> 8);
	radeon_emit(cmd_buffer->cs, va >> 40);
	radeon_emit(cmd_buffer->cs, ps->rsrc1);
	radeon_emit(cmd_buffer->cs, ps->rsrc2);

	if (ps->info.fs.early_fragment_test || !ps->info.fs.writes_memory)
		z_order = V_02880C_EARLY_Z_THEN_LATE_Z;
	else
		z_order = V_02880C_LATE_Z;


	radeon_set_context_reg(cmd_buffer->cs, R_02880C_DB_SHADER_CONTROL,
			       S_02880C_Z_EXPORT_ENABLE(ps->info.fs.writes_z) |
			       S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(ps->info.fs.writes_stencil) |
			       S_02880C_KILL_ENABLE(!!ps->info.fs.can_discard) |
			       S_02880C_Z_ORDER(z_order) |
			       S_02880C_DEPTH_BEFORE_SHADER(ps->info.fs.early_fragment_test) |
			       S_02880C_EXEC_ON_HIER_FAIL(ps->info.fs.writes_memory) |
			       S_02880C_EXEC_ON_NOOP(ps->info.fs.writes_memory));

	radeon_set_context_reg(cmd_buffer->cs, R_0286CC_SPI_PS_INPUT_ENA,
			       ps->config.spi_ps_input_ena);

	radeon_set_context_reg(cmd_buffer->cs, R_0286D0_SPI_PS_INPUT_ADDR,
			       ps->config.spi_ps_input_addr);

	spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);
	radeon_set_context_reg(cmd_buffer->cs, R_0286D8_SPI_PS_IN_CONTROL,
			       S_0286D8_NUM_INTERP(ps->info.fs.num_interp));

	radeon_set_context_reg(cmd_buffer->cs, R_0286E0_SPI_BARYC_CNTL, spi_baryc_cntl);

	radeon_set_context_reg(cmd_buffer->cs, R_028710_SPI_SHADER_Z_FORMAT,
			       ps->info.fs.writes_stencil ? V_028710_SPI_SHADER_32_GR :
			       ps->info.fs.writes_z ? V_028710_SPI_SHADER_32_R :
			       V_028710_SPI_SHADER_ZERO);

	radeon_set_context_reg(cmd_buffer->cs, R_028714_SPI_SHADER_COL_FORMAT, blend->spi_shader_col_format);

	radeon_set_context_reg(cmd_buffer->cs, R_028238_CB_TARGET_MASK, blend->cb_target_mask);
	radeon_set_context_reg(cmd_buffer->cs, R_02823C_CB_SHADER_MASK, blend->cb_shader_mask);

	if (ps->info.fs.has_pcoord) {
		unsigned val;
		val = S_028644_PT_SPRITE_TEX(1) | S_028644_OFFSET(0x20);
		radeon_set_context_reg(cmd_buffer->cs, R_028644_SPI_PS_INPUT_CNTL_0 + 4 * ps_offset, val);
		ps_offset = 1;
	}

	for (unsigned i = 0; i < 32 && (1u << i) <= ps->info.fs.input_mask; ++i) {
		unsigned vs_offset, flat_shade;
		unsigned val;

		if (!(ps->info.fs.input_mask & (1u << i)))
			continue;


		if (!(vs->info.vs.export_mask & (1u << i))) {
			radeon_set_context_reg(cmd_buffer->cs, R_028644_SPI_PS_INPUT_CNTL_0 + 4 * ps_offset,
					       S_028644_OFFSET(0x20));
			++ps_offset;
			continue;
		}

		vs_offset = util_bitcount(vs->info.vs.export_mask & ((1u << i) - 1));
		flat_shade = !!(ps->info.fs.flat_shaded_mask & (1u << ps_offset));

		val = S_028644_OFFSET(vs_offset) | S_028644_FLAT_SHADE(flat_shade);
		radeon_set_context_reg(cmd_buffer->cs, R_028644_SPI_PS_INPUT_CNTL_0 + 4 * ps_offset, val);
		++ps_offset;
	}
}

static void
radv_emit_graphics_pipeline(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_pipeline *pipeline)
{
	if (!pipeline || cmd_buffer->state.emitted_pipeline == pipeline)
		return;

	radv_emit_graphics_depth_stencil_state(cmd_buffer, pipeline);
	radv_emit_graphics_blend_state(cmd_buffer, pipeline);
	radv_emit_graphics_raster_state(cmd_buffer, pipeline);
	radv_update_multisample_state(cmd_buffer, pipeline);
	radv_emit_vertex_shader(cmd_buffer, pipeline);
	radv_emit_fragment_shader(cmd_buffer, pipeline);

	radeon_set_context_reg(cmd_buffer->cs, R_028A94_VGT_MULTI_PRIM_IB_RESET_EN,
			       pipeline->graphics.prim_restart_enable);

	cmd_buffer->state.emitted_pipeline = pipeline;
}

static void
radv_emit_viewport(struct radv_cmd_buffer *cmd_buffer)
{
	si_write_viewport(cmd_buffer->cs, 0, cmd_buffer->state.dynamic.viewport.count,
			  cmd_buffer->state.dynamic.viewport.viewports);
}

static void
radv_emit_scissor(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t count = cmd_buffer->state.dynamic.scissor.count;
	si_write_scissors(cmd_buffer->cs, 0, count,
			  cmd_buffer->state.dynamic.scissor.scissors);
	radeon_set_context_reg(cmd_buffer->cs, R_028A48_PA_SC_MODE_CNTL_0,
			       cmd_buffer->state.pipeline->graphics.ms.pa_sc_mode_cntl_0 | S_028A48_VPORT_SCISSOR_ENABLE(count ? 1 : 0));
}

static void
radv_emit_fb_color_state(struct radv_cmd_buffer *cmd_buffer,
			 int index,
			 struct radv_color_buffer_info *cb)
{
	bool is_vi = cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= VI;
	radeon_set_context_reg_seq(cmd_buffer->cs, R_028C60_CB_COLOR0_BASE + index * 0x3c, 11);
	radeon_emit(cmd_buffer->cs, cb->cb_color_base);
	radeon_emit(cmd_buffer->cs, cb->cb_color_pitch);
	radeon_emit(cmd_buffer->cs, cb->cb_color_slice);
	radeon_emit(cmd_buffer->cs, cb->cb_color_view);
	radeon_emit(cmd_buffer->cs, cb->cb_color_info);
	radeon_emit(cmd_buffer->cs, cb->cb_color_attrib);
	radeon_emit(cmd_buffer->cs, cb->cb_dcc_control);
	radeon_emit(cmd_buffer->cs, cb->cb_color_cmask);
	radeon_emit(cmd_buffer->cs, cb->cb_color_cmask_slice);
	radeon_emit(cmd_buffer->cs, cb->cb_color_fmask);
	radeon_emit(cmd_buffer->cs, cb->cb_color_fmask_slice);

	if (is_vi) { /* DCC BASE */
		radeon_set_context_reg(cmd_buffer->cs, R_028C94_CB_COLOR0_DCC_BASE + index * 0x3c, cb->cb_dcc_base);
	}
}

static void
radv_emit_fb_ds_state(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_ds_buffer_info *ds,
		      struct radv_image *image,
		      VkImageLayout layout)
{
	uint32_t db_z_info = ds->db_z_info;

	if (!radv_layout_has_htile(image, layout))
		db_z_info &= C_028040_TILE_SURFACE_ENABLE;

	if (!radv_layout_can_expclear(image, layout))
		db_z_info &= C_028040_ALLOW_EXPCLEAR & C_028044_ALLOW_EXPCLEAR;

	radeon_set_context_reg(cmd_buffer->cs, R_028008_DB_DEPTH_VIEW, ds->db_depth_view);
	radeon_set_context_reg(cmd_buffer->cs, R_028014_DB_HTILE_DATA_BASE, ds->db_htile_data_base);

	radeon_set_context_reg_seq(cmd_buffer->cs, R_02803C_DB_DEPTH_INFO, 9);
	radeon_emit(cmd_buffer->cs, ds->db_depth_info);	/* R_02803C_DB_DEPTH_INFO */
	radeon_emit(cmd_buffer->cs, db_z_info);			/* R_028040_DB_Z_INFO */
	radeon_emit(cmd_buffer->cs, ds->db_stencil_info);	/* R_028044_DB_STENCIL_INFO */
	radeon_emit(cmd_buffer->cs, ds->db_z_read_base);	/* R_028048_DB_Z_READ_BASE */
	radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base);	/* R_02804C_DB_STENCIL_READ_BASE */
	radeon_emit(cmd_buffer->cs, ds->db_z_write_base);	/* R_028050_DB_Z_WRITE_BASE */
	radeon_emit(cmd_buffer->cs, ds->db_stencil_write_base);	/* R_028054_DB_STENCIL_WRITE_BASE */
	radeon_emit(cmd_buffer->cs, ds->db_depth_size);	/* R_028058_DB_DEPTH_SIZE */
	radeon_emit(cmd_buffer->cs, ds->db_depth_slice);	/* R_02805C_DB_DEPTH_SLICE */

	radeon_set_context_reg(cmd_buffer->cs, R_028ABC_DB_HTILE_SURFACE, ds->db_htile_surface);
	radeon_set_context_reg(cmd_buffer->cs, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
			       ds->pa_su_poly_offset_db_fmt_cntl);
}

/*
 * To hw resolve multisample images both src and dst need to have the same
 * micro tiling mode. However we don't always know in advance when creating
 * the images. This function gets called if we have a resolve attachment,
 * and tests if the attachment image has the same tiling mode, then it
 * checks if the generated framebuffer data has the same tiling mode, and
 * updates it if not.
 */
static void radv_set_optimal_micro_tile_mode(struct radv_device *device,
					     struct radv_attachment_info *att,
					     uint32_t micro_tile_mode)
{
	struct radv_image *image = att->attachment->image;
	uint32_t tile_mode_index;
	if (image->surface.nsamples <= 1)
		return;

	if (image->surface.micro_tile_mode != micro_tile_mode) {
		radv_image_set_optimal_micro_tile_mode(device, image, micro_tile_mode);
	}

	if (att->cb.micro_tile_mode != micro_tile_mode) {
		tile_mode_index = image->surface.tiling_index[0];

		att->cb.cb_color_attrib &= C_028C74_TILE_MODE_INDEX;
		att->cb.cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);
		att->cb.micro_tile_mode = micro_tile_mode;
	}
}

void
radv_set_depth_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  VkClearDepthStencilValue ds_clear_value,
			  VkImageAspectFlags aspects)
{
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;
	unsigned reg_offset = 0, reg_count = 0;

	if (!image->htile.size || !aspects)
		return;

	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		++reg_count;
	} else {
		++reg_offset;
		va += 4;
	}
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		++reg_count;

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, image->bo, 8);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + reg_count, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
		radeon_emit(cmd_buffer->cs, ds_clear_value.stencil);
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		radeon_emit(cmd_buffer->cs, fui(ds_clear_value.depth));

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028028_DB_STENCIL_CLEAR + 4 * reg_offset, reg_count);
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
		radeon_emit(cmd_buffer->cs, ds_clear_value.stencil); /* R_028028_DB_STENCIL_CLEAR */
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		radeon_emit(cmd_buffer->cs, fui(ds_clear_value.depth)); /* R_02802C_DB_DEPTH_CLEAR */
}

static void
radv_load_depth_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image)
{
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;

	if (!image->htile.size)
		return;

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, image->bo, 8);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_COPY_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
				    COPY_DATA_DST_SEL(COPY_DATA_REG) |
				    COPY_DATA_COUNT_SEL);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, R_028028_DB_STENCIL_CLEAR >> 2);
	radeon_emit(cmd_buffer->cs, 0);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
	radeon_emit(cmd_buffer->cs, 0);
}

void
radv_set_color_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  int idx,
			  uint32_t color_values[2])
{
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;

	if (!image->cmask.size && !image->surface.dcc_size)
		return;

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, image->bo, 8);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, color_values[0]);
	radeon_emit(cmd_buffer->cs, color_values[1]);

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028C8C_CB_COLOR0_CLEAR_WORD0 + idx * 0x3c, 2);
	radeon_emit(cmd_buffer->cs, color_values[0]);
	radeon_emit(cmd_buffer->cs, color_values[1]);
}

static void
radv_load_color_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image,
			   int idx)
{
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;

	if (!image->cmask.size && !image->surface.dcc_size)
		return;

	uint32_t reg = R_028C8C_CB_COLOR0_CLEAR_WORD0 + idx * 0x3c;
	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, image->bo, 8);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_COPY_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
				    COPY_DATA_DST_SEL(COPY_DATA_REG) |
				    COPY_DATA_COUNT_SEL);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, reg >> 2);
	radeon_emit(cmd_buffer->cs, 0);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
	radeon_emit(cmd_buffer->cs, 0);
}

void
radv_emit_framebuffer_state(struct radv_cmd_buffer *cmd_buffer)
{
	int i;
	struct radv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	int dst_resolve_micro_tile_mode = -1;

	if (subpass->has_resolve) {
		uint32_t a = subpass->resolve_attachments[0].attachment;
		const struct radv_image *image = framebuffer->attachments[a].attachment->image;
		dst_resolve_micro_tile_mode = image->surface.micro_tile_mode;
	}
	for (i = 0; i < subpass->color_count; ++i) {
		int idx = subpass->color_attachments[i].attachment;
		struct radv_attachment_info *att = &framebuffer->attachments[idx];

		if (dst_resolve_micro_tile_mode != -1) {
			radv_set_optimal_micro_tile_mode(cmd_buffer->device,
							 att, dst_resolve_micro_tile_mode);
		}
		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, att->attachment->bo, 8);

		assert(att->attachment->aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT);
		radv_emit_fb_color_state(cmd_buffer, i, &att->cb);

		radv_load_color_clear_regs(cmd_buffer, att->attachment->image, i);
	}

	for (i = subpass->color_count; i < 8; i++)
		radeon_set_context_reg(cmd_buffer->cs, R_028C70_CB_COLOR0_INFO + i * 0x3C,
				       S_028C70_FORMAT(V_028C70_COLOR_INVALID));

	if(subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		int idx = subpass->depth_stencil_attachment.attachment;
		VkImageLayout layout = subpass->depth_stencil_attachment.layout;
		struct radv_attachment_info *att = &framebuffer->attachments[idx];
		struct radv_image *image = att->attachment->image;
		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, att->attachment->bo, 8);

		radv_emit_fb_ds_state(cmd_buffer, &att->ds, image, layout);

		if (att->ds.offset_scale != cmd_buffer->state.offset_scale) {
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
			cmd_buffer->state.offset_scale = att->ds.offset_scale;
		}
		radv_load_depth_clear_regs(cmd_buffer, image);
	} else {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028040_DB_Z_INFO, 2);
		radeon_emit(cmd_buffer->cs, S_028040_FORMAT(V_028040_Z_INVALID)); /* R_028040_DB_Z_INFO */
		radeon_emit(cmd_buffer->cs, S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* R_028044_DB_STENCIL_INFO */
	}
	radeon_set_context_reg(cmd_buffer->cs, R_028208_PA_SC_WINDOW_SCISSOR_BR,
			       S_028208_BR_X(framebuffer->width) |
			       S_028208_BR_Y(framebuffer->height));
}

void radv_set_db_count_control(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t db_count_control;

	if(!cmd_buffer->state.active_occlusion_queries) {
		if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= CIK) {
			db_count_control = 0;
		} else {
			db_count_control = S_028004_ZPASS_INCREMENT_DISABLE(1);
		}
	} else {
		if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= CIK) {
			db_count_control = S_028004_PERFECT_ZPASS_COUNTS(1) |
				S_028004_SAMPLE_RATE(0) | /* TODO: set this to the number of samples of the current framebuffer */
				S_028004_ZPASS_ENABLE(1) |
				S_028004_SLICE_EVEN_ENABLE(1) |
				S_028004_SLICE_ODD_ENABLE(1);
		} else {
			db_count_control = S_028004_PERFECT_ZPASS_COUNTS(1) |
				S_028004_SAMPLE_RATE(0); /* TODO: set this to the number of samples of the current framebuffer */
		}
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028004_DB_COUNT_CONTROL, db_count_control);
}

static void
radv_cmd_buffer_flush_dynamic_state(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH) {
		unsigned width = cmd_buffer->state.dynamic.line_width * 8;
		radeon_set_context_reg(cmd_buffer->cs, R_028A08_PA_SU_LINE_CNTL,
				       S_028A08_WIDTH(CLAMP(width, 0, 0xFFF)));
	}

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS) {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028414_CB_BLEND_RED, 4);
		radeon_emit_array(cmd_buffer->cs, (uint32_t*)d->blend_constants, 4);
	}

	if (cmd_buffer->state.dirty & (RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE |
				       RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |
				       RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK)) {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028430_DB_STENCILREFMASK, 2);
		radeon_emit(cmd_buffer->cs, S_028430_STENCILTESTVAL(d->stencil_reference.front) |
			    S_028430_STENCILMASK(d->stencil_compare_mask.front) |
			    S_028430_STENCILWRITEMASK(d->stencil_write_mask.front) |
			    S_028430_STENCILOPVAL(1));
		radeon_emit(cmd_buffer->cs, S_028434_STENCILTESTVAL_BF(d->stencil_reference.back) |
			    S_028434_STENCILMASK_BF(d->stencil_compare_mask.back) |
			    S_028434_STENCILWRITEMASK_BF(d->stencil_write_mask.back) |
			    S_028434_STENCILOPVAL_BF(1));
	}

	if (cmd_buffer->state.dirty & (RADV_CMD_DIRTY_PIPELINE |
				       RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS)) {
		radeon_set_context_reg(cmd_buffer->cs, R_028020_DB_DEPTH_BOUNDS_MIN, fui(d->depth_bounds.min));
		radeon_set_context_reg(cmd_buffer->cs, R_028024_DB_DEPTH_BOUNDS_MAX, fui(d->depth_bounds.max));
	}

	if (cmd_buffer->state.dirty & (RADV_CMD_DIRTY_PIPELINE |
				       RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS)) {
		struct radv_raster_state *raster = &cmd_buffer->state.pipeline->graphics.raster;
		unsigned slope = fui(d->depth_bias.slope * 16.0f);
		unsigned bias = fui(d->depth_bias.bias * cmd_buffer->state.offset_scale);

		if (G_028814_POLY_OFFSET_FRONT_ENABLE(raster->pa_su_sc_mode_cntl)) {
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028B7C_PA_SU_POLY_OFFSET_CLAMP, 5);
			radeon_emit(cmd_buffer->cs, fui(d->depth_bias.clamp)); /* CLAMP */
			radeon_emit(cmd_buffer->cs, slope); /* FRONT SCALE */
			radeon_emit(cmd_buffer->cs, bias); /* FRONT OFFSET */
			radeon_emit(cmd_buffer->cs, slope); /* BACK SCALE */
			radeon_emit(cmd_buffer->cs, bias); /* BACK OFFSET */
		}
	}

	cmd_buffer->state.dirty = 0;
}

static void
radv_flush_constants(struct radv_cmd_buffer *cmd_buffer,
		     struct radv_pipeline_layout *layout,
		     VkShaderStageFlags stages) {
	unsigned offset;
	void *ptr;
	uint64_t va;

	stages &= cmd_buffer->push_constant_stages;
	if (!stages || !layout || (!layout->push_constant_size && !layout->dynamic_offset_count))
		return;

	radv_cmd_buffer_upload_alloc(cmd_buffer, layout->push_constant_size +
				     16 * layout->dynamic_offset_count,
				     256, &offset, &ptr);

	memcpy(ptr, cmd_buffer->push_constants, layout->push_constant_size);
	memcpy((char*)ptr + layout->push_constant_size, cmd_buffer->dynamic_buffers,
	       16 * layout->dynamic_offset_count);

	va = cmd_buffer->device->ws->buffer_get_va(cmd_buffer->upload.upload_bo);
	va += offset;

	if (stages & VK_SHADER_STAGE_VERTEX_BIT) {
		radeon_set_sh_reg_seq(cmd_buffer->cs,
				      R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_PUSH_CONST_DYN * 4, 2);
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
	}

	if (stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
		radeon_set_sh_reg_seq(cmd_buffer->cs,
				      R_00B030_SPI_SHADER_USER_DATA_PS_0 + AC_USERDATA_PUSH_CONST_DYN * 4, 2);
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
	}

	if (stages & VK_SHADER_STAGE_COMPUTE_BIT) {
		radeon_set_sh_reg_seq(cmd_buffer->cs,
				      R_00B900_COMPUTE_USER_DATA_0 + AC_USERDATA_PUSH_CONST_DYN * 4, 2);
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
	}

	cmd_buffer->push_constant_stages &= ~stages;
}

static void
radv_cmd_buffer_flush_state(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	struct radv_device *device = cmd_buffer->device;
	uint32_t ia_multi_vgt_param;
	uint32_t ls_hs_config = 0;

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
					      4096);

	if ((cmd_buffer->state.vertex_descriptors_dirty || cmd_buffer->state.vb_dirty) &&
	    cmd_buffer->state.pipeline->num_vertex_attribs) {
		unsigned vb_offset;
		void *vb_ptr;
		uint32_t i = 0;
		uint32_t num_attribs = cmd_buffer->state.pipeline->num_vertex_attribs;
		uint64_t va;

		/* allocate some descriptor state for vertex buffers */
		radv_cmd_buffer_upload_alloc(cmd_buffer, num_attribs * 16, 256,
					     &vb_offset, &vb_ptr);

		for (i = 0; i < num_attribs; i++) {
			uint32_t *desc = &((uint32_t *)vb_ptr)[i * 4];
			uint32_t offset;
			int vb = cmd_buffer->state.pipeline->va_binding[i];
			struct radv_buffer *buffer = cmd_buffer->state.vertex_bindings[vb].buffer;
			uint32_t stride = cmd_buffer->state.pipeline->binding_stride[vb];

			device->ws->cs_add_buffer(cmd_buffer->cs, buffer->bo, 8);
			va = device->ws->buffer_get_va(buffer->bo);

			offset = cmd_buffer->state.vertex_bindings[vb].offset + cmd_buffer->state.pipeline->va_offset[i];
			va += offset + buffer->offset;
			desc[0] = va;
			desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) | S_008F04_STRIDE(stride);
			if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class <= CIK && stride)
				desc[2] = (buffer->size - offset - cmd_buffer->state.pipeline->va_format_size[i]) / stride + 1;
			else
				desc[2] = buffer->size - offset;
			desc[3] = cmd_buffer->state.pipeline->va_rsrc_word3[i];
		}

		va = device->ws->buffer_get_va(cmd_buffer->upload.upload_bo);
		va += vb_offset;
		radeon_set_sh_reg_seq(cmd_buffer->cs,
				      R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_VS_VERTEX_BUFFERS * 4, 2);
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);

	}

	cmd_buffer->state.vertex_descriptors_dirty = false;
	cmd_buffer->state.vb_dirty = 0;
	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE)
		radv_emit_graphics_pipeline(cmd_buffer, pipeline);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_RENDER_TARGETS)
		radv_emit_framebuffer_state(cmd_buffer);

	if (cmd_buffer->state.dirty & (RADV_CMD_DIRTY_DYNAMIC_VIEWPORT))
		radv_emit_viewport(cmd_buffer);

	if (cmd_buffer->state.dirty & (RADV_CMD_DIRTY_DYNAMIC_SCISSOR))
		radv_emit_scissor(cmd_buffer);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE) {
		radeon_set_context_reg(cmd_buffer->cs, R_028B54_VGT_SHADER_STAGES_EN, 0);
		ia_multi_vgt_param = si_get_ia_multi_vgt_param(cmd_buffer);

		if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= CIK) {
			radeon_set_context_reg_idx(cmd_buffer->cs, R_028AA8_IA_MULTI_VGT_PARAM, 1, ia_multi_vgt_param);
			radeon_set_context_reg_idx(cmd_buffer->cs, R_028B58_VGT_LS_HS_CONFIG, 2, ls_hs_config);
			radeon_set_uconfig_reg_idx(cmd_buffer->cs, R_030908_VGT_PRIMITIVE_TYPE, 1, cmd_buffer->state.pipeline->graphics.prim);
		} else {
			radeon_set_config_reg(cmd_buffer->cs, R_008958_VGT_PRIMITIVE_TYPE, cmd_buffer->state.pipeline->graphics.prim);
			radeon_set_context_reg(cmd_buffer->cs, R_028AA8_IA_MULTI_VGT_PARAM, ia_multi_vgt_param);
			radeon_set_context_reg(cmd_buffer->cs, R_028B58_VGT_LS_HS_CONFIG, ls_hs_config);
		}
		radeon_set_context_reg(cmd_buffer->cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, cmd_buffer->state.pipeline->graphics.gs_out);
	}

	radv_cmd_buffer_flush_dynamic_state(cmd_buffer);

	radv_flush_constants(cmd_buffer, cmd_buffer->state.pipeline->layout,
			     VK_SHADER_STAGE_ALL_GRAPHICS);

	assert(cmd_buffer->cs->cdw <= cdw_max);

	si_emit_cache_flush(cmd_buffer);
}

static void radv_stage_flush(struct radv_cmd_buffer *cmd_buffer,
			     VkPipelineStageFlags src_stage_mask)
{
	if (src_stage_mask & (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
	                      VK_PIPELINE_STAGE_TRANSFER_BIT |
	                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
	                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH;
	}

	if (src_stage_mask & (VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
			      VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
			      VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
			      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
			      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			      VK_PIPELINE_STAGE_TRANSFER_BIT |
			      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
			      VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT |
			      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH;
	} else if (src_stage_mask & (VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
	                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
	                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
	                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VS_PARTIAL_FLUSH;
	}
}

static void radv_subpass_barrier(struct radv_cmd_buffer *cmd_buffer, const struct radv_subpass_barrier *barrier)
{
	radv_stage_flush(cmd_buffer, barrier->src_stage_mask);

	/* TODO: actual cache flushes */
}

static void radv_handle_subpass_image_transition(struct radv_cmd_buffer *cmd_buffer,
						 VkAttachmentReference att)
{
	unsigned idx = att.attachment;
	struct radv_image_view *view = cmd_buffer->state.framebuffer->attachments[idx].attachment;
	VkImageSubresourceRange range;
	range.aspectMask = 0;
	range.baseMipLevel = view->base_mip;
	range.levelCount = 1;
	range.baseArrayLayer = view->base_layer;
	range.layerCount = cmd_buffer->state.framebuffer->layers;

	radv_handle_image_transition(cmd_buffer,
				     view->image,
				     cmd_buffer->state.attachments[idx].current_layout,
				     att.layout, range,
				     cmd_buffer->state.attachments[idx].pending_clear_aspects);

	cmd_buffer->state.attachments[idx].current_layout = att.layout;


}

void
radv_cmd_buffer_set_subpass(struct radv_cmd_buffer *cmd_buffer,
			    const struct radv_subpass *subpass, bool transitions)
{
	if (transitions) {
		radv_subpass_barrier(cmd_buffer, &subpass->start_barrier);

		for (unsigned i = 0; i < subpass->color_count; ++i) {
			radv_handle_subpass_image_transition(cmd_buffer,
							subpass->color_attachments[i]);
		}

		for (unsigned i = 0; i < subpass->input_count; ++i) {
			radv_handle_subpass_image_transition(cmd_buffer,
							subpass->input_attachments[i]);
		}

		if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
			radv_handle_subpass_image_transition(cmd_buffer,
							subpass->depth_stencil_attachment);
		}
	}

	cmd_buffer->state.subpass = subpass;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_RENDER_TARGETS;
}

static void
radv_cmd_state_setup_attachments(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_render_pass *pass,
				 const VkRenderPassBeginInfo *info)
{
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (pass->attachment_count == 0) {
		state->attachments = NULL;
		return;
	}

	state->attachments = vk_alloc(&cmd_buffer->pool->alloc,
					pass->attachment_count *
					sizeof(state->attachments[0]),
					8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (state->attachments == NULL) {
		/* FIXME: Propagate VK_ERROR_OUT_OF_HOST_MEMORY to vkEndCommandBuffer */
		abort();
	}

	for (uint32_t i = 0; i < pass->attachment_count; ++i) {
		struct radv_render_pass_attachment *att = &pass->attachments[i];
		VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
		VkImageAspectFlags clear_aspects = 0;

		if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
			/* color attachment */
			if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
			}
		} else {
			/* depthstencil attachment */
			if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
			    att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
			}
			if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
			    att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}

		state->attachments[i].pending_clear_aspects = clear_aspects;
		if (clear_aspects && info) {
			assert(info->clearValueCount > i);
			state->attachments[i].clear_value = info->pClearValues[i];
		}

		state->attachments[i].current_layout = att->initial_layout;
	}
}

VkResult radv_AllocateCommandBuffers(
	VkDevice _device,
	const VkCommandBufferAllocateInfo *pAllocateInfo,
	VkCommandBuffer *pCommandBuffers)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_cmd_pool, pool, pAllocateInfo->commandPool);

	VkResult result = VK_SUCCESS;
	uint32_t i;

	for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
		result = radv_create_cmd_buffer(device, pool, pAllocateInfo->level,
						&pCommandBuffers[i]);
		if (result != VK_SUCCESS)
			break;
	}

	if (result != VK_SUCCESS)
		radv_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
					i, pCommandBuffers);

	return result;
}

static void
radv_cmd_buffer_destroy(struct radv_cmd_buffer *cmd_buffer)
{
	list_del(&cmd_buffer->pool_link);

	list_for_each_entry_safe(struct radv_cmd_buffer_upload, up,
				 &cmd_buffer->upload.list, list) {
		cmd_buffer->device->ws->buffer_destroy(up->upload_bo);
		list_del(&up->list);
		free(up);
	}

	if (cmd_buffer->upload.upload_bo)
		cmd_buffer->device->ws->buffer_destroy(cmd_buffer->upload.upload_bo);
	cmd_buffer->device->ws->cs_destroy(cmd_buffer->cs);
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

void radv_FreeCommandBuffers(
	VkDevice device,
	VkCommandPool commandPool,
	uint32_t commandBufferCount,
	const VkCommandBuffer *pCommandBuffers)
{
	for (uint32_t i = 0; i < commandBufferCount; i++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

		if (cmd_buffer)
			radv_cmd_buffer_destroy(cmd_buffer);
	}
}

static void  radv_reset_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{

	cmd_buffer->device->ws->cs_reset(cmd_buffer->cs);

	list_for_each_entry_safe(struct radv_cmd_buffer_upload, up,
				 &cmd_buffer->upload.list, list) {
		cmd_buffer->device->ws->buffer_destroy(up->upload_bo);
		list_del(&up->list);
		free(up);
	}

	if (cmd_buffer->upload.upload_bo)
		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs,
						      cmd_buffer->upload.upload_bo, 8);
	cmd_buffer->upload.offset = 0;

	cmd_buffer->record_fail = false;
}

VkResult radv_ResetCommandBuffer(
	VkCommandBuffer commandBuffer,
	VkCommandBufferResetFlags flags)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	radv_reset_cmd_buffer(cmd_buffer);
	return VK_SUCCESS;
}

VkResult radv_BeginCommandBuffer(
	VkCommandBuffer commandBuffer,
	const VkCommandBufferBeginInfo *pBeginInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	radv_reset_cmd_buffer(cmd_buffer);

	memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));

	/* setup initial configuration into command buffer */
	if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
		/* Flush read caches at the beginning of CS not flushed by the kernel. */
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_ICACHE |
			RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
			RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
			RADV_CMD_FLAG_INV_VMEM_L1 |
			RADV_CMD_FLAG_INV_SMEM_L1 |
			RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER |
			RADV_CMD_FLAG_INV_GLOBAL_L2;
		si_init_config(&cmd_buffer->device->instance->physicalDevice, cmd_buffer);
		radv_set_db_count_control(cmd_buffer);
		si_emit_cache_flush(cmd_buffer);
	}

	if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
		cmd_buffer->state.framebuffer = radv_framebuffer_from_handle(pBeginInfo->pInheritanceInfo->framebuffer);
		cmd_buffer->state.pass = radv_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);

		struct radv_subpass *subpass =
			&cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];

		radv_cmd_state_setup_attachments(cmd_buffer, cmd_buffer->state.pass, NULL);
		radv_cmd_buffer_set_subpass(cmd_buffer, subpass, false);
	}

	return VK_SUCCESS;
}

void radv_CmdBindVertexBuffers(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstBinding,
	uint32_t                                    bindingCount,
	const VkBuffer*                             pBuffers,
	const VkDeviceSize*                         pOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_vertex_binding *vb = cmd_buffer->state.vertex_bindings;

	/* We have to defer setting up vertex buffer since we need the buffer
	 * stride from the pipeline. */

	assert(firstBinding + bindingCount < MAX_VBS);
	for (uint32_t i = 0; i < bindingCount; i++) {
		vb[firstBinding + i].buffer = radv_buffer_from_handle(pBuffers[i]);
		vb[firstBinding + i].offset = pOffsets[i];
		cmd_buffer->state.vb_dirty |= 1 << (firstBinding + i);
	}
}

void radv_CmdBindIndexBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer buffer,
	VkDeviceSize offset,
	VkIndexType indexType)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	cmd_buffer->state.index_buffer = radv_buffer_from_handle(buffer);
	cmd_buffer->state.index_offset = offset;
	cmd_buffer->state.index_type = indexType; /* vk matches hw */
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_INDEX_BUFFER;
	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, cmd_buffer->state.index_buffer->bo, 8);
}


void radv_bind_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_descriptor_set *set,
			      unsigned idx)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;

	cmd_buffer->state.descriptors[idx] = set;

	if (!set)
		return;

	for (unsigned j = 0; j < set->layout->buffer_count; ++j)
		if (set->descriptors[j])
			ws->cs_add_buffer(cmd_buffer->cs, set->descriptors[j], 7);

	radeon_set_sh_reg_seq(cmd_buffer->cs,
				R_00B030_SPI_SHADER_USER_DATA_PS_0 + 8 * idx, 2);
	radeon_emit(cmd_buffer->cs, set->va);
	radeon_emit(cmd_buffer->cs, set->va >> 32);

	radeon_set_sh_reg_seq(cmd_buffer->cs,
				R_00B130_SPI_SHADER_USER_DATA_VS_0 + 8 * idx, 2);
	radeon_emit(cmd_buffer->cs, set->va);
	radeon_emit(cmd_buffer->cs, set->va >> 32);

	radeon_set_sh_reg_seq(cmd_buffer->cs,
				R_00B900_COMPUTE_USER_DATA_0 + 8 * idx, 2);
	radeon_emit(cmd_buffer->cs, set->va);
	radeon_emit(cmd_buffer->cs, set->va >> 32);

	if(set->bo)
		ws->cs_add_buffer(cmd_buffer->cs, set->bo, 8);
}

void radv_CmdBindDescriptorSets(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipelineLayout                            _layout,
	uint32_t                                    firstSet,
	uint32_t                                    descriptorSetCount,
	const VkDescriptorSet*                      pDescriptorSets,
	uint32_t                                    dynamicOffsetCount,
	const uint32_t*                             pDynamicOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline_layout, layout, _layout);
	unsigned dyn_idx = 0;

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
					      MAX_SETS * 4 * 6);

	for (unsigned i = 0; i < descriptorSetCount; ++i) {
		unsigned idx = i + firstSet;
		RADV_FROM_HANDLE(radv_descriptor_set, set, pDescriptorSets[i]);
		radv_bind_descriptor_set(cmd_buffer, set, idx);

		for(unsigned j = 0; j < set->layout->dynamic_offset_count; ++j, ++dyn_idx) {
			unsigned idx = j + layout->set[i].dynamic_offset_start;
			uint32_t *dst = cmd_buffer->dynamic_buffers + idx * 4;
			assert(dyn_idx < dynamicOffsetCount);

			struct radv_descriptor_range *range = set->dynamic_descriptors + j;
			uint64_t va = range->va + pDynamicOffsets[dyn_idx];
			dst[0] = va;
			dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
			dst[2] = range->size;
			dst[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			         S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			         S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			         S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			         S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
			cmd_buffer->push_constant_stages |=
			                     set->layout->dynamic_shader_stages;
		}
	}

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdPushConstants(VkCommandBuffer commandBuffer,
			   VkPipelineLayout layout,
			   VkShaderStageFlags stageFlags,
			   uint32_t offset,
			   uint32_t size,
			   const void* pValues)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	memcpy(cmd_buffer->push_constants + offset, pValues, size);
	cmd_buffer->push_constant_stages |= stageFlags;
}

VkResult radv_EndCommandBuffer(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	si_emit_cache_flush(cmd_buffer);
	if (!cmd_buffer->device->ws->cs_finalize(cmd_buffer->cs) ||
	    cmd_buffer->record_fail)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	return VK_SUCCESS;
}

static void
radv_emit_compute_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radv_shader_variant *compute_shader;
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
	uint64_t va;

	if (!pipeline || pipeline == cmd_buffer->state.emitted_compute_pipeline)
		return;

	cmd_buffer->state.emitted_compute_pipeline = pipeline;

	compute_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
	va = ws->buffer_get_va(compute_shader->bo);

	ws->cs_add_buffer(cmd_buffer->cs, compute_shader->bo, 8);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 16);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B830_COMPUTE_PGM_LO, 2);
	radeon_emit(cmd_buffer->cs, va >> 8);
	radeon_emit(cmd_buffer->cs, va >> 40);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
	radeon_emit(cmd_buffer->cs, compute_shader->rsrc1);
	radeon_emit(cmd_buffer->cs, compute_shader->rsrc2);

	/* change these once we have scratch support */
	radeon_set_sh_reg(cmd_buffer->cs, R_00B860_COMPUTE_TMPRING_SIZE,
			  S_00B860_WAVES(32) | S_00B860_WAVESIZE(0));

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[0]));
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[1]));
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[2]));

	assert(cmd_buffer->cs->cdw <= cdw_max);
}


void radv_CmdBindPipeline(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipeline                                  _pipeline)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

	switch (pipelineBindPoint) {
	case VK_PIPELINE_BIND_POINT_COMPUTE:
		cmd_buffer->state.compute_pipeline = pipeline;
		cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_COMPUTE_BIT;
		break;
	case VK_PIPELINE_BIND_POINT_GRAPHICS:
		cmd_buffer->state.pipeline = pipeline;
		cmd_buffer->state.vertex_descriptors_dirty = true;
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_PIPELINE;
		cmd_buffer->push_constant_stages |= pipeline->active_stages;

		/* Apply the dynamic state from the pipeline */
		cmd_buffer->state.dirty |= pipeline->dynamic_state_mask;
		radv_dynamic_state_copy(&cmd_buffer->state.dynamic,
					&pipeline->dynamic_state,
					pipeline->dynamic_state_mask);
		break;
	default:
		assert(!"invalid bind point");
		break;
	}
}

void radv_CmdSetViewport(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstViewport,
	uint32_t                                    viewportCount,
	const VkViewport*                           pViewports)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	const uint32_t total_count = firstViewport + viewportCount;
	if (cmd_buffer->state.dynamic.viewport.count < total_count)
		cmd_buffer->state.dynamic.viewport.count = total_count;

	memcpy(cmd_buffer->state.dynamic.viewport.viewports + firstViewport,
	       pViewports, viewportCount * sizeof(*pViewports));

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void radv_CmdSetScissor(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstScissor,
	uint32_t                                    scissorCount,
	const VkRect2D*                             pScissors)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	const uint32_t total_count = firstScissor + scissorCount;
	if (cmd_buffer->state.dynamic.scissor.count < total_count)
		cmd_buffer->state.dynamic.scissor.count = total_count;

	memcpy(cmd_buffer->state.dynamic.scissor.scissors + firstScissor,
	       pScissors, scissorCount * sizeof(*pScissors));
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void radv_CmdSetLineWidth(
	VkCommandBuffer                             commandBuffer,
	float                                       lineWidth)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	cmd_buffer->state.dynamic.line_width = lineWidth;
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
}

void radv_CmdSetDepthBias(
	VkCommandBuffer                             commandBuffer,
	float                                       depthBiasConstantFactor,
	float                                       depthBiasClamp,
	float                                       depthBiasSlopeFactor)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	cmd_buffer->state.dynamic.depth_bias.bias = depthBiasConstantFactor;
	cmd_buffer->state.dynamic.depth_bias.clamp = depthBiasClamp;
	cmd_buffer->state.dynamic.depth_bias.slope = depthBiasSlopeFactor;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
}

void radv_CmdSetBlendConstants(
	VkCommandBuffer                             commandBuffer,
	const float                                 blendConstants[4])
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	memcpy(cmd_buffer->state.dynamic.blend_constants,
	       blendConstants, sizeof(float) * 4);

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
}

void radv_CmdSetDepthBounds(
	VkCommandBuffer                             commandBuffer,
	float                                       minDepthBounds,
	float                                       maxDepthBounds)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	cmd_buffer->state.dynamic.depth_bounds.min = minDepthBounds;
	cmd_buffer->state.dynamic.depth_bounds.max = maxDepthBounds;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
}

void radv_CmdSetStencilCompareMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    compareMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_compare_mask.front = compareMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_compare_mask.back = compareMask;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void radv_CmdSetStencilWriteMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    writeMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_write_mask.front = writeMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_write_mask.back = writeMask;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void radv_CmdSetStencilReference(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    reference)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_reference.front = reference;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_reference.back = reference;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
}


void radv_CmdExecuteCommands(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                      pCmdBuffers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, primary, commandBuffer);

	for (uint32_t i = 0; i < commandBufferCount; i++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, secondary, pCmdBuffers[i]);

		primary->device->ws->cs_execute_secondary(primary->cs, secondary->cs);
	}

	/* if we execute secondary we need to re-emit out pipelines */
	if (commandBufferCount) {
		primary->state.emitted_pipeline = NULL;
		primary->state.dirty |= RADV_CMD_DIRTY_PIPELINE;
		primary->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_ALL;
	}
}

VkResult radv_CreateCommandPool(
	VkDevice                                    _device,
	const VkCommandPoolCreateInfo*              pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkCommandPool*                              pCmdPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_cmd_pool *pool;

	pool = vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pool == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	if (pAllocator)
		pool->alloc = *pAllocator;
	else
		pool->alloc = device->alloc;

	list_inithead(&pool->cmd_buffers);

	*pCmdPool = radv_cmd_pool_to_handle(pool);

	return VK_SUCCESS;

}

void radv_DestroyCommandPool(
	VkDevice                                    _device,
	VkCommandPool                               commandPool,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);

	if (!pool)
		return;

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->cmd_buffers, pool_link) {
		radv_cmd_buffer_destroy(cmd_buffer);
	}

	vk_free2(&device->alloc, pAllocator, pool);
}

VkResult radv_ResetCommandPool(
	VkDevice                                    device,
	VkCommandPool                               commandPool,
	VkCommandPoolResetFlags                     flags)
{
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);

	list_for_each_entry(struct radv_cmd_buffer, cmd_buffer,
			    &pool->cmd_buffers, pool_link) {
		radv_reset_cmd_buffer(cmd_buffer);
	}

	return VK_SUCCESS;
}

void radv_CmdBeginRenderPass(
	VkCommandBuffer                             commandBuffer,
	const VkRenderPassBeginInfo*                pRenderPassBegin,
	VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_render_pass, pass, pRenderPassBegin->renderPass);
	RADV_FROM_HANDLE(radv_framebuffer, framebuffer, pRenderPassBegin->framebuffer);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
					      2048);

	cmd_buffer->state.framebuffer = framebuffer;
	cmd_buffer->state.pass = pass;
	cmd_buffer->state.render_area = pRenderPassBegin->renderArea;
	radv_cmd_state_setup_attachments(cmd_buffer, pass, pRenderPassBegin);

	si_emit_cache_flush(cmd_buffer);

	radv_cmd_buffer_set_subpass(cmd_buffer, pass->subpasses, true);
	assert(cmd_buffer->cs->cdw <= cdw_max);

	radv_cmd_buffer_clear_subpass(cmd_buffer);
}

void radv_CmdNextSubpass(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	si_emit_cache_flush(cmd_buffer);
	radv_cmd_buffer_resolve_subpass(cmd_buffer);

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
					      2048);

	radv_cmd_buffer_set_subpass(cmd_buffer, cmd_buffer->state.subpass + 1, true);
	radv_cmd_buffer_clear_subpass(cmd_buffer);
}

void radv_CmdDraw(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    vertexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstVertex,
	uint32_t                                    firstInstance)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	radv_cmd_buffer_flush_state(cmd_buffer);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 9);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_VS_BASE_VERTEX * 4, 2);
	radeon_emit(cmd_buffer->cs, firstVertex);
	radeon_emit(cmd_buffer->cs, firstInstance);
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_NUM_INSTANCES, 0, 0));
	radeon_emit(cmd_buffer->cs, instanceCount);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, 0));
	radeon_emit(cmd_buffer->cs, vertexCount);
	radeon_emit(cmd_buffer->cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX |
		    S_0287F0_USE_OPAQUE(0));

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void radv_emit_primitive_reset_index(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t primitive_reset_index = cmd_buffer->state.last_primitive_reset_index ? 0xffffffffu : 0xffffu;

	if (cmd_buffer->state.pipeline->graphics.prim_restart_enable &&
	    primitive_reset_index != cmd_buffer->state.last_primitive_reset_index) {
		cmd_buffer->state.last_primitive_reset_index = primitive_reset_index;
		radeon_set_context_reg(cmd_buffer->cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
				       primitive_reset_index);
	}
}

void radv_CmdDrawIndexed(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    indexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstIndex,
	int32_t                                     vertexOffset,
	uint32_t                                    firstInstance)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	int index_size = cmd_buffer->state.index_type ? 4 : 2;
	uint32_t index_max_size = (cmd_buffer->state.index_buffer->size - cmd_buffer->state.index_offset) / index_size;
	uint64_t index_va;

	radv_cmd_buffer_flush_state(cmd_buffer);
	radv_emit_primitive_reset_index(cmd_buffer);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 14);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
	radeon_emit(cmd_buffer->cs, cmd_buffer->state.index_type);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_VS_BASE_VERTEX * 4, 2);
	radeon_emit(cmd_buffer->cs, vertexOffset);
	radeon_emit(cmd_buffer->cs, firstInstance);
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_NUM_INSTANCES, 0, 0));
	radeon_emit(cmd_buffer->cs, instanceCount);

	index_va = cmd_buffer->device->ws->buffer_get_va(cmd_buffer->state.index_buffer->bo);
	index_va += firstIndex * index_size + cmd_buffer->state.index_buffer->offset + cmd_buffer->state.index_offset;
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_2, 4, false));
	radeon_emit(cmd_buffer->cs, index_max_size);
	radeon_emit(cmd_buffer->cs, index_va);
	radeon_emit(cmd_buffer->cs, (index_va >> 32UL) & 0xFF);
	radeon_emit(cmd_buffer->cs, indexCount);
	radeon_emit(cmd_buffer->cs, V_0287F0_DI_SRC_SEL_DMA);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void
radv_emit_indirect_draw(struct radv_cmd_buffer *cmd_buffer,
			VkBuffer _buffer,
			VkDeviceSize offset,
			uint32_t draw_count,
			uint32_t stride,
			bool indexed)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	unsigned di_src_sel = indexed ? V_0287F0_DI_SRC_SEL_DMA
					    : V_0287F0_DI_SRC_SEL_AUTO_INDEX;
	uint64_t indirect_va = cmd_buffer->device->ws->buffer_get_va(buffer->bo);
	indirect_va += offset + buffer->offset;

	if (!draw_count)
		return;

	cmd_buffer->device->ws->cs_add_buffer(cs, buffer->bo, 8);

	radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
	radeon_emit(cs, 1);
	radeon_emit(cs, indirect_va);
	radeon_emit(cs, indirect_va >> 32);

	radeon_emit(cs, PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI :
				       PKT3_DRAW_INDIRECT_MULTI,
			     8, false));
	radeon_emit(cs, 0);
	radeon_emit(cs, ((R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_VS_BASE_VERTEX * 4) - SI_SH_REG_OFFSET) >> 2);
	radeon_emit(cs, ((R_00B130_SPI_SHADER_USER_DATA_VS_0 + AC_USERDATA_VS_START_INSTANCE * 4) - SI_SH_REG_OFFSET) >> 2);
	radeon_emit(cs, 0); /* draw_index */
	radeon_emit(cs, draw_count); /* count */
	radeon_emit(cs, 0); /* count_addr -- disabled */
	radeon_emit(cs, 0);
	radeon_emit(cs, stride); /* stride */
	radeon_emit(cs, di_src_sel);
}

void radv_CmdDrawIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	radv_cmd_buffer_flush_state(cmd_buffer);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 14);

	radv_emit_indirect_draw(cmd_buffer, _buffer, offset, drawCount, stride, false);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdDrawIndexedIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	int index_size = cmd_buffer->state.index_type ? 4 : 2;
	uint32_t index_max_size = (cmd_buffer->state.index_buffer->size - cmd_buffer->state.index_offset) / index_size;
	uint64_t index_va;
	radv_cmd_buffer_flush_state(cmd_buffer);
	radv_emit_primitive_reset_index(cmd_buffer);

	index_va = cmd_buffer->device->ws->buffer_get_va(cmd_buffer->state.index_buffer->bo);
	index_va += cmd_buffer->state.index_buffer->offset + cmd_buffer->state.index_offset;

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 21);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
	radeon_emit(cmd_buffer->cs, cmd_buffer->state.index_type);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_INDEX_BASE, 1, 0));
	radeon_emit(cmd_buffer->cs, index_va);
	radeon_emit(cmd_buffer->cs, index_va >> 32);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
	radeon_emit(cmd_buffer->cs, index_max_size);

	radv_emit_indirect_draw(cmd_buffer, _buffer, offset, drawCount, stride, true);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdDispatch(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_emit_compute_pipeline(cmd_buffer);
	radv_flush_constants(cmd_buffer, cmd_buffer->state.compute_pipeline->layout,
			     VK_SHADER_STAGE_COMPUTE_BIT);
	si_emit_cache_flush(cmd_buffer);
	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 10);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B900_COMPUTE_USER_DATA_0 + AC_USERDATA_CS_GRID_SIZE * 4, 3);
	radeon_emit(cmd_buffer->cs, x);
	radeon_emit(cmd_buffer->cs, y);
	radeon_emit(cmd_buffer->cs, z);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DISPATCH_DIRECT, 3, 0) |
		    PKT3_SHADER_TYPE_S(1));
	radeon_emit(cmd_buffer->cs, x);
	radeon_emit(cmd_buffer->cs, y);
	radeon_emit(cmd_buffer->cs, z);
	radeon_emit(cmd_buffer->cs, 1);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdDispatchIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(buffer->bo);
	va += buffer->offset + offset;

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, buffer->bo, 8);

	radv_emit_compute_pipeline(cmd_buffer);
	radv_flush_constants(cmd_buffer, cmd_buffer->state.compute_pipeline->layout,
			     VK_SHADER_STAGE_COMPUTE_BIT);
	si_emit_cache_flush(cmd_buffer);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 25);

	for (unsigned i = 0; i < 3; ++i) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cmd_buffer->cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
					    COPY_DATA_DST_SEL(COPY_DATA_REG));
		radeon_emit(cmd_buffer->cs, (va +  4 * i));
		radeon_emit(cmd_buffer->cs, (va + 4 * i) >> 32);
		radeon_emit(cmd_buffer->cs, ((R_00B900_COMPUTE_USER_DATA_0 + AC_USERDATA_CS_GRID_SIZE * 4) >> 2) + i);
		radeon_emit(cmd_buffer->cs, 0);
	}

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_SET_BASE, 2, 0) |
				    PKT3_SHADER_TYPE_S(1));
	radeon_emit(cmd_buffer->cs, 1);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DISPATCH_INDIRECT, 1, 0) |
				    PKT3_SHADER_TYPE_S(1));
	radeon_emit(cmd_buffer->cs, 0);
	radeon_emit(cmd_buffer->cs, 1);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_unaligned_dispatch(
	struct radv_cmd_buffer                      *cmd_buffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
	struct radv_shader_variant *compute_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
	uint32_t blocks[3], remainder[3];

	blocks[0] = round_up_u32(x, compute_shader->info.cs.block_size[0]);
	blocks[1] = round_up_u32(y, compute_shader->info.cs.block_size[1]);
	blocks[2] = round_up_u32(z, compute_shader->info.cs.block_size[2]);

	/* If aligned, these should be an entire block size, not 0 */
	remainder[0] = x + compute_shader->info.cs.block_size[0] - align_u32_npot(x, compute_shader->info.cs.block_size[0]);
	remainder[1] = y + compute_shader->info.cs.block_size[1] - align_u32_npot(y, compute_shader->info.cs.block_size[1]);
	remainder[2] = z + compute_shader->info.cs.block_size[2] - align_u32_npot(z, compute_shader->info.cs.block_size[2]);

	radv_emit_compute_pipeline(cmd_buffer);
	radv_flush_constants(cmd_buffer, cmd_buffer->state.compute_pipeline->layout,
			     VK_SHADER_STAGE_COMPUTE_BIT);
	si_emit_cache_flush(cmd_buffer);
	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 15);

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[0]) |
		    S_00B81C_NUM_THREAD_PARTIAL(remainder[0]));
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[1]) |
		    S_00B81C_NUM_THREAD_PARTIAL(remainder[1]));
	radeon_emit(cmd_buffer->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[2]) |
		    S_00B81C_NUM_THREAD_PARTIAL(remainder[2]));

	radeon_set_sh_reg_seq(cmd_buffer->cs, R_00B900_COMPUTE_USER_DATA_0 + AC_USERDATA_CS_GRID_SIZE * 4, 3);
	radeon_emit(cmd_buffer->cs, blocks[0]);
	radeon_emit(cmd_buffer->cs, blocks[1]);
	radeon_emit(cmd_buffer->cs, blocks[2]);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DISPATCH_DIRECT, 3, 0) |
		    PKT3_SHADER_TYPE_S(1));
	radeon_emit(cmd_buffer->cs, blocks[0]);
	radeon_emit(cmd_buffer->cs, blocks[1]);
	radeon_emit(cmd_buffer->cs, blocks[2]);
	radeon_emit(cmd_buffer->cs, S_00B800_COMPUTE_SHADER_EN(1) |
	                            S_00B800_PARTIAL_TG_EN(1));

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdEndRenderPass(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier);

	si_emit_cache_flush(cmd_buffer);
	radv_cmd_buffer_resolve_subpass(cmd_buffer);

	for (unsigned i = 0; i < cmd_buffer->state.framebuffer->attachment_count; ++i) {
		VkImageLayout layout = cmd_buffer->state.pass->attachments[i].final_layout;
		radv_handle_subpass_image_transition(cmd_buffer,
		                      (VkAttachmentReference){i, layout});
	}

	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);

	cmd_buffer->state.pass = NULL;
	cmd_buffer->state.subpass = NULL;
	cmd_buffer->state.attachments = NULL;
	cmd_buffer->state.framebuffer = NULL;
}


static void radv_initialize_htile(struct radv_cmd_buffer *cmd_buffer,
				  struct radv_image *image)
{

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

	radv_fill_buffer(cmd_buffer, image->bo, image->offset + image->htile.offset,
			 image->htile.size, 0xffffffff);

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META |
	                                RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VMEM_L1 |
	                                RADV_CMD_FLAG_INV_GLOBAL_L2;
}

static void radv_handle_depth_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       VkImageLayout dst_layout,
					       VkImageSubresourceRange range,
					       VkImageAspectFlags pending_clears)
{
	if (dst_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
	    (pending_clears & vk_format_aspects(image->vk_format)) == vk_format_aspects(image->vk_format) &&
	    cmd_buffer->state.render_area.offset.x == 0 && cmd_buffer->state.render_area.offset.y == 0 &&
	    cmd_buffer->state.render_area.extent.width == image->extent.width &&
	    cmd_buffer->state.render_area.extent.height == image->extent.height) {
		/* The clear will initialize htile. */
		return;
	} else if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
	           radv_layout_has_htile(image, dst_layout)) {
		/* TODO: merge with the clear if applicable */
		radv_initialize_htile(cmd_buffer, image);
	} else if (!radv_layout_has_htile(image, src_layout) &&
	           radv_layout_has_htile(image, dst_layout)) {
		radv_initialize_htile(cmd_buffer, image);
	} else if ((radv_layout_has_htile(image, src_layout) &&
	            !radv_layout_has_htile(image, dst_layout)) ||
	           (radv_layout_is_htile_compressed(image, src_layout) &&
	            !radv_layout_is_htile_compressed(image, dst_layout))) {

		range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;

		radv_decompress_depth_image_inplace(cmd_buffer, image, &range);
	}
}

void radv_initialise_cmask(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image, uint32_t value)
{
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	radv_fill_buffer(cmd_buffer, image->bo, image->offset + image->cmask.offset,
			 image->cmask.size, value);

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
	                                RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VMEM_L1 |
	                                RADV_CMD_FLAG_INV_GLOBAL_L2;
}

static void radv_handle_cmask_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       VkImageLayout dst_layout,
					       VkImageSubresourceRange range,
					       VkImageAspectFlags pending_clears)
{
	if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		if (image->fmask.size)
			radv_initialise_cmask(cmd_buffer, image, 0xccccccccu);
		else
			radv_initialise_cmask(cmd_buffer, image, 0xffffffffu);
	} else if (radv_layout_has_cmask(image, src_layout) &&
		   !radv_layout_has_cmask(image, dst_layout)) {

		if (!cmd_buffer->device->allow_fast_clears)
			return;
		radv_fast_clear_flush_image_inplace(cmd_buffer, image);
	}
}

void radv_initialize_dcc(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image, uint32_t value)
{

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	radv_fill_buffer(cmd_buffer, image->bo, image->offset + image->dcc_offset,
			 image->surface.dcc_size, value);

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
	                                RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VMEM_L1 |
	                                RADV_CMD_FLAG_INV_GLOBAL_L2;
}

static void radv_handle_dcc_image_transition(struct radv_cmd_buffer *cmd_buffer,
					     struct radv_image *image,
					     VkImageLayout src_layout,
					     VkImageLayout dst_layout,
					     VkImageSubresourceRange range,
					     VkImageAspectFlags pending_clears)
{
	if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		radv_initialize_dcc(cmd_buffer, image, 0x20202020u);
	} else if(src_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
		  dst_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		radv_fast_clear_flush_image_inplace(cmd_buffer, image);
	}
}

static void radv_handle_image_transition(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageLayout src_layout,
					 VkImageLayout dst_layout,
					 VkImageSubresourceRange range,
					 VkImageAspectFlags pending_clears)
{
	if (image->htile.size)
		radv_handle_depth_image_transition(cmd_buffer, image, src_layout,
						   dst_layout, range, pending_clears);

	if (image->cmask.size)
		radv_handle_cmask_image_transition(cmd_buffer, image, src_layout,
						   dst_layout, range, pending_clears);

	if (image->surface.dcc_size)
		radv_handle_dcc_image_transition(cmd_buffer, image, src_layout,
						 dst_layout, range, pending_clears);
}

void radv_CmdPipelineBarrier(
	VkCommandBuffer                             commandBuffer,
	VkPipelineStageFlags                        srcStageMask,
	VkPipelineStageFlags                        destStageMask,
	VkBool32                                    byRegion,
	uint32_t                                    memoryBarrierCount,
	const VkMemoryBarrier*                      pMemoryBarriers,
	uint32_t                                    bufferMemoryBarrierCount,
	const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
	uint32_t                                    imageMemoryBarrierCount,
	const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	VkAccessFlags src_flags = 0;
	VkAccessFlags dst_flags = 0;
	uint32_t b;
	for (uint32_t i = 0; i < memoryBarrierCount; i++) {
		src_flags |= pMemoryBarriers[i].srcAccessMask;
		dst_flags |= pMemoryBarriers[i].dstAccessMask;
	}

	for (uint32_t i = 0; i < bufferMemoryBarrierCount; i++) {
		src_flags |= pBufferMemoryBarriers[i].srcAccessMask;
		dst_flags |= pBufferMemoryBarriers[i].dstAccessMask;
	}

	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);
		src_flags |= pImageMemoryBarriers[i].srcAccessMask;
		dst_flags |= pImageMemoryBarriers[i].dstAccessMask;

		radv_handle_image_transition(cmd_buffer, image,
					     pImageMemoryBarriers[i].oldLayout,
					     pImageMemoryBarriers[i].newLayout,
					     pImageMemoryBarriers[i].subresourceRange,
					     0);
	}

	enum radv_cmd_flush_bits flush_bits = 0;

	for_each_bit(b, src_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_SHADER_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_GLOBAL_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
			break;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB;
			break;
		case VK_ACCESS_TRANSFER_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
			break;
		default:
			break;
		}
	}

	for_each_bit(b, dst_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
		case VK_ACCESS_INDEX_READ_BIT:
		case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
		case VK_ACCESS_UNIFORM_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VMEM_L1;
			break;
		case VK_ACCESS_SHADER_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_GLOBAL_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
		case VK_ACCESS_TRANSFER_READ_BIT:
			flush_bits |= RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER | RADV_CMD_FLAG_INV_GLOBAL_L2;
		default:
			break;
		}
	}

	flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
		RADV_CMD_FLAG_PS_PARTIAL_FLUSH;

	cmd_buffer->state.flush_bits |= flush_bits;
}


static void write_event(struct radv_cmd_buffer *cmd_buffer,
			struct radv_event *event,
			VkPipelineStageFlags stageMask,
			unsigned value)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(event->bo);

	cmd_buffer->device->ws->cs_add_buffer(cs, event->bo, 8);

	unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 12);

	/* TODO: this is overkill. Probably should figure something out from
	 * the stage mask. */

	if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class == CIK) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_BOTTOM_OF_PIPE_TS) |
				EVENT_INDEX(5));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) | EOP_DATA_SEL(1));
		radeon_emit(cs, 2);
		radeon_emit(cs, 0);
	}

	radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
	radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_BOTTOM_OF_PIPE_TS) |
			EVENT_INDEX(5));
	radeon_emit(cs, va);
	radeon_emit(cs, (va >> 32) | EOP_DATA_SEL(1));
	radeon_emit(cs, value);
	radeon_emit(cs, 0);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdSetEvent(VkCommandBuffer commandBuffer,
		      VkEvent _event,
		      VkPipelineStageFlags stageMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_event, event, _event);

	write_event(cmd_buffer, event, stageMask, 1);
}

void radv_CmdResetEvent(VkCommandBuffer commandBuffer,
			VkEvent _event,
			VkPipelineStageFlags stageMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_event, event, _event);

	write_event(cmd_buffer, event, stageMask, 0);
}

void radv_CmdWaitEvents(VkCommandBuffer commandBuffer,
			uint32_t eventCount,
			const VkEvent* pEvents,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			uint32_t memoryBarrierCount,
			const VkMemoryBarrier* pMemoryBarriers,
			uint32_t bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier* pBufferMemoryBarriers,
			uint32_t imageMemoryBarrierCount,
			const VkImageMemoryBarrier* pImageMemoryBarriers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;

	for (unsigned i = 0; i < eventCount; ++i) {
		RADV_FROM_HANDLE(radv_event, event, pEvents[i]);
		uint64_t va = cmd_buffer->device->ws->buffer_get_va(event->bo);

		cmd_buffer->device->ws->cs_add_buffer(cs, event->bo, 8);

		unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 7);

		radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
		radeon_emit(cs, WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_MEM_SPACE(1));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, 1); /* reference value */
		radeon_emit(cs, 0xffffffff); /* mask */
		radeon_emit(cs, 4); /* poll interval */

		assert(cmd_buffer->cs->cdw <= cdw_max);
	}


	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);

		radv_handle_image_transition(cmd_buffer, image,
					     pImageMemoryBarriers[i].oldLayout,
					     pImageMemoryBarriers[i].newLayout,
					     pImageMemoryBarriers[i].subresourceRange,
					     0);
	}

	/* TODO: figure out how to do memory barriers without waiting */
	cmd_buffer->state.flush_bits |= RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER |
					RADV_CMD_FLAG_INV_GLOBAL_L2 |
					RADV_CMD_FLAG_INV_VMEM_L1 |
					RADV_CMD_FLAG_INV_SMEM_L1;
}
