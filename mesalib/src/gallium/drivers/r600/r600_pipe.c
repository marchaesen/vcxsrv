/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * SPDX-License-Identifier: MIT
 */

#include "r600_pipe.h"
#include "r600_public.h"
#include "r600_isa.h"
#include "r600_sfn.h"
#include "evergreen_compute.h"
#include "r600d.h"

#include <errno.h>
#include "pipe/p_shader_tokens.h"
#include "util/u_debug.h"
#include "util/u_endian.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_simple_shaders.h"
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"
#include "radeon_video.h"
#include "radeon_uvd.h"
#include "util/os_time.h"

static const struct debug_named_value r600_debug_options[] = {
	/* features */
	{ "nocpdma", DBG_NO_CP_DMA, "Disable CP DMA" },

        DEBUG_NAMED_VALUE_END /* must be last */
};

/*
 * pipe_context
 */

static void r600_destroy_context(struct pipe_context *context)
{
	struct r600_context *rctx = (struct r600_context *)context;
	unsigned sh, i;

	r600_isa_destroy(rctx->isa);

	for (sh = 0; sh < (rctx->b.gfx_level < EVERGREEN ? R600_NUM_HW_STAGES : EG_NUM_HW_STAGES); sh++) {
		r600_resource_reference(&rctx->scratch_buffers[sh].buffer, NULL);
	}
	r600_resource_reference(&rctx->dummy_cmask, NULL);
	r600_resource_reference(&rctx->dummy_fmask, NULL);

	if (rctx->append_fence)
		pipe_resource_reference((struct pipe_resource**)&rctx->append_fence, NULL);
	for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
		rctx->b.b.set_constant_buffer(&rctx->b.b, sh, R600_BUFFER_INFO_CONST_BUFFER, false, NULL);
		free(rctx->driver_consts[sh].constants);
	}

	if (rctx->fixed_func_tcs_shader)
		rctx->b.b.delete_tcs_state(&rctx->b.b, rctx->fixed_func_tcs_shader);

	if (rctx->dummy_pixel_shader) {
		rctx->b.b.delete_fs_state(&rctx->b.b, rctx->dummy_pixel_shader);
	}
	if (rctx->custom_dsa_flush) {
		rctx->b.b.delete_depth_stencil_alpha_state(&rctx->b.b, rctx->custom_dsa_flush);
	}
	if (rctx->custom_blend_resolve) {
		rctx->b.b.delete_blend_state(&rctx->b.b, rctx->custom_blend_resolve);
	}
	if (rctx->custom_blend_decompress) {
		rctx->b.b.delete_blend_state(&rctx->b.b, rctx->custom_blend_decompress);
	}
	if (rctx->custom_blend_fastclear) {
		rctx->b.b.delete_blend_state(&rctx->b.b, rctx->custom_blend_fastclear);
	}
	util_unreference_framebuffer_state(&rctx->framebuffer.state);

	if (rctx->gs_rings.gsvs_ring.buffer)
		pipe_resource_reference(&rctx->gs_rings.gsvs_ring.buffer, NULL);

	if (rctx->gs_rings.esgs_ring.buffer)
		pipe_resource_reference(&rctx->gs_rings.esgs_ring.buffer, NULL);

	for (sh = 0; sh < PIPE_SHADER_TYPES; ++sh)
		for (i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; ++i)
			rctx->b.b.set_constant_buffer(context, sh, i, false, NULL);

	if (rctx->blitter) {
		util_blitter_destroy(rctx->blitter);
	}
	u_suballocator_destroy(&rctx->allocator_fetch_shader);

	r600_release_command_buffer(&rctx->start_cs_cmd);

	FREE(rctx->start_compute_cs_cmd.buf);

	r600_common_context_cleanup(&rctx->b);

	r600_resource_reference(&rctx->trace_buf, NULL);
	r600_resource_reference(&rctx->last_trace_buf, NULL);
	radeon_clear_saved_cs(&rctx->last_gfx);

	switch (rctx->b.gfx_level) {
	case EVERGREEN:
	case CAYMAN:
		for (i = 0; i < EG_MAX_ATOMIC_BUFFERS; ++i)
			pipe_resource_reference(&rctx->atomic_buffer_state.buffer[i].buffer, NULL);
		break;
	default:
		break;
	}

	FREE(rctx);
}

