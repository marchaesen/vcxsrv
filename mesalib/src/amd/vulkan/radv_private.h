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

#ifndef RADV_PRIVATE_H
#define RADV_PRIVATE_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

#include <amdgpu.h>
#include "compiler/shader_enums.h"
#include "util/macros.h"
#include "util/list.h"
#include "main/macros.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"

#include "radv_radeon_winsys.h"
#include "ac_binary.h"
#include "ac_nir_to_llvm.h"
#include "ac_gpu_info.h"
#include "ac_surface.h"
#include "radv_descriptor_set.h"
#include "radv_extensions.h"

#include <llvm-c/TargetMachine.h>

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_intel.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vk_android_native_buffer.h>

#include "radv_entrypoints.h"

#include "wsi_common.h"

#define ATI_VENDOR_ID 0x1002

#define MAX_VBS         32
#define MAX_VERTEX_ATTRIBS 32
#define MAX_RTS          8
#define MAX_VIEWPORTS   16
#define MAX_SCISSORS    16
#define MAX_DISCARD_RECTANGLES 4
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_PUSH_DESCRIPTORS 32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define MAX_SAMPLES_LOG2 4
#define NUM_META_FS_KEYS 13
#define RADV_MAX_DRM_DEVICES 8
#define MAX_VIEWS        8

#define NUM_DEPTH_CLEAR_PIPELINES 3

/*
 * This is the point we switch from using CP to compute shader
 * for certain buffer operations.
 */
#define RADV_BUFFER_OPS_CS_THRESHOLD 4096

enum radv_mem_heap {
	RADV_MEM_HEAP_VRAM,
	RADV_MEM_HEAP_VRAM_CPU_ACCESS,
	RADV_MEM_HEAP_GTT,
	RADV_MEM_HEAP_COUNT
};

enum radv_mem_type {
	RADV_MEM_TYPE_VRAM,
	RADV_MEM_TYPE_GTT_WRITE_COMBINE,
	RADV_MEM_TYPE_VRAM_CPU_ACCESS,
	RADV_MEM_TYPE_GTT_CACHED,
	RADV_MEM_TYPE_COUNT
};

#define radv_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
	assert(a != 0 && a == (a & -a));
	return (v + a - 1) & ~(a - 1);
}

static inline uint32_t
align_u32_npot(uint32_t v, uint32_t a)
{
	return (v + a - 1) / a * a;
}

static inline uint64_t
align_u64(uint64_t v, uint64_t a)
{
	assert(a != 0 && a == (a & -a));
	return (v + a - 1) & ~(a - 1);
}

static inline int32_t
align_i32(int32_t v, int32_t a)
{
	assert(a != 0 && a == (a & -a));
	return (v + a - 1) & ~(a - 1);
}

/** Alignment must be a power of 2. */
static inline bool
radv_is_aligned(uintmax_t n, uintmax_t a)
{
	assert(a == (a & -a));
	return (n & (a - 1)) == 0;
}

static inline uint32_t
round_up_u32(uint32_t v, uint32_t a)
{
	return (v + a - 1) / a;
}

static inline uint64_t
round_up_u64(uint64_t v, uint64_t a)
{
	return (v + a - 1) / a;
}

static inline uint32_t
radv_minify(uint32_t n, uint32_t levels)
{
	if (unlikely(n == 0))
		return 0;
	else
		return MAX2(n >> levels, 1);
}
static inline float
radv_clamp_f(float f, float min, float max)
{
	assert(min < max);

	if (f > max)
		return max;
	else if (f < min)
		return min;
	else
		return f;
}

static inline bool
radv_clear_mask(uint32_t *inout_mask, uint32_t clear_mask)
{
	if (*inout_mask & clear_mask) {
		*inout_mask &= ~clear_mask;
		return true;
	} else {
		return false;
	}
}

#define for_each_bit(b, dword)                          \
	for (uint32_t __dword = (dword);		\
	     (b) = __builtin_ffs(__dword) - 1, __dword;	\
	     __dword &= ~(1 << (b)))

#define typed_memcpy(dest, src, count) ({				\
			STATIC_ASSERT(sizeof(*src) == sizeof(*dest)); \
			memcpy((dest), (src), (count) * sizeof(*(src))); \
		})

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

VkResult __vk_errorf(VkResult error, const char *file, int line, const char *format, ...);

#ifdef DEBUG
#define vk_error(error) __vk_errorf(error, __FILE__, __LINE__, NULL);
#define vk_errorf(error, format, ...) __vk_errorf(error, __FILE__, __LINE__, format, ## __VA_ARGS__);
#else
#define vk_error(error) error
#define vk_errorf(error, format, ...) error
#endif

void __radv_finishme(const char *file, int line, const char *format, ...)
	radv_printflike(3, 4);
void radv_loge(const char *format, ...) radv_printflike(1, 2);
void radv_loge_v(const char *format, va_list va);

/**
 * Print a FINISHME message, including its source location.
 */
#define radv_finishme(format, ...)					\
	do { \
		static bool reported = false; \
		if (!reported) { \
			__radv_finishme(__FILE__, __LINE__, format, ##__VA_ARGS__); \
			reported = true; \
		} \
	} while (0)

/* A non-fatal assert.  Useful for debugging. */
#ifdef DEBUG
#define radv_assert(x) ({						\
			if (unlikely(!(x)))				\
				fprintf(stderr, "%s:%d ASSERT: %s\n", __FILE__, __LINE__, #x); \
		})
#else
#define radv_assert(x)
#endif

#define stub_return(v)					\
	do {						\
		radv_finishme("stub %s", __func__);	\
		return (v);				\
	} while (0)

#define stub()						\
	do {						\
		radv_finishme("stub %s", __func__);	\
		return;					\
	} while (0)

void *radv_lookup_entrypoint_unchecked(const char *name);
void *radv_lookup_entrypoint_checked(const char *name,
                                    uint32_t core_version,
                                    const struct radv_instance_extension_table *instance,
                                    const struct radv_device_extension_table *device);

struct radv_physical_device {
	VK_LOADER_DATA                              _loader_data;

	struct radv_instance *                       instance;

	struct radeon_winsys *ws;
	struct radeon_info rad_info;
	char                                        path[20];
	char                                        name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	uint8_t                                     driver_uuid[VK_UUID_SIZE];
	uint8_t                                     device_uuid[VK_UUID_SIZE];
	uint8_t                                     cache_uuid[VK_UUID_SIZE];

	int local_fd;
	struct wsi_device                       wsi_device;

	bool has_rbplus; /* if RB+ register exist */
	bool rbplus_allowed; /* if RB+ is allowed */
	bool has_clear_state;
	bool cpdma_prefetch_writes_memory;
	bool has_scissor_bug;

	/* This is the drivers on-disk cache used as a fallback as opposed to
	 * the pipeline cache defined by apps.
	 */
	struct disk_cache *                          disk_cache;

	VkPhysicalDeviceMemoryProperties memory_properties;
	enum radv_mem_type mem_type_indices[RADV_MEM_TYPE_COUNT];

	struct radv_device_extension_table supported_extensions;
};

struct radv_instance {
	VK_LOADER_DATA                              _loader_data;

	VkAllocationCallbacks                       alloc;

