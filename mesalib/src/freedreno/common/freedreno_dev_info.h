/*
 * Copyright © 2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef FREEDRENO_DEVICE_INFO_H
#define FREEDRENO_DEVICE_INFO_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Freedreno hardware description and quirks
 */

struct fd_dev_info {
   uint8_t chip;

   /* alignment for size of tiles */
   uint32_t tile_align_w, tile_align_h;
   /* gmem load/store granularity */
   uint32_t gmem_align_w, gmem_align_h;
   /* max tile size */
   uint32_t tile_max_w, tile_max_h;

   uint32_t num_vsc_pipes;

   uint32_t cs_shared_mem_size;

   int wave_granularity;

   /* These are fallback values that should match what drm/msm programs, for
    * kernels that don't support returning them. Newer devices should not set
    * them and just use the value from the kernel.
    */
   uint32_t highest_bank_bit;
   uint32_t ubwc_swizzle;
   uint32_t macrotile_mode;

   /* Information for private memory calculations */
   uint32_t fibers_per_sp;

   uint32_t threadsize_base;

   uint32_t max_waves;

   /* number of CCU is always equal to the number of SP */
   union {
      uint32_t num_sp_cores;
      uint32_t num_ccu;
   };

   struct {
      uint32_t reg_size_vec4;

      /* The size (in instrlen units (128 bytes)) of instruction cache where
       * we preload a shader. Loading more than this could trigger a hang
       * on gen3 and later.
       */
      uint32_t instr_cache_size;

      bool has_hw_multiview;

      bool has_fs_tex_prefetch;

      /* Whether the PC_MULTIVIEW_MASK register exists. */
      bool supports_multiview_mask;

      /* info for setting RB_CCU_CNTL */
      bool concurrent_resolve;
      bool has_z24uint_s8uint;

      bool tess_use_shared;

      /* Does the hw support GL_QCOM_shading_rate? */
      bool has_legacy_pipeline_shading_rate;

      /* Whether a 16-bit descriptor can be used */
      bool storage_16bit;

      /* The latest known a630_sqe.fw fails to wait for WFI before
       * reading the indirect buffer when using CP_DRAW_INDIRECT_MULTI,
       * so we have to fall back to CP_WAIT_FOR_ME except for a650
       * which has a fixed firmware.
       *
       * TODO: There may be newer a630_sqe.fw released in the future
       * which fixes this, if so we should detect it and avoid this
       * workaround.  Once we have uapi to query fw version, we can
       * replace this with minimum fw version.
       */
      bool indirect_draw_wfm_quirk;

      /* On some GPUs, the depth test needs to be enabled when the
       * depth bounds test is enabled and the depth attachment uses UBWC.
       */
      bool depth_bounds_require_depth_test_quirk;

      bool has_tex_filter_cubic;

      /* The blob driver does not support SEPARATE_RECONSTRUCTION_FILTER_BIT
       * before a6xx_gen3.  It still sets CHROMA_LINEAR bit according to
       * chromaFilter, but the bit has no effect before a6xx_gen3.
       */
      bool has_separate_chroma_filter;

      bool has_sample_locations;

      /* The firmware on newer a6xx drops CP_REG_WRITE support as we
       * can now use direct register writes for these regs.
       */
      bool has_cp_reg_write;

      bool has_8bpp_ubwc;

      bool has_lpac;

      bool has_getfiberid;

      bool has_dp2acc;
      bool has_dp4acc;

      /* LRZ fast-clear works on all gens, however blob disables it on
       * gen1 and gen2. We also elect to disable fast-clear on these gens
       * because for close to none gains it adds complexity and seem to work
       * a bit differently from gen3+. Which creates at least one edge case:
       * if first draw which uses LRZ fast-clear doesn't lock LRZ direction
       * the fast-clear value is undefined. For details see
       * https://gitlab.freedesktop.org/mesa/mesa/-/issues/6829
       */
      bool enable_lrz_fast_clear;
      bool has_lrz_dir_tracking;
      bool lrz_track_quirk;
      bool has_lrz_feedback;

      /* Some generations have a bit to add the multiview index to the
       * viewport index, which lets us implement different scaling for
       * different views.
       */
      bool has_per_view_viewport;
      bool has_gmem_fast_clear;

      /* Per CCU GMEM amount reserved for each of DEPTH and COLOR caches
       * in sysmem rendering. */
      uint32_t sysmem_per_ccu_depth_cache_size;
      uint32_t sysmem_per_ccu_color_cache_size;
      /* Per CCU GMEM amount reserved for color cache used by GMEM resolves
       * which require color cache (non-BLIT event case).
       * The size is expressed as a fraction of ccu cache used by sysmem
       * rendering. If a GMEM resolve requires color cache, the driver needs
       * to make sure it will not overwrite pixel data in GMEM that is still
       * needed.
       */
      /* see enum a6xx_ccu_cache_size */
      uint32_t gmem_ccu_color_cache_fraction;