static struct pipe_context *r600_create_context(struct pipe_screen *screen,
                                                void *priv, unsigned flags)
{
	struct r600_context *rctx = CALLOC_STRUCT(r600_context);
	struct r600_screen* rscreen = (struct r600_screen *)screen;
	struct radeon_winsys *ws = rscreen->b.ws;

	if (!rctx)
		return NULL;

	rctx->b.b.screen = screen;
	assert(!priv);
	rctx->b.b.priv = NULL; /* for threaded_context_unwrap_sync */
	rctx->b.b.destroy = r600_destroy_context;
	rctx->b.set_atom_dirty = (void *)r600_set_atom_dirty;

	if (!r600_common_context_init(&rctx->b, &rscreen->b, flags))
		goto fail;

	rctx->screen = rscreen;
	list_inithead(&rctx->texture_buffers);

	r600_init_blit_functions(rctx);

	if (rscreen->b.info.ip[AMD_IP_UVD].num_queues) {
		rctx->b.b.create_video_codec = r600_uvd_create_decoder;
		rctx->b.b.create_video_buffer = r600_video_buffer_create;
	} else {
		rctx->b.b.create_video_codec = vl_create_decoder;
		rctx->b.b.create_video_buffer = vl_video_buffer_create;
	}

	if (getenv("R600_TRACE"))
		rctx->is_debug = true;
	r600_init_common_state_functions(rctx);

	switch (rctx->b.gfx_level) {
	case R600:
	case R700:
		r600_init_state_functions(rctx);
		r600_init_atom_start_cs(rctx);
		rctx->custom_dsa_flush = r600_create_db_flush_dsa(rctx);
		rctx->custom_blend_resolve = rctx->b.gfx_level == R700 ? r700_create_resolve_blend(rctx)
								      : r600_create_resolve_blend(rctx);
		rctx->custom_blend_decompress = r600_create_decompress_blend(rctx);
		rctx->has_vertex_cache = !(rctx->b.family == CHIP_RV610 ||
					   rctx->b.family == CHIP_RV620 ||
					   rctx->b.family == CHIP_RS780 ||
					   rctx->b.family == CHIP_RS880 ||
					   rctx->b.family == CHIP_RV710);
		break;
	case EVERGREEN:
	case CAYMAN:
		evergreen_init_state_functions(rctx);
		evergreen_init_atom_start_cs(rctx);
		evergreen_init_atom_start_compute_cs(rctx);
		rctx->custom_dsa_flush = evergreen_create_db_flush_dsa(rctx);
		rctx->custom_blend_resolve = evergreen_create_resolve_blend(rctx);
		rctx->custom_blend_decompress = evergreen_create_decompress_blend(rctx);
		rctx->custom_blend_fastclear = evergreen_create_fastclear_blend(rctx);
		rctx->has_vertex_cache = !(rctx->b.family == CHIP_CEDAR ||
					   rctx->b.family == CHIP_PALM ||
					   rctx->b.family == CHIP_SUMO ||
					   rctx->b.family == CHIP_SUMO2 ||
					   rctx->b.family == CHIP_CAICOS ||
					   rctx->b.family == CHIP_CAYMAN ||
					   rctx->b.family == CHIP_ARUBA);

		rctx->append_fence = pipe_buffer_create(rctx->b.b.screen, PIPE_BIND_CUSTOM,
							 PIPE_USAGE_DEFAULT, 32);
		break;
	default:
		R600_ERR("Unsupported gfx level %d.\n", rctx->b.gfx_level);
		goto fail;
	}

	ws->cs_create(&rctx->b.gfx.cs, rctx->b.ctx, AMD_IP_GFX,
                      r600_context_gfx_flush, rctx);
	rctx->b.gfx.flush = r600_context_gfx_flush;

	u_suballocator_init(&rctx->allocator_fetch_shader, &rctx->b.b, 64 * 1024,
                            0, PIPE_USAGE_DEFAULT, 0, false);

	rctx->isa = calloc(1, sizeof(struct r600_isa));
	if (!rctx->isa || r600_isa_init(rctx->b.gfx_level, rctx->isa))
		goto fail;

	if (rscreen->b.debug_flags & DBG_FORCE_DMA)
		rctx->b.b.resource_copy_region = rctx->b.dma_copy;

	rctx->blitter = util_blitter_create(&rctx->b.b);
	if (rctx->blitter == NULL)
		goto fail;
	util_blitter_set_texture_multisample(rctx->blitter, rscreen->has_msaa);
	rctx->blitter->draw_rectangle = r600_draw_rectangle;

	r600_begin_new_cs(rctx);

	rctx->dummy_pixel_shader =
		util_make_fragment_cloneinput_shader(&rctx->b.b, 0,
						     TGSI_SEMANTIC_GENERIC,
						     TGSI_INTERPOLATE_CONSTANT);
	rctx->b.b.bind_fs_state(&rctx->b.b, rctx->dummy_pixel_shader);

	return &rctx->b.b;

fail:
	r600_destroy_context(&rctx->b.b);
	return NULL;
}