	uint32_t                                    apiVersion;
	int                                         physicalDeviceCount;
	struct radv_physical_device                 physicalDevices[RADV_MAX_DRM_DEVICES];

	uint64_t debug_flags;
	uint64_t perftest_flags;

	struct vk_debug_report_instance             debug_report_callbacks;

	struct radv_instance_extension_table enabled_extensions;
};

VkResult radv_init_wsi(struct radv_physical_device *physical_device);
void radv_finish_wsi(struct radv_physical_device *physical_device);

bool radv_instance_extension_supported(const char *name);
uint32_t radv_physical_device_api_version(struct radv_physical_device *dev);
bool radv_physical_device_extension_supported(struct radv_physical_device *dev,
					      const char *name);

struct cache_entry;

struct radv_pipeline_cache {
	struct radv_device *                          device;
	pthread_mutex_t                              mutex;

	uint32_t                                     total_size;
	uint32_t                                     table_size;
	uint32_t                                     kernel_count;
	struct cache_entry **                        hash_table;
	bool                                         modified;

	VkAllocationCallbacks                        alloc;
};

struct radv_pipeline_key {
	uint32_t instance_rate_inputs;
	unsigned tess_input_vertices;
	uint32_t col_format;
	uint32_t is_int8;
	uint32_t is_int10;
	uint8_t log2_ps_iter_samples;
	uint8_t log2_num_samples;
	uint32_t multisample : 1;
	uint32_t has_multiview_view_index : 1;
};

void
radv_pipeline_cache_init(struct radv_pipeline_cache *cache,
			 struct radv_device *device);
void
radv_pipeline_cache_finish(struct radv_pipeline_cache *cache);
void
radv_pipeline_cache_load(struct radv_pipeline_cache *cache,
			 const void *data, size_t size);

struct radv_shader_variant;

bool
radv_create_shader_variants_from_pipeline_cache(struct radv_device *device,
					        struct radv_pipeline_cache *cache,
					        const unsigned char *sha1,
					        struct radv_shader_variant **variants);

void
radv_pipeline_cache_insert_shaders(struct radv_device *device,
				   struct radv_pipeline_cache *cache,
				   const unsigned char *sha1,
				   struct radv_shader_variant **variants,
				   const void *const *codes,
				   const unsigned *code_sizes);

enum radv_blit_ds_layout {
	RADV_BLIT_DS_LAYOUT_TILE_ENABLE,
	RADV_BLIT_DS_LAYOUT_TILE_DISABLE,
	RADV_BLIT_DS_LAYOUT_COUNT,
};

static inline enum radv_blit_ds_layout radv_meta_blit_ds_to_type(VkImageLayout layout)
{
	return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_BLIT_DS_LAYOUT_TILE_DISABLE : RADV_BLIT_DS_LAYOUT_TILE_ENABLE;
}

static inline VkImageLayout radv_meta_blit_ds_to_layout(enum radv_blit_ds_layout ds_layout)
{
	return ds_layout == RADV_BLIT_DS_LAYOUT_TILE_ENABLE ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}

enum radv_meta_dst_layout {
	RADV_META_DST_LAYOUT_GENERAL,
	RADV_META_DST_LAYOUT_OPTIMAL,
	RADV_META_DST_LAYOUT_COUNT,
};

static inline enum radv_meta_dst_layout radv_meta_dst_layout_from_layout(VkImageLayout layout)
{
	return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_META_DST_LAYOUT_GENERAL : RADV_META_DST_LAYOUT_OPTIMAL;
}

static inline VkImageLayout radv_meta_dst_layout_to_layout(enum radv_meta_dst_layout layout)
{
	return layout == RADV_META_DST_LAYOUT_OPTIMAL ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}

struct radv_meta_state {
	VkAllocationCallbacks alloc;

	struct radv_pipeline_cache cache;

	/**
	 * Use array element `i` for images with `2^i` samples.
	 */
	struct {
		VkRenderPass render_pass[NUM_META_FS_KEYS];
		VkPipeline color_pipelines[NUM_META_FS_KEYS];

		VkRenderPass depthstencil_rp;
		VkPipeline depth_only_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
		VkPipeline stencil_only_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
		VkPipeline depthstencil_pipeline[NUM_DEPTH_CLEAR_PIPELINES];
	} clear[1 + MAX_SAMPLES_LOG2];

	VkPipelineLayout                          clear_color_p_layout;
	VkPipelineLayout                          clear_depth_p_layout;
	struct {
		VkRenderPass render_pass[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];

		/** Pipeline that blits from a 1D image. */
		VkPipeline pipeline_1d_src[NUM_META_FS_KEYS];

		/** Pipeline that blits from a 2D image. */
		VkPipeline pipeline_2d_src[NUM_META_FS_KEYS];

		/** Pipeline that blits from a 3D image. */
		VkPipeline pipeline_3d_src[NUM_META_FS_KEYS];

		VkRenderPass depth_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
		VkPipeline depth_only_1d_pipeline;
		VkPipeline depth_only_2d_pipeline;
		VkPipeline depth_only_3d_pipeline;

		VkRenderPass stencil_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
		VkPipeline stencil_only_1d_pipeline;
		VkPipeline stencil_only_2d_pipeline;
		VkPipeline stencil_only_3d_pipeline;
		VkPipelineLayout                          pipeline_layout;
		VkDescriptorSetLayout                     ds_layout;
	} blit;

	struct {
		VkRenderPass render_passes[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];

		VkPipelineLayout p_layouts[3];
		VkDescriptorSetLayout ds_layouts[3];
		VkPipeline pipelines[3][NUM_META_FS_KEYS];

		VkRenderPass depth_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
		VkPipeline depth_only_pipeline[3];

		VkRenderPass stencil_only_rp[RADV_BLIT_DS_LAYOUT_COUNT];
		VkPipeline stencil_only_pipeline[3];
	} blit2d;

	struct {
		VkPipelineLayout                          img_p_layout;
		VkDescriptorSetLayout                     img_ds_layout;
		VkPipeline pipeline;
		VkPipeline pipeline_3d;
	} itob;
	struct {
		VkPipelineLayout                          img_p_layout;
		VkDescriptorSetLayout                     img_ds_layout;
		VkPipeline pipeline;
		VkPipeline pipeline_3d;
	} btoi;
	struct {
		VkPipelineLayout                          img_p_layout;
		VkDescriptorSetLayout                     img_ds_layout;
		VkPipeline pipeline;
		VkPipeline pipeline_3d;
	} itoi;
	struct {
		VkPipelineLayout                          img_p_layout;
		VkDescriptorSetLayout                     img_ds_layout;
		VkPipeline pipeline;
		VkPipeline pipeline_3d;
	} cleari;

	struct {
		VkPipelineLayout                          p_layout;
		VkPipeline                                pipeline[NUM_META_FS_KEYS];
		VkRenderPass                              pass[NUM_META_FS_KEYS];
	} resolve;

	struct {
		VkDescriptorSetLayout                     ds_layout;
		VkPipelineLayout                          p_layout;
		struct {
			VkPipeline                                pipeline;
			VkPipeline                                i_pipeline;
			VkPipeline                                srgb_pipeline;
		} rc[MAX_SAMPLES_LOG2];
	} resolve_compute;