      /* Corresponds to HLSQ_CONTROL_1_REG::PRIMALLOCTHRESHOLD */
      uint32_t prim_alloc_threshold;

      uint32_t vs_max_inputs_count;

      bool supports_double_threadsize;

      bool has_sampler_minmax;

      bool broken_ds_ubwc_quirk;

      /* See ir3_compiler::has_scalar_alu. */
      bool has_scalar_alu;
      /* See ir3_compiler::has_early_preamble. */
      bool has_early_preamble;

      bool has_isam_v;
      bool has_ssbo_imm_offsets;

      /* Whether writing to UBWC attachment and reading the same image as input
       * attachment or as a texture reads correct values from the image.
       * If this is false, we may read stale values from the flag buffer,
       * thus reading incorrect values from the image.
       * Happens with VK_EXT_attachment_feedback_loop_layout.
       */
      bool has_coherent_ubwc_flag_caches;

      bool has_attachment_shading_rate;

      /* Whether mipmaps below certain threshold can use LINEAR tiling when higher
       * levels use UBWC,
       */
      bool has_ubwc_linear_mipmap_fallback;

      /* Whether 4 nops are needed after the second pred[tf] of a
       * pred[tf]/pred[ft] pair to work around a hardware issue.
       */
      bool predtf_nop_quirk;

      /* Whether 6 nops are needed after prede to work around a hardware
       * issue.
       */
      bool prede_nop_quirk;

      /* Whether the sad instruction (iadd3) is supported. */
      bool has_sad;

      struct {
         uint32_t PC_POWER_CNTL;
         uint32_t TPL1_DBG_ECO_CNTL;
         uint32_t GRAS_DBG_ECO_CNTL;
         uint32_t SP_CHICKEN_BITS;
         uint32_t UCHE_CLIENT_PF;
         uint32_t PC_MODE_CNTL;
         uint32_t SP_DBG_ECO_CNTL;
         uint32_t RB_DBG_ECO_CNTL;
         uint32_t RB_DBG_ECO_CNTL_blit;
         uint32_t HLSQ_DBG_ECO_CNTL;
         uint32_t RB_UNKNOWN_8E01;
         uint32_t VPC_DBG_ECO_CNTL;
         uint32_t UCHE_UNKNOWN_0E12;

         uint32_t RB_UNKNOWN_8E06;
      } magic;

      struct {
            uint32_t reg;
            uint32_t value;
      } magic_raw[64];

      /* maximum number of descriptor sets */
      uint32_t max_sets;

      float line_width_min;
      float line_width_max;

      bool has_bin_mask;
   } a6xx;

   struct {
      /* stsc may need to be done twice for the same range to workaround
       * _something_, observed in blob's disassembly.
       */
      bool stsc_duplication_quirk;

      /* Whether there is CP_EVENT_WRITE7::WRITE_SAMPLE_COUNT */
      bool has_event_write_sample_count;

      bool has_64b_ssbo_atomics;

      /* Blob executes a special compute dispatch at the start of each
       * command buffers. We copy this dispatch as is.
       */
      bool cmdbuf_start_a725_quirk;

      bool load_inline_uniforms_via_preamble_ldgk;
      bool load_shader_consts_via_preamble;

      bool has_gmem_vpc_attr_buf;
      /* Size of buffer in gmem for VPC attributes */
      uint32_t sysmem_vpc_attr_buf_size;
      uint32_t gmem_vpc_attr_buf_size;

      /* Whether UBWC is supported on all IBOs. Prior to this, only readonly
       * or writeonly IBOs could use UBWC and mixing reads and writes was not
       * permitted.
       */
      bool supports_ibo_ubwc;

      /* Whether the UBWC fast-clear values for snorn, unorm, and int formats
       * are the same. This is the case from a740 onwards. These formats were
       * already otherwise UBWC-compatible, so this means that they are now
       * fully compatible.
       */
      bool ubwc_unorm_snorm_int_compatible;

      /* Having zero consts in one FS may corrupt consts in follow up FSs,
       * on such GPUs blob never has zero consts in FS. The mechanism of
       * corruption is unknown.
       */
      bool fs_must_have_non_zero_constlen_quirk;

      /* On a750 there is a hardware bug where certain VPC sizes in a GS with
       * an input primitive type that is a triangle with adjacency can hang
       * with a high enough vertex count.
       */
      bool gs_vpc_adjacency_quirk;