/*
 * pipe_screen
 */

static void r600_init_shader_caps(struct r600_screen *rscreen)
{
	for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
		struct pipe_shader_caps *caps =
			(struct pipe_shader_caps *)&rscreen->b.b.shader_caps[i];

		switch (i) {
		case PIPE_SHADER_TESS_CTRL:
		case PIPE_SHADER_TESS_EVAL:
		case PIPE_SHADER_COMPUTE:
			if (rscreen->b.family < CHIP_CEDAR)
				continue;
			break;
		default:
			break;
		}

		caps->max_instructions =
		caps->max_alu_instructions =
		caps->max_tex_instructions =
		caps->max_tex_indirections = 16384;
		caps->max_control_flow_depth = 32;
		caps->max_inputs = i == PIPE_SHADER_VERTEX ? 16 : 32;
		caps->max_outputs = i == PIPE_SHADER_FRAGMENT ? 8 : 32;
		caps->max_temps = 256; /* Max native temporaries. */

		caps->max_const_buffer0_size = i == PIPE_SHADER_COMPUTE ?
			MIN2(rscreen->b.b.compute_caps.max_mem_alloc_size, INT_MAX) :
			R600_MAX_CONST_BUFFER_SIZE;

		caps->max_const_buffers = R600_MAX_USER_CONST_BUFFERS;
		caps->cont_supported = true;
		caps->tgsi_sqrt_supported = true;
		caps->indirect_temp_addr = true;
		caps->indirect_const_addr = true;
		caps->integers = true;
		caps->tgsi_any_inout_decl_range = true;
		caps->max_texture_samplers =
		caps->max_sampler_views = 16;

		caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
		if (i == PIPE_SHADER_COMPUTE)
			caps->supported_irs |= 1 << PIPE_SHADER_IR_NATIVE;

		caps->max_shader_buffers =
		caps->max_shader_images =
			rscreen->b.family >= CHIP_CEDAR &&
			(i == PIPE_SHADER_FRAGMENT || i == PIPE_SHADER_COMPUTE) ? 8 : 0;

		caps->max_hw_atomic_counters =
			rscreen->b.family >= CHIP_CEDAR && rscreen->has_atomics ? 8 : 0;

		/* having to allocate the atomics out amongst shaders stages is messy,
		   so give compute 8 buffers and all the others one */
		caps->max_hw_atomic_counter_buffers =
			rscreen->b.family >= CHIP_CEDAR && rscreen->has_atomics ?
			EG_MAX_ATOMIC_BUFFERS : 0;
	}
}