	struct {
		VkDescriptorSetLayout                     ds_layout;
		VkPipelineLayout                          p_layout;

		struct {
			VkRenderPass render_pass[NUM_META_FS_KEYS][RADV_META_DST_LAYOUT_COUNT];
			VkPipeline   pipeline[NUM_META_FS_KEYS];
		} rc[MAX_SAMPLES_LOG2];
	} resolve_fragment;

	struct {
		VkPipelineLayout                          p_layout;
		VkPipeline                                decompress_pipeline;
		VkPipeline                                resummarize_pipeline;
		VkRenderPass                              pass;
	} depth_decomp[1 + MAX_SAMPLES_LOG2];

	struct {
		VkPipelineLayout                          p_layout;
		VkPipeline                                cmask_eliminate_pipeline;
		VkPipeline                                fmask_decompress_pipeline;
		VkPipeline                                dcc_decompress_pipeline;
		VkRenderPass                              pass;

		VkDescriptorSetLayout                     dcc_decompress_compute_ds_layout;
		VkPipelineLayout                          dcc_decompress_compute_p_layout;
		VkPipeline                                dcc_decompress_compute_pipeline;
	} fast_clear_flush;

	struct {
		VkPipelineLayout fill_p_layout;
		VkPipelineLayout copy_p_layout;
		VkDescriptorSetLayout fill_ds_layout;
		VkDescriptorSetLayout copy_ds_layout;
		VkPipeline fill_pipeline;
		VkPipeline copy_pipeline;
	} buffer;

	struct {
		VkDescriptorSetLayout ds_layout;
		VkPipelineLayout p_layout;
		VkPipeline occlusion_query_pipeline;
		VkPipeline pipeline_statistics_query_pipeline;
	} query;
};

/* queue types */
#define RADV_QUEUE_GENERAL 0
#define RADV_QUEUE_COMPUTE 1
#define RADV_QUEUE_TRANSFER 2

#define RADV_MAX_QUEUE_FAMILIES 3

enum ring_type radv_queue_family_to_ring(int f);

struct radv_queue {
	VK_LOADER_DATA                              _loader_data;
	struct radv_device *                         device;
	struct radeon_winsys_ctx                    *hw_ctx;
	enum radeon_ctx_priority                     priority;
	uint32_t queue_family_index;
	int queue_idx;
	VkDeviceQueueCreateFlags flags;

	uint32_t scratch_size;
	uint32_t compute_scratch_size;
	uint32_t esgs_ring_size;
	uint32_t gsvs_ring_size;
	bool has_tess_rings;
	bool has_sample_positions;

	struct radeon_winsys_bo *scratch_bo;
	struct radeon_winsys_bo *descriptor_bo;
	struct radeon_winsys_bo *compute_scratch_bo;
	struct radeon_winsys_bo *esgs_ring_bo;
	struct radeon_winsys_bo *gsvs_ring_bo;
	struct radeon_winsys_bo *tess_rings_bo;
	struct radeon_winsys_cs *initial_preamble_cs;
	struct radeon_winsys_cs *initial_full_flush_preamble_cs;
	struct radeon_winsys_cs *continue_preamble_cs;
};

struct radv_device {
	VK_LOADER_DATA                              _loader_data;

	VkAllocationCallbacks                       alloc;

	struct radv_instance *                       instance;
	struct radeon_winsys *ws;

	struct radv_meta_state                       meta_state;

	struct radv_queue *queues[RADV_MAX_QUEUE_FAMILIES];
	int queue_count[RADV_MAX_QUEUE_FAMILIES];
	struct radeon_winsys_cs *empty_cs[RADV_MAX_QUEUE_FAMILIES];

	bool always_use_syncobj;
	bool llvm_supports_spill;
	bool has_distributed_tess;
	bool pbb_allowed;
	bool dfsm_allowed;
	uint32_t tess_offchip_block_dw_size;
	uint32_t scratch_waves;
	uint32_t dispatch_initiator;

	uint32_t gs_table_depth;

	/* MSAA sample locations.
	 * The first index is the sample index.
	 * The second index is the coordinate: X, Y. */
	float sample_locations_1x[1][2];
	float sample_locations_2x[2][2];
	float sample_locations_4x[4][2];
	float sample_locations_8x[8][2];
	float sample_locations_16x[16][2];

	/* CIK and later */
	uint32_t gfx_init_size_dw;
	struct radeon_winsys_bo                      *gfx_init;

	struct radeon_winsys_bo                      *trace_bo;
	uint32_t                                     *trace_id_ptr;

	/* Whether to keep shader debug info, for tracing or VK_AMD_shader_info */
	bool                                         keep_shader_info;

	struct radv_physical_device                  *physical_device;

	/* Backup in-memory cache to be used if the app doesn't provide one */
	struct radv_pipeline_cache *                mem_cache;

	/*
	 * use different counters so MSAA MRTs get consecutive surface indices,
	 * even if MASK is allocated in between.
	 */
	uint32_t image_mrt_offset_counter;
	uint32_t fmask_mrt_offset_counter;
	struct list_head shader_slabs;
	mtx_t shader_slab_mutex;

	/* For detecting VM faults reported by dmesg. */
	uint64_t dmesg_timestamp;

	struct radv_device_extension_table enabled_extensions;
};

struct radv_device_memory {
	struct radeon_winsys_bo                      *bo;
	/* for dedicated allocations */
	struct radv_image                            *image;
	struct radv_buffer                           *buffer;
	uint32_t                                     type_index;
	VkDeviceSize                                 map_size;
	void *                                       map;
	void *                                       user_ptr;
};


struct radv_descriptor_range {
	uint64_t va;
	uint32_t size;
};

struct radv_descriptor_set {
	const struct radv_descriptor_set_layout *layout;
	uint32_t size;

	struct radeon_winsys_bo *bo;
	uint64_t va;
	uint32_t *mapped_ptr;
	struct radv_descriptor_range *dynamic_descriptors;

	struct radeon_winsys_bo *descriptors[0];
};

struct radv_push_descriptor_set
{
	struct radv_descriptor_set set;
	uint32_t capacity;
};

struct radv_descriptor_pool_entry {
	uint32_t offset;
	uint32_t size;
	struct radv_descriptor_set *set;
};

struct radv_descriptor_pool {
	struct radeon_winsys_bo *bo;
	uint8_t *mapped_ptr;
	uint64_t current_offset;
	uint64_t size;

	uint8_t *host_memory_base;
	uint8_t *host_memory_ptr;
	uint8_t *host_memory_end;

	uint32_t entry_count;
	uint32_t max_entry_count;
	struct radv_descriptor_pool_entry entries[0];
};

struct radv_descriptor_update_template_entry {
	VkDescriptorType descriptor_type;

	/* The number of descriptors to update */
	uint32_t descriptor_count;

	/* Into mapped_ptr or dynamic_descriptors, in units of the respective array */
	uint32_t dst_offset;

	/* In dwords. Not valid/used for dynamic descriptors */
	uint32_t dst_stride;

	uint32_t buffer_offset;

	/* Only valid for combined image samplers and samplers */
	uint16_t has_sampler;

	/* In bytes */
	size_t src_offset;
	size_t src_stride;

	/* For push descriptors */
	const uint32_t *immutable_samplers;
};

