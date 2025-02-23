top_srcdir=../../../..

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__ HAVE_DRI

PACKAGE_VERSION:=\"$(strip $(shell cat $(top_srcdir)/VERSION))\"
DEFINES += PACKAGE_VERSION=$(PACKAGE_VERSION)

INCLUDES += $(MHMAKECONF)/include ../../../loader ../../auxiliary ../../auxiliary/util ../../auxiliary/driver_rbug ../../include ../../.. $(top_srcdir)/include ../../frontends/dri \
  ../../../util ../../../mesa ../../drivers ../../../mapi ../../../compiler/nir ../../winsys ../../../$(OBJDIR)

LIBRARY = libgallium

vpath %.c ../../frontends/dri:../../auxiliary/os:../../auxiliary/indices:../../auxiliary/postprocess:../../auxiliary/driver_noop:../../auxiliary/driver_rbug:../../auxiliary/driver_trace:../../auxiliary/driver_ddebug:../../auxiliary/cso_cache:../../auxiliary/util:../../auxiliary/translate:../../auxiliary/rtasm:../../auxiliary/tgsi:../../auxiliary/hud:../../auxiliary/draw:../../auxiliary/nir:../../auxiliary/pipe-loader:../../drivers/softpipe:../../winsys/sw/dri::../../winsys/sw/wrapper:../../winsys/sw/null:../dril

tr_util.c ../../auxiliary/tr_util.h: ../../include/pipe/p_defines.h ../../include/pipe/p_video_enums.h ../../../util/blend.h
	$(PYTHON3) ../../auxiliary/driver_trace/enums2names.py $^ -C tr_util.c -H ../../auxiliary/tr_util.h -I tr_util.h


CSRCS := dri_target.c drisw.c dri2.c dri_context.c dri_screen.c dri_drawable.c dri_helpers.c dri_query_renderer.c dri_util.c \
  pp_colors.c pp_mlaa.c pp_init.c pp_run.c pp_program.c pp_celshade.c \
  cso_context.c cso_cache.c cso_hash.c \
  u_vbuf.c u_upload_mgr.c u_simple_shaders.c u_bitmask.c u_gen_mipmap.c u_draw.c u_helpers.c u_framebuffer.c u_tile.c u_surface.c u_draw_quad.c u_sampler.c u_screen.c u_pstipple.c u_blitter.c u_texture.c u_transfer.c \
  translate_cache.c translate.c translate_generic.c translate_sse.c \
  rtasm_x86sse.c rtasm_execmem.c \
  tgsi_strings.c tgsi_ureg.c tgsi_info.c tgsi_build.c tgsi_parse.c tgsi_dump.c tgsi_iterate.c tgsi_scan.c tgsi_util.c tgsi_transform.c tgsi_exec.c tgsi_text.c tgsi_sanity.c \
  hud_context.c hud_driver_query.c hud_cpu.c hud_fps.c font.c \
  draw_context.c draw_prim_assembler.c draw_gs.c draw_pipe.c draw_pipe_validate.c draw_pipe_wide_point.c draw_pipe_util.c draw_pipe_wide_line.c draw_pipe_stipple.c draw_pipe_user_cull.c draw_pipe_cull.c draw_pipe_flatshade.c draw_pipe_clip.c draw_pipe_offset.c draw_pipe_twoside.c draw_pipe_unfilled.c draw_pipe_aaline.c draw_pipe_aapoint.c draw_pt.c draw_pt_mesh_pipeline.c draw_pt_util.c draw_pt_fetch_shade_pipeline.c draw_pt_post_vs.c draw_pt_fetch.c draw_pt_so_emit.c draw_pt_emit.c draw_vertex.c draw_pt_fetch_shade_emit.c draw_vs.c draw_pt_vsplit.c draw_tess.c draw_vs_exec.c draw_vs_variant.c tgsi_from_mesa.c draw_fs.c draw_pipe_vbuf.c draw_pipe_pstipple.c\
  nir_to_tgsi.c \
  pipe_loader.c pipe_loader_sw.c \
  sp_screen.c sp_texture.c sp_context.c sp_state_shader.c sp_state_rasterizer.c sp_fs_exec.c sp_image.c sp_tex_sample.c sp_tex_tile_cache.c sp_query.c sp_tile_cache.c sp_surface.c sp_compute.c sp_state_derived.c sp_state_sampler.c sp_quad_pipe.c sp_draw_arrays.c sp_state_surface.c sp_state_image.c sp_state_vertex.c sp_state_so.c sp_state_clip.c sp_state_blend.c sp_prim_vbuf.c sp_flush.c sp_setup.c sp_quad_blend.c sp_quad_depth_test.c sp_quad_fs.c sp_clear.c sp_buffer.c sp_fence.c \
   dri_sw_winsys.c wrapper_sw_winsys.c null_sw_winsys.c dd_screen.c u_tests.c tr_screen.c tr_dump.c tr_dump_state.c dd_context.c dd_draw.c u_dump_state.c \
   u_dump_defines.c u_log.c tr_video.c tr_context.c tr_texture.c u_threaded_context.c \
   noop_pipe.c noop_state.c nir_draw_helpers.c \
   u_prim_restart.c u_driconf.c \
   tr_util.c