static void r600_init_compute_caps(struct r600_screen *screen)
{
	struct r600_common_screen *rscreen = &screen->b;

	struct pipe_compute_caps *caps =
		(struct pipe_compute_caps *)&rscreen->b.compute_caps;

	snprintf(caps->ir_target, sizeof(caps->ir_target), "%s-r600--",
		 r600_get_llvm_processor_name(rscreen->family));

	caps->grid_dimension = 3;

	caps->max_grid_size[0] =
	caps->max_grid_size[1] =
	caps->max_grid_size[2] = 65535;

	caps->max_block_size[0] =
	caps->max_block_size[1] =
	caps->max_block_size[2] = rscreen->gfx_level >= EVERGREEN ? 1024 : 256;

	caps->max_block_size_clover[0] =
	caps->max_block_size_clover[1] =
	caps->max_block_size_clover[2] = 256;

	caps->max_threads_per_block = rscreen->gfx_level >= EVERGREEN ? 1024 : 256;
	caps->max_threads_per_block_clover = 256;
	caps->address_bits = 32;
	caps->max_mem_alloc_size = (rscreen->info.max_heap_size_kb / 4) * 1024ull;

	/* In OpenCL, the MAX_MEM_ALLOC_SIZE must be at least
	 * 1/4 of the MAX_GLOBAL_SIZE.  Since the
	 * MAX_MEM_ALLOC_SIZE is fixed for older kernels,
	 * make sure we never report more than
	 * 4 * MAX_MEM_ALLOC_SIZE.
	 */
	caps->max_global_size = MIN2(4 * caps->max_mem_alloc_size,
				     rscreen->info.max_heap_size_kb * 1024ull);

	/* Value reported by the closed source driver. */
	caps->max_local_size = 32768;
	caps->max_input_size = 1024;
	caps->max_clock_frequency = rscreen->info.max_gpu_freq_mhz;
	caps->max_compute_units = rscreen->info.num_cu;
	caps->subgroup_sizes = r600_wavefront_size(rscreen->family);
}