struct radv_descriptor_update_template {
	uint32_t entry_count;
	VkPipelineBindPoint bind_point;
	struct radv_descriptor_update_template_entry entry[0];
};

struct radv_buffer {
	VkDeviceSize                                 size;

	VkBufferUsageFlags                           usage;
	VkBufferCreateFlags                          flags;

	/* Set when bound */
	struct radeon_winsys_bo *                      bo;
	VkDeviceSize                                 offset;

	bool shareable;
};

enum radv_dynamic_state_bits {
	RADV_DYNAMIC_VIEWPORT             = 1 << 0,
	RADV_DYNAMIC_SCISSOR              = 1 << 1,
	RADV_DYNAMIC_LINE_WIDTH           = 1 << 2,
	RADV_DYNAMIC_DEPTH_BIAS           = 1 << 3,
	RADV_DYNAMIC_BLEND_CONSTANTS      = 1 << 4,
	RADV_DYNAMIC_DEPTH_BOUNDS         = 1 << 5,
	RADV_DYNAMIC_STENCIL_COMPARE_MASK = 1 << 6,
	RADV_DYNAMIC_STENCIL_WRITE_MASK   = 1 << 7,
	RADV_DYNAMIC_STENCIL_REFERENCE    = 1 << 8,
	RADV_DYNAMIC_DISCARD_RECTANGLE    = 1 << 9,
	RADV_DYNAMIC_ALL                  = (1 << 10) - 1,
};

enum radv_cmd_dirty_bits {
	/* Keep the dynamic state dirty bits in sync with
	 * enum radv_dynamic_state_bits */
	RADV_CMD_DIRTY_DYNAMIC_VIEWPORT                  = 1 << 0,
	RADV_CMD_DIRTY_DYNAMIC_SCISSOR                   = 1 << 1,
	RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH                = 1 << 2,
	RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS                = 1 << 3,
	RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS           = 1 << 4,
	RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS              = 1 << 5,
	RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK      = 1 << 6,
	RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK        = 1 << 7,
	RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE         = 1 << 8,
	RADV_CMD_DIRTY_DYNAMIC_DISCARD_RECTANGLE         = 1 << 9,
	RADV_CMD_DIRTY_DYNAMIC_ALL                       = (1 << 10) - 1,
	RADV_CMD_DIRTY_PIPELINE                          = 1 << 10,
	RADV_CMD_DIRTY_INDEX_BUFFER                      = 1 << 11,
	RADV_CMD_DIRTY_FRAMEBUFFER                       = 1 << 12,
	RADV_CMD_DIRTY_VERTEX_BUFFER                     = 1 << 13,
};

enum radv_cmd_flush_bits {
	RADV_CMD_FLAG_INV_ICACHE = 1 << 0,
	/* SMEM L1, other names: KCACHE, constant cache, DCACHE, data cache */
	RADV_CMD_FLAG_INV_SMEM_L1 = 1 << 1,
	/* VMEM L1 can optionally be bypassed (GLC=1). Other names: TC L1 */
	RADV_CMD_FLAG_INV_VMEM_L1 = 1 << 2,
	/* Used by everything except CB/DB, can be bypassed (SLC=1). Other names: TC L2 */
	RADV_CMD_FLAG_INV_GLOBAL_L2 = 1 << 3,
	/* Same as above, but only writes back and doesn't invalidate */
	RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2 = 1 << 4,
	/* Framebuffer caches */
	RADV_CMD_FLAG_FLUSH_AND_INV_CB_META = 1 << 5,
	RADV_CMD_FLAG_FLUSH_AND_INV_DB_META = 1 << 6,
	RADV_CMD_FLAG_FLUSH_AND_INV_DB = 1 << 7,
	RADV_CMD_FLAG_FLUSH_AND_INV_CB = 1 << 8,
	/* Engine synchronization. */
	RADV_CMD_FLAG_VS_PARTIAL_FLUSH = 1 << 9,
	RADV_CMD_FLAG_PS_PARTIAL_FLUSH = 1 << 10,
	RADV_CMD_FLAG_CS_PARTIAL_FLUSH = 1 << 11,
	RADV_CMD_FLAG_VGT_FLUSH        = 1 << 12,

	RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER = (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					      RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
					      RADV_CMD_FLAG_FLUSH_AND_INV_DB |
					      RADV_CMD_FLAG_FLUSH_AND_INV_DB_META)
};

struct radv_vertex_binding {
	struct radv_buffer *                          buffer;
	VkDeviceSize                                 offset;
};

struct radv_viewport_state {
	uint32_t                                          count;
	VkViewport                                        viewports[MAX_VIEWPORTS];
};

struct radv_scissor_state {
	uint32_t                                          count;
	VkRect2D                                          scissors[MAX_SCISSORS];
};

struct radv_discard_rectangle_state {
	uint32_t                                          count;
	VkRect2D                                          rectangles[MAX_DISCARD_RECTANGLES];
};

struct radv_dynamic_state {
	/**
	 * Bitmask of (1 << VK_DYNAMIC_STATE_*).
	 * Defines the set of saved dynamic state.
	 */
	uint32_t mask;

	struct radv_viewport_state                        viewport;

	struct radv_scissor_state                         scissor;

	float                                        line_width;

	struct {
		float                                     bias;
		float                                     clamp;
		float                                     slope;
	} depth_bias;

	float                                        blend_constants[4];

	struct {
		float                                     min;
		float                                     max;
	} depth_bounds;

	struct {
		uint32_t                                  front;
		uint32_t                                  back;
	} stencil_compare_mask;

	struct {
		uint32_t                                  front;
		uint32_t                                  back;
	} stencil_write_mask;

	struct {
		uint32_t                                  front;
		uint32_t                                  back;
	} stencil_reference;

	struct radv_discard_rectangle_state               discard_rectangle;
};

extern const struct radv_dynamic_state default_dynamic_state;

const char *
radv_get_debug_option_name(int id);

const char *
radv_get_perftest_option_name(int id);

/**
 * Attachment state when recording a renderpass instance.
 *
 * The clear value is valid only if there exists a pending clear.
 */
struct radv_attachment_state {
	VkImageAspectFlags                           pending_clear_aspects;
	uint32_t                                     cleared_views;
	VkClearValue                                 clear_value;
	VkImageLayout                                current_layout;
};

struct radv_descriptor_state {
	struct radv_descriptor_set *sets[MAX_SETS];
	uint32_t dirty;
	uint32_t valid;
	struct radv_push_descriptor_set push_set;
	bool push_dirty;
};

struct radv_cmd_state {
	/* Vertex descriptors */
	bool                                          vb_prefetch_dirty;
	uint64_t                                      vb_va;
	unsigned                                      vb_size;

	bool predicating;
	uint32_t                                      dirty;

	struct radv_pipeline *                        pipeline;
	struct radv_pipeline *                        emitted_pipeline;
	struct radv_pipeline *                        compute_pipeline;
	struct radv_pipeline *                        emitted_compute_pipeline;
	struct radv_framebuffer *                     framebuffer;
	struct radv_render_pass *                     pass;
	const struct radv_subpass *                         subpass;
	struct radv_dynamic_state                     dynamic;
	struct radv_attachment_state *                attachments;
	VkRect2D                                     render_area;