      /* On a740 TPL1_DBG_ECO_CNTL1.TP_UBWC_FLAG_HINT must be the same between
       * all drivers in the system, somehow having different values affects
       * BLIT_OP_SCALE. We cannot automatically match blob's value, so the
       * best thing we could do is a toggle.
       */
      bool enable_tp_ubwc_flag_hint;

      bool storage_8bit;

      /* A750+ added a special flag that allows HW to correctly interpret UBWC, including
       * UBWC fast-clear when casting image to a different format permitted by Vulkan.
       * So it's possible to have UBWC enabled for image that has e.g. R32_UINT and
       * R8G8B8A8_UNORM in the mutable formats list.
       */
      bool ubwc_all_formats_compatible;

      bool has_compliant_dp4acc;

      /* Whether a single clear blit could be used for both sysmem and gmem.*/
      bool has_generic_clear;

      /* Whether r8g8 UBWC fast-clear work correctly. */
      bool r8g8_faulty_fast_clear_quirk;

      /* a750 has a bug where writing and then reading a UBWC-compressed IBO
       * requires flushing UCHE. This is reproducible in many CTS tests, for
       * example dEQP-VK.image.load_store.with_format.2d.*.
       */
      bool ubwc_coherency_quirk;

      /* Whether CP_ALWAYS_ON_COUNTER only resets on device loss rather than
       * on every suspend/resume.
       */
      bool has_persistent_counter;

      /* Whether only 256 vec4 constants are available for compute */
      bool compute_constlen_quirk;

      bool has_primitive_shading_rate;

      /* A7XX gen1 and gen2 seem to require declaring SAMPLEMASK input
       * for fragment shading rate to be read correctly.
       * This workaround was seen in the prop driver v512.762.12.
       */
      bool reading_shading_rate_requires_smask_quirk;

      /* Whether the ray_intersection instruction is present. */
      bool has_ray_intersection;

      /* Whether features may be fused off by the SW_FUSE. So far, this is
       * just raytracing.
       */
      bool has_sw_fuse;

      /* a750-specific HW bug workaround for ray tracing */
      bool has_rt_workaround;

      /* Whether alias.rt is supported. */
      bool has_alias_rt;

      /* Whether CP_SET_BIN_DATA5::ABS_MASK exists */
      bool has_abs_bin_mask;
   } a7xx;
};

struct fd_dev_id {
   uint32_t gpu_id;
   uint64_t chip_id;
};

/**
 * Note that gpu-id should be considered deprecated.  For newer a6xx, if
 * there is no gpu-id, this attempts to generate one from the chip-id.
 * But that may not work forever, so avoid depending on this for newer
 * gens
 */
static inline uint32_t
fd_dev_gpu_id(const struct fd_dev_id *id)
{
   assert(id->gpu_id || id->chip_id);
   if (!id->gpu_id) {
      return ((id->chip_id >> 24) & 0xff) * 100 +
             ((id->chip_id >> 16) & 0xff) * 10 +
             ((id->chip_id >>  8) & 0xff);

   }
   return id->gpu_id;
}

/* Unmodified dev info as defined in freedreno_devices.py */
const struct fd_dev_info *fd_dev_info_raw(const struct fd_dev_id *id);

/* Final dev info with dbg options and everything else applied.  */
const struct fd_dev_info fd_dev_info(const struct fd_dev_id *id);

const struct fd_dev_info *fd_dev_info_raw_by_name(const char *name);

static uint8_t
fd_dev_gen(const struct fd_dev_id *id)
{
   return fd_dev_info_raw(id)->chip;
}

static inline bool
fd_dev_64b(const struct fd_dev_id *id)
{
   return fd_dev_gen(id) >= 5;
}

/* per CCU GMEM amount reserved for depth cache for direct rendering */
#define A6XX_CCU_DEPTH_SIZE (64 * 1024)
/* per CCU GMEM amount reserved for color cache used by GMEM resolves
 * which require color cache (non-BLIT event case).
 * this is smaller than what is normally used by direct rendering
 * (RB_CCU_CNTL.GMEM bit enables this smaller size)
 * if a GMEM resolve requires color cache, the driver needs to make sure
 * it will not overwrite pixel data in GMEM that is still needed
 */
#define A6XX_CCU_GMEM_COLOR_SIZE (16 * 1024)

const char * fd_dev_name(const struct fd_dev_id *id);

void
fd_dev_info_apply_dbg_options(struct fd_dev_info *info);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* FREEDRENO_DEVICE_INFO_H */