static void r600_init_screen_caps(struct r600_screen *rscreen)
{
	struct pipe_caps *caps = (struct pipe_caps *)&rscreen->b.b.caps;

	u_init_pipe_screen_caps(&rscreen->b.b, 1);

	enum radeon_family family = rscreen->b.family;

	/* Supported features (boolean caps). */
	caps->npot_textures = true;
	caps->mixed_framebuffer_sizes = true;
	caps->mixed_color_depth_bits = true;
	caps->anisotropic_filter = true;
	caps->occlusion_query = true;
	caps->texture_mirror_clamp = true;
	caps->texture_mirror_clamp_to_edge = true;
	caps->blend_equation_separate = true;
	caps->texture_swizzle = true;
	caps->depth_clip_disable = true;
	caps->depth_clip_disable_separate = true;
	caps->shader_stencil_export = true;
	caps->vertex_element_instance_divisor = true;
	caps->fs_coord_origin_upper_left = true;
	caps->fs_coord_pixel_center_half_integer = true;
	caps->fragment_shader_texture_lod = true;
	caps->fragment_shader_derivatives = true;
	caps->seamless_cube_map = true;
	caps->primitive_restart = true;
	caps->primitive_restart_fixed_index = true;
	caps->conditional_render = true;
	caps->texture_barrier = true;
	caps->vertex_color_unclamped = true;
	caps->quads_follow_provoking_vertex_convention = true;
	caps->vs_instanceid = true;
	caps->start_instance = true;
	caps->max_dual_source_render_targets = true;
	caps->texture_buffer_objects = true;
	caps->query_pipeline_statistics = true;
	caps->texture_multisample = true;
	caps->vs_window_space_position = true;
	caps->vs_layer_viewport = true;
	caps->sample_shading = true;
	caps->memobj = true;
	caps->clip_halfz = true;
	caps->polygon_offset_clamp = true;
	caps->conditional_render_inverted = true;
	caps->texture_float_linear = true;
	caps->texture_half_float_linear = true;
	caps->texture_query_samples = true;
	caps->copy_between_compressed_and_plain_formats = true;
	caps->invalidate_buffer = true;
	caps->surface_reinterpret_blocks = true;
	caps->query_memory_info = true;
	caps->framebuffer_no_attachment = true;
	caps->polygon_offset_units_unscaled = true;
	caps->legacy_math_rules = true;
	caps->can_bind_const_buffer_as_vertex = true;
	caps->allow_mapped_buffers_during_execution = true;
	caps->robust_buffer_access_behavior = true;

	caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE;

	caps->nir_atomics_as_deref = true;
	caps->gl_spirv = true;

	caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;

	caps->shareable_shaders = false;

	/* Optimal number for good TexSubImage performance on Polaris10. */
	caps->max_texture_upload_memory_budget = 64 * 1024 * 1024;

	caps->device_reset_status_query = true;

	caps->resource_from_user_memory =
		!UTIL_ARCH_BIG_ENDIAN && rscreen->b.info.has_userptr;

	caps->compute = rscreen->b.gfx_level > R700;

	caps->tgsi_texcoord = true;

	caps->nir_images_as_deref = false;
	caps->fake_sw_msaa = false;

	caps->max_texel_buffer_elements =
		MIN2(rscreen->b.info.max_heap_size_kb * 1024ull / 4, INT_MAX);

	caps->min_map_buffer_alignment = R600_MAP_BUFFER_ALIGNMENT;

	caps->constant_buffer_offset_alignment = 256;

	caps->texture_buffer_offset_alignment = 4;
	caps->glsl_feature_level_compatibility =
	caps->glsl_feature_level = family >= CHIP_CEDAR ? 450 : 330;

	/* Supported except the original R600. */
	caps->indep_blend_enable =
	caps->indep_blend_func = family != CHIP_R600; /* R600 doesn't support per-MRT blends */

	/* Supported on Evergreen. */
	caps->seamless_cube_map_per_texture =
	caps->cube_map_array =
	caps->texture_gather_sm5 =
	caps->texture_query_lod =
	caps->fs_fine_derivative =
	caps->sampler_view_target =
	caps->shader_pack_half_float =
	caps->shader_clock =
	caps->shader_array_components =
	caps->query_buffer_object =
	caps->image_store_formatted =
	caps->alpha_to_coverage_dither_control = family >= CHIP_CEDAR;
	caps->max_texture_gather_components = family >= CHIP_CEDAR ? 4 : 0;
	/* kernel command checker support is also required */
	caps->draw_indirect = family >= CHIP_CEDAR;

	caps->buffer_sampler_view_rgba_only = family < CHIP_CEDAR;

	caps->max_combined_shader_output_resources = 8;

	caps->max_gs_invocations = 32;

	/* shader buffer objects */
	caps->max_shader_buffer_size = 1 << 27;
	caps->max_combined_shader_buffers = 8;

	caps->int64 =
	caps->doubles =
		rscreen->b.family == CHIP_ARUBA ||
		rscreen->b.family == CHIP_CAYMAN ||
		rscreen->b.family == CHIP_CYPRESS ||
		rscreen->b.family == CHIP_HEMLOCK ||
		rscreen->b.family >= CHIP_CEDAR;

	caps->two_sided_color = false;
	caps->cull_distance = true;

	caps->shader_buffer_offset_alignment = family >= CHIP_CEDAR ?  256 : 0;

	caps->max_shader_patch_varyings = family >= CHIP_CEDAR ? 30 : 0;

	/* Stream output. */
	caps->max_stream_output_buffers = rscreen->b.has_streamout ? 4 : 0;
	caps->stream_output_pause_resume =
	caps->stream_output_interleave_buffers = rscreen->b.has_streamout;
	caps->max_stream_output_separate_components =
	caps->max_stream_output_interleaved_components = 32*4;

	/* Geometry shader output. */
	caps->max_geometry_output_vertices = 1024;
	caps->max_geometry_total_output_components = 16384;
	caps->max_vertex_streams = family >= CHIP_CEDAR ? 4 : 1;

	/* Should be 2047, but 2048 is a requirement for GL 4.4 */
	caps->max_vertex_attrib_stride = 2048;

	/* Texturing. */
	caps->max_texture_2d_size = family >= CHIP_CEDAR ? 16384 : 8192;
	caps->max_texture_cube_levels = family >= CHIP_CEDAR ? 15 : 14;
	/* textures support 8192, but layered rendering supports 2048 */
	caps->max_texture_3d_levels = 12;
	/* textures support 8192, but layered rendering supports 2048 */
	caps->max_texture_array_layers = 2048;

	/* Render targets. */
	caps->max_render_targets = 8; /* XXX some r6xx are buggy and can only do 4 */

	caps->max_viewports = R600_MAX_VIEWPORTS;
	caps->viewport_subpixel_bits =
	caps->rasterizer_subpixel_bits = 8;

	/* Timer queries, present when the clock frequency is non zero. */
	caps->query_time_elapsed =
	caps->query_timestamp = rscreen->b.info.clock_crystal_freq != 0;

	/* Conversion to nanos from cycles per millisecond */
	caps->timer_resolution = DIV_ROUND_UP(1000000, rscreen->b.info.clock_crystal_freq);

	caps->min_texture_gather_offset =
	caps->min_texel_offset = -8;

	caps->max_texture_gather_offset =
	caps->max_texel_offset = 7;

	caps->max_varyings = 32;

	caps->texture_border_color_quirk = PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600;
	caps->endianness = PIPE_ENDIAN_LITTLE;

	caps->vendor_id = ATI_VENDOR_ID;
	caps->device_id = rscreen->b.info.pci_id;
	caps->video_memory = rscreen->b.info.vram_size_kb >> 10;
	caps->uma = false;
	caps->multisample_z_resolve = rscreen->b.gfx_level >= R700;
	caps->pci_group = rscreen->b.info.pci.domain;
	caps->pci_bus = rscreen->b.info.pci.bus;
	caps->pci_device = rscreen->b.info.pci.dev;
	caps->pci_function = rscreen->b.info.pci.func;

	caps->max_combined_hw_atomic_counters =
		rscreen->b.family >= CHIP_CEDAR && rscreen->has_atomics ? 8 : 0;

	caps->max_combined_hw_atomic_counter_buffers =
		rscreen->b.family >= CHIP_CEDAR && rscreen->has_atomics ?
		EG_MAX_ATOMIC_BUFFERS : 0;

	caps->validate_all_dirty_states = true;

	caps->min_line_width =
	caps->min_line_width_aa =
	caps->min_point_size =
	caps->min_point_size_aa = 1;

	caps->point_size_granularity =
	caps->line_width_granularity = 0.1;

	caps->max_line_width =
	caps->max_line_width_aa =
	caps->max_point_size =
	caps->max_point_size_aa = 8191.0f;
	caps->max_texture_anisotropy = 16.0f;
	caps->max_texture_lod_bias = 16.0f;
}