	/* Index buffer */
	struct radv_buffer                           *index_buffer;
	uint64_t                                     index_offset;
	uint32_t                                     index_type;
	uint32_t                                     max_index_count;
	uint64_t                                     index_va;
	int32_t                                      last_index_type;

	int32_t                                      last_primitive_reset_en;
	uint32_t                                     last_primitive_reset_index;
	enum radv_cmd_flush_bits                     flush_bits;
	unsigned                                     active_occlusion_queries;
	float					     offset_scale;
	uint32_t                                      trace_id;
	uint32_t                                      last_ia_multi_vgt_param;

	uint32_t last_num_instances;
	uint32_t last_first_instance;
	uint32_t last_vertex_offset;
};

struct radv_cmd_pool {
	VkAllocationCallbacks                        alloc;
	struct list_head                             cmd_buffers;
	struct list_head                             free_cmd_buffers;
	uint32_t queue_family_index;
};

struct radv_cmd_buffer_upload {
	uint8_t *map;
	unsigned offset;
	uint64_t size;
	struct radeon_winsys_bo *upload_bo;
	struct list_head list;
};

enum radv_cmd_buffer_status {
	RADV_CMD_BUFFER_STATUS_INVALID,
	RADV_CMD_BUFFER_STATUS_INITIAL,
	RADV_CMD_BUFFER_STATUS_RECORDING,
	RADV_CMD_BUFFER_STATUS_EXECUTABLE,
	RADV_CMD_BUFFER_STATUS_PENDING,
};

struct radv_cmd_buffer {
	VK_LOADER_DATA                               _loader_data;

	struct radv_device *                          device;

	struct radv_cmd_pool *                        pool;
	struct list_head                             pool_link;

	VkCommandBufferUsageFlags                    usage_flags;
	VkCommandBufferLevel                         level;
	enum radv_cmd_buffer_status status;
	struct radeon_winsys_cs *cs;
	struct radv_cmd_state state;
	struct radv_vertex_binding                   vertex_bindings[MAX_VBS];
	uint32_t queue_family_index;

	uint8_t push_constants[MAX_PUSH_CONSTANTS_SIZE];
	uint32_t dynamic_buffers[4 * MAX_DYNAMIC_BUFFERS];
	VkShaderStageFlags push_constant_stages;
	struct radv_descriptor_set meta_push_descriptors;

	struct radv_descriptor_state descriptors[VK_PIPELINE_BIND_POINT_RANGE_SIZE];

	struct radv_cmd_buffer_upload upload;

	uint32_t scratch_size_needed;
	uint32_t compute_scratch_size_needed;
	uint32_t esgs_ring_size_needed;
	uint32_t gsvs_ring_size_needed;
	bool tess_rings_needed;
	bool sample_positions_needed;

	VkResult record_result;

	int ring_offsets_idx; /* just used for verification */
	uint32_t gfx9_fence_offset;
	struct radeon_winsys_bo *gfx9_fence_bo;
	uint32_t gfx9_fence_idx;

	/**
	 * Whether a query pool has been resetted and we have to flush caches.
	 */
	bool pending_reset_query;
};

struct radv_image;

bool radv_cmd_buffer_uses_mec(struct radv_cmd_buffer *cmd_buffer);

void si_init_compute(struct radv_cmd_buffer *cmd_buffer);
void si_init_config(struct radv_cmd_buffer *cmd_buffer);

void cik_create_gfx_config(struct radv_device *device);

void si_write_viewport(struct radeon_winsys_cs *cs, int first_vp,
		       int count, const VkViewport *viewports);
void si_write_scissors(struct radeon_winsys_cs *cs, int first,
		       int count, const VkRect2D *scissors,
		       const VkViewport *viewports, bool can_use_guardband);
uint32_t si_get_ia_multi_vgt_param(struct radv_cmd_buffer *cmd_buffer,
				   bool instanced_draw, bool indirect_draw,
				   uint32_t draw_vertex_count);
void si_cs_emit_write_event_eop(struct radeon_winsys_cs *cs,
				bool predicated,
				enum chip_class chip_class,
				bool is_mec,
				unsigned event, unsigned event_flags,
				unsigned data_sel,
				uint64_t va,
				uint32_t old_fence,
				uint32_t new_fence);

void si_emit_wait_fence(struct radeon_winsys_cs *cs,
			bool predicated,
			uint64_t va, uint32_t ref,
			uint32_t mask);
void si_cs_emit_cache_flush(struct radeon_winsys_cs *cs,
			    enum chip_class chip_class,
			    uint32_t *fence_ptr, uint64_t va,
			    bool is_mec,
			    enum radv_cmd_flush_bits flush_bits);
void si_emit_cache_flush(struct radv_cmd_buffer *cmd_buffer);
void si_emit_set_predication_state(struct radv_cmd_buffer *cmd_buffer, uint64_t va);
void si_cp_dma_buffer_copy(struct radv_cmd_buffer *cmd_buffer,
			   uint64_t src_va, uint64_t dest_va,
			   uint64_t size);
void si_cp_dma_prefetch(struct radv_cmd_buffer *cmd_buffer, uint64_t va,
                        unsigned size);
void si_cp_dma_clear_buffer(struct radv_cmd_buffer *cmd_buffer, uint64_t va,
			    uint64_t size, unsigned value);
void radv_set_db_count_control(struct radv_cmd_buffer *cmd_buffer);
bool
radv_cmd_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer,
			     unsigned size,
			     unsigned alignment,
			     unsigned *out_offset,
			     void **ptr);
void
radv_cmd_buffer_set_subpass(struct radv_cmd_buffer *cmd_buffer,
			    const struct radv_subpass *subpass,
			    bool transitions);
bool
radv_cmd_buffer_upload_data(struct radv_cmd_buffer *cmd_buffer,
			    unsigned size, unsigned alignmnet,
			    const void *data, unsigned *out_offset);

void radv_cmd_buffer_clear_subpass(struct radv_cmd_buffer *cmd_buffer);
void radv_cmd_buffer_resolve_subpass(struct radv_cmd_buffer *cmd_buffer);
void radv_cmd_buffer_resolve_subpass_cs(struct radv_cmd_buffer *cmd_buffer);
void radv_cmd_buffer_resolve_subpass_fs(struct radv_cmd_buffer *cmd_buffer);
void radv_cayman_emit_msaa_sample_locs(struct radeon_winsys_cs *cs, int nr_samples);
unsigned radv_cayman_get_maxdist(int log_samples);
void radv_device_init_msaa(struct radv_device *device);
void radv_set_depth_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_image *image,
			       VkClearDepthStencilValue ds_clear_value,
			       VkImageAspectFlags aspects);
void radv_set_color_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_image *image,
			       int idx,
			       uint32_t color_values[2]);
void radv_set_dcc_need_cmask_elim_pred(struct radv_cmd_buffer *cmd_buffer,
				       struct radv_image *image,
				       bool value);
uint32_t radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer,
			  struct radeon_winsys_bo *bo,
			  uint64_t offset, uint64_t size, uint32_t value);
void radv_cmd_buffer_trace_emit(struct radv_cmd_buffer *cmd_buffer);
bool radv_get_memory_fd(struct radv_device *device,
			struct radv_device_memory *memory,
			int *pFD);

static inline struct radv_descriptor_state *
radv_get_descriptors_state(struct radv_cmd_buffer *cmd_buffer,
			   VkPipelineBindPoint bind_point)
{
	assert(bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ||
	       bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);
	return &cmd_buffer->descriptors[bind_point];
}

/*
 * Takes x,y,z as exact numbers of invocations, instead of blocks.
 *
 * Limitations: Can't call normal dispatch functions without binding or rebinding
 *              the compute pipeline.
 */
void radv_unaligned_dispatch(
	struct radv_cmd_buffer                      *cmd_buffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z);

struct radv_event {
	struct radeon_winsys_bo *bo;
	uint64_t *map;
};

struct radv_shader_module;

#define RADV_HASH_SHADER_IS_GEOM_COPY_SHADER (1 << 0)
#define RADV_HASH_SHADER_SISCHED             (1 << 1)
#define RADV_HASH_SHADER_UNSAFE_MATH         (1 << 2)
void
radv_hash_shaders(unsigned char *hash,
		  const VkPipelineShaderStageCreateInfo **stages,
		  const struct radv_pipeline_layout *layout,
		  const struct radv_pipeline_key *key,
		  uint32_t flags);

static inline gl_shader_stage
vk_to_mesa_shader_stage(VkShaderStageFlagBits vk_stage)
{
	assert(__builtin_popcount(vk_stage) == 1);
	return ffs(vk_stage) - 1;
}

static inline VkShaderStageFlagBits
mesa_to_vk_shader_stage(gl_shader_stage mesa_stage)
{
	return (1 << mesa_stage);
}

#define RADV_STAGE_MASK ((1 << MESA_SHADER_STAGES) - 1)

#define radv_foreach_stage(stage, stage_bits)				\
	for (gl_shader_stage stage,					\
		     __tmp = (gl_shader_stage)((stage_bits) & RADV_STAGE_MASK);	\
	     stage = __builtin_ffs(__tmp) - 1, __tmp;			\
	     __tmp &= ~(1 << (stage)))

unsigned radv_format_meta_fs_key(VkFormat format);

struct radv_multisample_state {
	uint32_t db_eqaa;
	uint32_t pa_sc_line_cntl;
	uint32_t pa_sc_mode_cntl_0;
	uint32_t pa_sc_mode_cntl_1;
	uint32_t pa_sc_aa_config;
	uint32_t pa_sc_aa_mask[2];
	unsigned num_samples;
};

struct radv_prim_vertex_count {
	uint8_t min;
	uint8_t incr;
};

struct radv_vertex_elements_info {
	uint32_t rsrc_word3[MAX_VERTEX_ATTRIBS];
	uint32_t format_size[MAX_VERTEX_ATTRIBS];
	uint32_t binding[MAX_VERTEX_ATTRIBS];
	uint32_t offset[MAX_VERTEX_ATTRIBS];
	uint32_t count;
};

struct radv_ia_multi_vgt_param_helpers {
	uint32_t base;
	bool partial_es_wave;
	uint8_t primgroup_size;
	bool wd_switch_on_eop;
	bool ia_switch_on_eoi;
	bool partial_vs_wave;
};

#define SI_GS_PER_ES 128

struct radv_pipeline {
	struct radv_device *                          device;
	struct radv_dynamic_state                     dynamic_state;

	struct radv_pipeline_layout *                 layout;

	bool					     need_indirect_descriptor_sets;
	struct radv_shader_variant *                 shaders[MESA_SHADER_STAGES];
	struct radv_shader_variant *gs_copy_shader;
	VkShaderStageFlags                           active_stages;

	struct radeon_winsys_cs                      cs;

	struct radv_vertex_elements_info             vertex_elements;

	uint32_t                                     binding_stride[MAX_VBS];

	uint32_t user_data_0[MESA_SHADER_STAGES];
	union {
		struct {
			struct radv_multisample_state ms;
			uint32_t spi_baryc_cntl;
			bool prim_restart_enable;
			unsigned esgs_ring_size;
			unsigned gsvs_ring_size;
			uint32_t vtx_base_sgpr;
			struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param;
			uint8_t vtx_emit_num;
			struct radv_prim_vertex_count prim_vertex_count;
 			bool can_use_guardband;
			uint32_t needed_dynamic_state;
		} graphics;
	};

	unsigned max_waves;
	unsigned scratch_bytes_per_wave;
};

static inline bool radv_pipeline_has_gs(const struct radv_pipeline *pipeline)
{
	return pipeline->shaders[MESA_SHADER_GEOMETRY] ? true : false;
}

static inline bool radv_pipeline_has_tess(const struct radv_pipeline *pipeline)
{
	return pipeline->shaders[MESA_SHADER_TESS_CTRL] ? true : false;
}

struct radv_userdata_info *radv_lookup_user_sgpr(struct radv_pipeline *pipeline,
						 gl_shader_stage stage,
						 int idx);

struct radv_shader_variant *radv_get_vertex_shader(struct radv_pipeline *pipeline);

struct radv_graphics_pipeline_create_info {
	bool use_rectlist;
	bool db_depth_clear;
	bool db_stencil_clear;
	bool db_depth_disable_expclear;
	bool db_stencil_disable_expclear;
	bool db_flush_depth_inplace;
	bool db_flush_stencil_inplace;
	bool db_resummarize;
	uint32_t custom_blend_mode;
};

VkResult
radv_graphics_pipeline_create(VkDevice device,
			      VkPipelineCache cache,
			      const VkGraphicsPipelineCreateInfo *pCreateInfo,
			      const struct radv_graphics_pipeline_create_info *extra,
			      const VkAllocationCallbacks *alloc,
			      VkPipeline *pPipeline);

struct vk_format_description;
uint32_t radv_translate_buffer_dataformat(const struct vk_format_description *desc,
					  int first_non_void);
uint32_t radv_translate_buffer_numformat(const struct vk_format_description *desc,
					 int first_non_void);
uint32_t radv_translate_colorformat(VkFormat format);
uint32_t radv_translate_color_numformat(VkFormat format,
					const struct vk_format_description *desc,
					int first_non_void);
uint32_t radv_colorformat_endian_swap(uint32_t colorformat);
unsigned radv_translate_colorswap(VkFormat format, bool do_endian_swap);
uint32_t radv_translate_dbformat(VkFormat format);
uint32_t radv_translate_tex_dataformat(VkFormat format,
				       const struct vk_format_description *desc,
				       int first_non_void);
uint32_t radv_translate_tex_numformat(VkFormat format,
				      const struct vk_format_description *desc,
				      int first_non_void);
bool radv_format_pack_clear_color(VkFormat format,
				  uint32_t clear_vals[2],
				  VkClearColorValue *value);
bool radv_is_colorbuffer_format_supported(VkFormat format, bool *blendable);
bool radv_dcc_formats_compatible(VkFormat format1,
                                 VkFormat format2);

struct radv_fmask_info {
	uint64_t offset;
	uint64_t size;
	unsigned alignment;
	unsigned pitch_in_pixels;
	unsigned bank_height;
	unsigned slice_tile_max;
	unsigned tile_mode_index;
	unsigned tile_swizzle;
};

struct radv_cmask_info {
	uint64_t offset;
	uint64_t size;
	unsigned alignment;
	unsigned slice_tile_max;
};