static void r600_destroy_screen(struct pipe_screen* pscreen)
{
	struct r600_screen *rscreen = (struct r600_screen *)pscreen;

	if (!rscreen)
		return;

	if (!rscreen->b.ws->unref(rscreen->b.ws))
		return;

	if (rscreen->global_pool) {
		compute_memory_pool_delete(rscreen->global_pool);
	}

	r600_destroy_common_screen(&rscreen->b);
}

static struct pipe_resource *r600_resource_create(struct pipe_screen *screen,
						  const struct pipe_resource *templ)
{
	if (templ->target == PIPE_BUFFER &&
	    (templ->bind & PIPE_BIND_GLOBAL))
		return r600_compute_global_buffer_create(screen, templ);

	return r600_resource_create_common(screen, templ);
}

struct pipe_screen *r600_screen_create(struct radeon_winsys *ws,
				       const struct pipe_screen_config *config)
{
	struct r600_screen *rscreen = CALLOC_STRUCT(r600_screen);

	if (!rscreen) {
		return NULL;
	}

	/* Set functions first. */
	rscreen->b.b.context_create = r600_create_context;
	rscreen->b.b.destroy = r600_destroy_screen;
	rscreen->b.b.resource_create = r600_resource_create;

	if (!r600_common_screen_init(&rscreen->b, ws)) {
		FREE(rscreen);
		return NULL;
	}

	if (rscreen->b.info.gfx_level >= EVERGREEN) {
		rscreen->b.b.is_format_supported = evergreen_is_format_supported;
	} else {
		rscreen->b.b.is_format_supported = r600_is_format_supported;
	}

	rscreen->b.debug_flags |= debug_get_flags_option("R600_DEBUG", r600_debug_options, 0);
	if (debug_get_bool_option("R600_DEBUG_COMPUTE", false))
		rscreen->b.debug_flags |= DBG_COMPUTE;
	if (debug_get_bool_option("R600_DUMP_SHADERS", false))
		rscreen->b.debug_flags |= DBG_ALL_SHADERS | DBG_FS;
	if (!debug_get_bool_option("R600_HYPERZ", true))
		rscreen->b.debug_flags |= DBG_NO_HYPERZ;

	if (rscreen->b.family == CHIP_UNKNOWN) {
		fprintf(stderr, "r600: Unknown chipset 0x%04X\n", rscreen->b.info.pci_id);
		FREE(rscreen);
		return NULL;
	}

	rscreen->b.b.finalize_nir = r600_finalize_nir;

	rscreen->b.has_streamout = true;

	rscreen->has_msaa = true;

	/* MSAA support. */
	switch (rscreen->b.gfx_level) {
	case R600:
	case R700:
		rscreen->has_compressed_msaa_texturing = false;
		break;
	case EVERGREEN:
		rscreen->has_compressed_msaa_texturing = true;
		break;
	case CAYMAN:
		rscreen->has_compressed_msaa_texturing = true;
		break;
	default:
		rscreen->has_compressed_msaa_texturing = false;
	}

	rscreen->b.has_cp_dma = !(rscreen->b.debug_flags & DBG_NO_CP_DMA);

	rscreen->b.barrier_flags.cp_to_L2 =
		R600_CONTEXT_INV_VERTEX_CACHE |
		R600_CONTEXT_INV_TEX_CACHE |
		R600_CONTEXT_INV_CONST_CACHE;
	rscreen->b.barrier_flags.compute_to_L2 = R600_CONTEXT_CS_PARTIAL_FLUSH | R600_CONTEXT_FLUSH_AND_INV;

	rscreen->global_pool = compute_memory_pool_new(rscreen);

	rscreen->has_atomics = true;

	r600_init_compute_caps(rscreen);
	r600_init_shader_caps(rscreen);
	r600_init_screen_caps(rscreen);

	/* Create the auxiliary context. This must be done last. */
	rscreen->b.aux_context = rscreen->b.b.context_create(&rscreen->b.b, NULL, 0);

#if 0 /* This is for testing whether aux_context and buffer clearing work correctly. */
	struct pipe_resource templ = {};

	templ.width0 = 4;
	templ.height0 = 2048;
	templ.depth0 = 1;
	templ.array_size = 1;
	templ.target = PIPE_TEXTURE_2D;
	templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	templ.usage = PIPE_USAGE_DEFAULT;

	struct r600_resource *res = r600_resource(rscreen->screen.resource_create(&rscreen->screen, &templ));
	unsigned char *map = ws->buffer_map(res->buf, NULL, PIPE_MAP_WRITE);

	memset(map, 0, 256);

	r600_screen_clear_buffer(rscreen, &res->b.b, 4, 4, 0xCC);
	r600_screen_clear_buffer(rscreen, &res->b.b, 8, 4, 0xDD);
	r600_screen_clear_buffer(rscreen, &res->b.b, 12, 4, 0xEE);
	r600_screen_clear_buffer(rscreen, &res->b.b, 20, 4, 0xFF);
	r600_screen_clear_buffer(rscreen, &res->b.b, 32, 20, 0x87);

	ws->buffer_wait(res->buf, RADEON_USAGE_WRITE);

	int i;
	for (i = 0; i < 256; i++) {
		printf("%02X", map[i]);
		if (i % 16 == 15)
			printf("\n");
	}
#endif

	if (rscreen->b.debug_flags & DBG_TEST_DMA)
		r600_test_dma(&rscreen->b);

	r600_query_fix_enabled_rb_mask(&rscreen->b);
	return &rscreen->b.b;
}