struct radv_image {
	VkImageType type;
	/* The original VkFormat provided by the client.  This may not match any
	 * of the actual surface formats.
	 */
	VkFormat vk_format;
	VkImageAspectFlags aspects;
	VkImageUsageFlags usage; /**< Superset of VkImageCreateInfo::usage. */
	struct ac_surf_info info;
	VkImageTiling tiling; /** VkImageCreateInfo::tiling */
	VkImageCreateFlags flags; /** VkImageCreateInfo::flags */

	VkDeviceSize size;
	uint32_t alignment;

	unsigned queue_family_mask;
	bool exclusive;
	bool shareable;

	/* Set when bound */
	struct radeon_winsys_bo *bo;
	VkDeviceSize offset;
	uint64_t dcc_offset;
	uint64_t htile_offset;
	bool tc_compatible_htile;
	struct radeon_surf surface;

	struct radv_fmask_info fmask;
	struct radv_cmask_info cmask;
	uint64_t clear_value_offset;
	uint64_t dcc_pred_offset;

	/* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
	VkDeviceMemory owned_memory;
};

/* Whether the image has a htile that is known consistent with the contents of
 * the image. */
bool radv_layout_has_htile(const struct radv_image *image,
                           VkImageLayout layout,
                           unsigned queue_mask);

/* Whether the image has a htile  that is known consistent with the contents of
 * the image and is allowed to be in compressed form.
 *
 * If this is false reads that don't use the htile should be able to return
 * correct results.
 */
bool radv_layout_is_htile_compressed(const struct radv_image *image,
                                     VkImageLayout layout,
                                     unsigned queue_mask);

bool radv_layout_can_fast_clear(const struct radv_image *image,
			        VkImageLayout layout,
			        unsigned queue_mask);

bool radv_layout_dcc_compressed(const struct radv_image *image,
			        VkImageLayout layout,
			        unsigned queue_mask);

static inline bool
radv_vi_dcc_enabled(const struct radv_image *image, unsigned level)
{
	return image->surface.dcc_size && level < image->surface.num_dcc_levels;
}

static inline bool
radv_htile_enabled(const struct radv_image *image, unsigned level)
{
	return image->surface.htile_size && level == 0;
}

unsigned radv_image_queue_family_mask(const struct radv_image *image, uint32_t family, uint32_t queue_family);

static inline uint32_t
radv_get_layerCount(const struct radv_image *image,
		    const VkImageSubresourceRange *range)
{
	return range->layerCount == VK_REMAINING_ARRAY_LAYERS ?
		image->info.array_size - range->baseArrayLayer : range->layerCount;
}

static inline uint32_t
radv_get_levelCount(const struct radv_image *image,
		    const VkImageSubresourceRange *range)
{
	return range->levelCount == VK_REMAINING_MIP_LEVELS ?
		image->info.levels - range->baseMipLevel : range->levelCount;
}

struct radeon_bo_metadata;
void
radv_init_metadata(struct radv_device *device,
		   struct radv_image *image,
		   struct radeon_bo_metadata *metadata);

struct radv_image_view {
	struct radv_image *image; /**< VkImageViewCreateInfo::image */
	struct radeon_winsys_bo *bo;

	VkImageViewType type;
	VkImageAspectFlags aspect_mask;
	VkFormat vk_format;
	uint32_t base_layer;
	uint32_t layer_count;
	uint32_t base_mip;
	uint32_t level_count;
	VkExtent3D extent; /**< Extent of VkImageViewCreateInfo::baseMipLevel. */

	uint32_t descriptor[16];

	/* Descriptor for use as a storage image as opposed to a sampled image.
	 * This has a few differences for cube maps (e.g. type).
	 */
	uint32_t storage_descriptor[16];
};

struct radv_image_create_info {
	const VkImageCreateInfo *vk_info;
	bool scanout;
	bool no_metadata_planes;
};

VkResult radv_image_create(VkDevice _device,
			   const struct radv_image_create_info *info,
			   const VkAllocationCallbacks* alloc,
			   VkImage *pImage);

VkResult
radv_image_from_gralloc(VkDevice device_h,
                       const VkImageCreateInfo *base_info,
                       const VkNativeBufferANDROID *gralloc_info,
                       const VkAllocationCallbacks *alloc,
                       VkImage *out_image_h);

void radv_image_view_init(struct radv_image_view *view,
			  struct radv_device *device,
			  const VkImageViewCreateInfo* pCreateInfo);

struct radv_buffer_view {
	struct radeon_winsys_bo *bo;
	VkFormat vk_format;
	uint64_t range; /**< VkBufferViewCreateInfo::range */
	uint32_t state[4];
};
void radv_buffer_view_init(struct radv_buffer_view *view,
			   struct radv_device *device,
			   const VkBufferViewCreateInfo* pCreateInfo);

static inline struct VkExtent3D
radv_sanitize_image_extent(const VkImageType imageType,
			   const struct VkExtent3D imageExtent)
{
	switch (imageType) {
	case VK_IMAGE_TYPE_1D:
		return (VkExtent3D) { imageExtent.width, 1, 1 };
	case VK_IMAGE_TYPE_2D:
		return (VkExtent3D) { imageExtent.width, imageExtent.height, 1 };
	case VK_IMAGE_TYPE_3D:
		return imageExtent;
	default:
		unreachable("invalid image type");
	}
}

static inline struct VkOffset3D
radv_sanitize_image_offset(const VkImageType imageType,
			   const struct VkOffset3D imageOffset)
{
	switch (imageType) {
	case VK_IMAGE_TYPE_1D:
		return (VkOffset3D) { imageOffset.x, 0, 0 };
	case VK_IMAGE_TYPE_2D:
		return (VkOffset3D) { imageOffset.x, imageOffset.y, 0 };
	case VK_IMAGE_TYPE_3D:
		return imageOffset;
	default:
		unreachable("invalid image type");
	}
}

static inline bool
radv_image_extent_compare(const struct radv_image *image,
			  const VkExtent3D *extent)
{
	if (extent->width != image->info.width ||
	    extent->height != image->info.height ||
	    extent->depth != image->info.depth)
		return false;
	return true;
}

struct radv_sampler {
	uint32_t state[4];
};

struct radv_color_buffer_info {
	uint64_t cb_color_base;
	uint64_t cb_color_cmask;
	uint64_t cb_color_fmask;
	uint64_t cb_dcc_base;
	uint32_t cb_color_pitch;
	uint32_t cb_color_slice;
	uint32_t cb_color_view;
	uint32_t cb_color_info;
	uint32_t cb_color_attrib;
	uint32_t cb_color_attrib2;
	uint32_t cb_dcc_control;
	uint32_t cb_color_cmask_slice;
	uint32_t cb_color_fmask_slice;
};

struct radv_ds_buffer_info {
	uint64_t db_z_read_base;
	uint64_t db_stencil_read_base;
	uint64_t db_z_write_base;
	uint64_t db_stencil_write_base;
	uint64_t db_htile_data_base;
	uint32_t db_depth_info;
	uint32_t db_z_info;
	uint32_t db_stencil_info;
	uint32_t db_depth_view;
	uint32_t db_depth_size;
	uint32_t db_depth_slice;
	uint32_t db_htile_surface;
	uint32_t pa_su_poly_offset_db_fmt_cntl;
	uint32_t db_z_info2;
	uint32_t db_stencil_info2;
	float offset_scale;
};

struct radv_attachment_info {
	union {
		struct radv_color_buffer_info cb;
		struct radv_ds_buffer_info ds;
	};
	struct radv_image_view *attachment;
};

struct radv_framebuffer {
	uint32_t                                     width;
	uint32_t                                     height;
	uint32_t                                     layers;

	uint32_t                                     attachment_count;
	struct radv_attachment_info                  attachments[0];
};

struct radv_subpass_barrier {
	VkPipelineStageFlags src_stage_mask;
	VkAccessFlags        src_access_mask;
	VkAccessFlags        dst_access_mask;
};

struct radv_subpass {
	uint32_t                                     input_count;
	uint32_t                                     color_count;
	VkAttachmentReference *                      input_attachments;
	VkAttachmentReference *                      color_attachments;
	VkAttachmentReference *                      resolve_attachments;
	VkAttachmentReference                        depth_stencil_attachment;

	/** Subpass has at least one resolve attachment */
	bool                                         has_resolve;

	struct radv_subpass_barrier                  start_barrier;

	uint32_t                                     view_mask;
};

struct radv_render_pass_attachment {
	VkFormat                                     format;
	uint32_t                                     samples;
	VkAttachmentLoadOp                           load_op;
	VkAttachmentLoadOp                           stencil_load_op;
	VkImageLayout                                initial_layout;
	VkImageLayout                                final_layout;
	uint32_t                                     view_mask;
};

struct radv_render_pass {
	uint32_t                                     attachment_count;
	uint32_t                                     subpass_count;
	VkAttachmentReference *                      subpass_attachments;
	struct radv_render_pass_attachment *         attachments;
	struct radv_subpass_barrier                  end_barrier;
	struct radv_subpass                          subpasses[0];
};

VkResult radv_device_init_meta(struct radv_device *device);
void radv_device_finish_meta(struct radv_device *device);

struct radv_query_pool {
	struct radeon_winsys_bo *bo;
	uint32_t stride;
	uint32_t availability_offset;
	uint64_t size;
	char *ptr;
	VkQueryType type;
	uint32_t pipeline_stats_mask;
};

struct radv_semaphore {
	/* use a winsys sem for non-exportable */
	struct radeon_winsys_sem *sem;
	uint32_t syncobj;
	uint32_t temp_syncobj;
};

VkResult radv_alloc_sem_info(struct radv_winsys_sem_info *sem_info,
			     int num_wait_sems,
			     const VkSemaphore *wait_sems,
			     int num_signal_sems,
			     const VkSemaphore *signal_sems,
			     VkFence fence);
void radv_free_sem_info(struct radv_winsys_sem_info *sem_info);

void radv_set_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			     VkPipelineBindPoint bind_point,
			     struct radv_descriptor_set *set,
			     unsigned idx);

void
radv_update_descriptor_sets(struct radv_device *device,
                            struct radv_cmd_buffer *cmd_buffer,
                            VkDescriptorSet overrideSet,
                            uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount,
                            const VkCopyDescriptorSet *pDescriptorCopies);

void
radv_update_descriptor_set_with_template(struct radv_device *device,
                                         struct radv_cmd_buffer *cmd_buffer,
                                         struct radv_descriptor_set *set,
                                         VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate,
                                         const void *pData);

void radv_meta_push_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint pipelineBindPoint,
                                   VkPipelineLayout _layout,
                                   uint32_t set,
                                   uint32_t descriptorWriteCount,
                                   const VkWriteDescriptorSet *pDescriptorWrites);

void radv_initialise_cmask(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image, uint32_t value);
void radv_initialize_dcc(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image, uint32_t value);

struct radv_fence {
	struct radeon_winsys_fence *fence;
	bool submitted;
	bool signalled;

	uint32_t syncobj;
	uint32_t temp_syncobj;
};

/* radv_nir_to_llvm.c */
struct radv_shader_variant_info;
struct radv_nir_compiler_options;

void radv_compile_gs_copy_shader(LLVMTargetMachineRef tm,
				 struct nir_shader *geom_shader,
				 struct ac_shader_binary *binary,
				 struct ac_shader_config *config,
				 struct radv_shader_variant_info *shader_info,
				 const struct radv_nir_compiler_options *option);

void radv_compile_nir_shader(LLVMTargetMachineRef tm,
			     struct ac_shader_binary *binary,
			     struct ac_shader_config *config,
			     struct radv_shader_variant_info *shader_info,
			     struct nir_shader *const *nir,
			     int nir_count,
			     const struct radv_nir_compiler_options *options);

/* radv_shader_info.h */
struct radv_shader_info;

void radv_nir_shader_info_pass(const struct nir_shader *nir,
			       const struct radv_nir_compiler_options *options,
			       struct radv_shader_info *info);

struct radeon_winsys_sem;

#define RADV_DEFINE_HANDLE_CASTS(__radv_type, __VkType)		\
								\
	static inline struct __radv_type *			\
	__radv_type ## _from_handle(__VkType _handle)		\
	{							\
		return (struct __radv_type *) _handle;		\
	}							\
								\
	static inline __VkType					\
	__radv_type ## _to_handle(struct __radv_type *_obj)	\
	{							\
		return (__VkType) _obj;				\
	}

#define RADV_DEFINE_NONDISP_HANDLE_CASTS(__radv_type, __VkType)		\
									\
	static inline struct __radv_type *				\
	__radv_type ## _from_handle(__VkType _handle)			\
	{								\
		return (struct __radv_type *)(uintptr_t) _handle;	\
	}								\
									\
	static inline __VkType						\
	__radv_type ## _to_handle(struct __radv_type *_obj)		\
	{								\
		return (__VkType)(uintptr_t) _obj;			\
	}

#define RADV_FROM_HANDLE(__radv_type, __name, __handle)			\
	struct __radv_type *__name = __radv_type ## _from_handle(__handle)

RADV_DEFINE_HANDLE_CASTS(radv_cmd_buffer, VkCommandBuffer)
RADV_DEFINE_HANDLE_CASTS(radv_device, VkDevice)
RADV_DEFINE_HANDLE_CASTS(radv_instance, VkInstance)
RADV_DEFINE_HANDLE_CASTS(radv_physical_device, VkPhysicalDevice)
RADV_DEFINE_HANDLE_CASTS(radv_queue, VkQueue)

RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_cmd_pool, VkCommandPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_buffer, VkBuffer)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_buffer_view, VkBufferView)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_pool, VkDescriptorPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set, VkDescriptorSet)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set_layout, VkDescriptorSetLayout)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_update_template, VkDescriptorUpdateTemplateKHR)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_device_memory, VkDeviceMemory)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_fence, VkFence)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_event, VkEvent)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_framebuffer, VkFramebuffer)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_image, VkImage)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_image_view, VkImageView);
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline_cache, VkPipelineCache)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline, VkPipeline)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline_layout, VkPipelineLayout)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_query_pool, VkQueryPool)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_render_pass, VkRenderPass)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_sampler, VkSampler)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_shader_module, VkShaderModule)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_semaphore, VkSemaphore)

#endif /* RADV_PRIVATE_H */
