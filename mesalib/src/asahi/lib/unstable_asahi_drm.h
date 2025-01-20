/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) The Asahi Linux Contributors
 *
 * Based on panfrost_drm.h which is
 *
 * Copyright © 2014-2018 Broadcom
 * Copyright © 2019 Collabora ltd.
 */
/* clang-format off */
#ifndef _ASAHI_DRM_H_
#define _ASAHI_DRM_H_

#include "drm-uapi/drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * The UAPI defined in this file MUST NOT BE USED. End users, DO NOT attempt to
 * use upstream Mesa with asahi kernels, it will blow up. Distro packagers, DO
 * NOT patch upstream Mesa to do the same.
 */
#define DRM_ASAHI_UNSTABLE_UABI_VERSION (0xDEADBEEF)

#define DRM_ASAHI_GET_PARAMS			0x00
#define DRM_ASAHI_VM_CREATE			0x01
#define DRM_ASAHI_VM_DESTROY			0x02
#define DRM_ASAHI_GEM_CREATE			0x03
#define DRM_ASAHI_GEM_MMAP_OFFSET		0x04
#define DRM_ASAHI_GEM_BIND			0x05
#define DRM_ASAHI_QUEUE_CREATE			0x06
#define DRM_ASAHI_QUEUE_DESTROY			0x07
#define DRM_ASAHI_SUBMIT			0x08
#define DRM_ASAHI_GET_TIME			0x09
/* TODO: Maybe merge with DRM_ASAHI_GEM_BIND? (Becomes IOWR) */
#define DRM_ASAHI_GEM_BIND_OBJECT		0x0a

/* TODO: Bump to 64, just in case? */
#define DRM_ASAHI_MAX_CLUSTERS	32

struct drm_asahi_params_global {
	__u32 unstable_uabi_version;
	__u32 pad0;

	/** @feat_compat: Compatible feature bits, from drm_asahi_feat_compat */
	__u64 feat_compat;
	/** @feat_incompat: Incompatible feature bits, from drm_asahi_feat_incompat */
	__u64 feat_incompat;

	/** @gpu_generation: GPU generation, e.g. 13 for G13G */
	__u32 gpu_generation;
	/** @gpu_variant: GPU variant as a character, e.g. 'G' for G13G */
	__u32 gpu_variant;
	/** @gpu_revision: GPU revision in BCD, e.g. 0x00 for 'A0', 0x21 for 'C1' */
	__u32 gpu_revision;
	/** @chip_id: Chip ID in BCD, e.g. 0x8103 for T8103 */
	__u32 chip_id;

	/** @num_dies: Number of dies in the SoC */
	__u32 num_dies;
	/** @num_clusters_total: Number of GPU clusters (across all dies) */
	__u32 num_clusters_total;
	/** @num_cores_per_cluster: Number of logical cores per cluster
	 *  (including inactive/nonexistent) */
	__u32 num_cores_per_cluster;
	/** @num_frags_per_cluster: Number of frags per cluster */
	__u32 num_frags_per_cluster;
	/** @num_gps_per_cluster: Number of GPs per cluster */
	__u32 num_gps_per_cluster;
	/** @num_cores_total_active: Total number of active cores (total bit weight of core_masks) */
	__u32 num_cores_total_active;
	/** @core_masks: Bitmask of present/enabled cores per cluster */
	__u64 core_masks[DRM_ASAHI_MAX_CLUSTERS];

	/** @vm_page_size: GPU VM page size */
	__u32 vm_page_size;
	/** @pad1: Padding, MBZ */
	__u32 pad1;
	/** @vm_user_start: VM user range start VMA */
	__u64 vm_user_start;
	/** @vm_user_end: VM user range end VMA */
	__u64 vm_user_end;
	/** @vm_usc_start: VM USC region start VMA (zero if flexible) */
	__u64 vm_usc_start;
	/** @vm_usc_end: VM USC region end VMA (zero if flexible) */
	__u64 vm_usc_end;
	/** @vm_kernel_min_size: Minimum kernel VMA window size within user range */
	__u64 vm_kernel_min_size;

	/** @max_syncs_per_submission: Maximum number of supported sync objects per submission */
	__u32 max_syncs_per_submission;
	/** @max_commands_per_submission: Maximum number of supported commands per submission */
	__u32 max_commands_per_submission;
	/** @max_commands_in_flight: Maximum number of commands simultaneously in flight per queue */
	/* TODO: Remove? */
	__u32 max_commands_in_flight;
	/** @max_attachments: Maximum number of attachments per command */
	__u32 max_attachments;

	/** @timer_frequency_hz: Clock frequency for timestamps */
	/* TODO: Switch to u64 */
	__u32 timer_frequency_hz;
	/** @min_frequency_khz: Minimum GPU core clock frequency */
	__u32 min_frequency_khz;
	/** @max_frequency_khz: Maximum GPU core clock frequency */
	__u32 max_frequency_khz;
	/** @max_power_mw: Maximum GPU power consumption */
	__u32 max_power_mw;

	/** @result_render_size: Result structure size for render commands */
	__u32 result_render_size;
	/** @result_compute_size: Result structure size for compute commands */
	__u32 result_compute_size;

	/** @firmware_version: GPU firmware version, as 4 integers */
	/* TODO: Do something to distinguish iOS */
	__u32 firmware_version[4];

	/** @user_timestamp_frequency_hz: Timebase frequency for user timestamps */
	__u64 user_timestamp_frequency_hz;
};

/** Compatible feature bits */
enum drm_asahi_feat_compat {
	/** GPU has soft faults enabled (for USC and texture sampling) */
	DRM_ASAHI_FEAT_SOFT_FAULTS = (1UL) << 0,
	DRM_ASAHI_FEAT_GETTIME = (1UL) << 1, /* Remove for upstream */
	DRM_ASAHI_FEAT_USER_TIMESTAMPS = (1UL) << 2,
};

/** Incompatible feature bits */
enum drm_asahi_feat_incompat {
	/** GPU requires compression for Z/S buffers */
	DRM_ASAHI_FEAT_MANDATORY_ZS_COMPRESSION = (1UL) << 0,
};

/** Get driver/GPU parameters */
struct drm_asahi_get_params {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @param: Parameter group to fetch (MBZ) */
	__u32 param_group;

	/** @pad: MBZ */
	__u32 pad;

	/** @value: User pointer to write parameter struct */
	__u64 pointer;

	/** @value: Size of user buffer, max size supported on return */
	__u64 size;
};

/** Create a GPU VM address space */
struct drm_asahi_vm_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @kernel_start: Start of the kernel-reserved address range */
	__u64 kernel_start;

	/** @kernel_end: End of the kernel-reserved address range */
	__u64 kernel_end;

	/** @value: Returned VM ID */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;
};

/** Destroy a GPU VM address space */
struct drm_asahi_vm_destroy {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @value: VM ID to be destroyed */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;
};
/** BO should be CPU-mapped as writeback, not write-combine (optimize for CPU reads) */
#define ASAHI_GEM_WRITEBACK	(1L << 0)
/** BO is private to this GPU VM (no exports) */
#define ASAHI_GEM_VM_PRIVATE	(1L << 1)

/** Destroy a GPU VM address space */
struct drm_asahi_gem_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @size: Size of the BO */
	__u64 size;

	/** @flags: BO creation flags */
	__u32 flags;

	/** @handle: VM ID to assign to the BO, if ASAHI_GEM_VM_PRIVATE is set. */
	__u32 vm_id;

	/** @handle: Returned GEM handle for the BO */
	__u32 handle;

	/** @pad: MBZ */
	__u32 pad;
};

/** Get BO mmap offset */
struct drm_asahi_gem_mmap_offset {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @handle: Handle for the object being mapped. */
	__u32 handle;

	/** @flags: Must be zero */
	__u32 flags;

	/** @offset: The fake offset to use for subsequent mmap call */
	__u64 offset;
};

/** VM_BIND operations */
enum drm_asahi_bind_op {
	/** Bind a BO to a GPU VMA range */
	ASAHI_BIND_OP_BIND = 0,
	/** Unbind a GPU VMA range */
	ASAHI_BIND_OP_UNBIND = 1,
	/** Unbind all mappings of a given BO */
	ASAHI_BIND_OP_UNBIND_ALL = 2,
};

/** Map BO with GPU read permission */
#define ASAHI_BIND_READ		(1L << 0)
/** Map BO with GPU write permission */
#define ASAHI_BIND_WRITE	(1L << 1)

/** BO VM_BIND operations */
struct drm_asahi_gem_bind {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @obj: Bind operation (enum drm_asahi_bind_op) */
	__u32 op;

	/** @flags: One or more of ASAHI_BIND_* (BIND only) */
	__u32 flags;

	/** @obj: GEM object to bind/unbind (BIND or UNBIND_ALL) */
	__u32 handle;

	/** @vm_id: The ID of the VM to operate on */
	__u32 vm_id;

	/** @offset: Offset into the object (BIND only) */
	__u64 offset;

	/** @range: Number of bytes to bind/unbind to addr (BIND or UNBIND only) */
	__u64 range;

	/** @addr: Address to bind to (BIND or UNBIND only) */
	__u64 addr;
};

/** VM_BIND operations */
enum drm_asahi_bind_object_op {
	/** Bind a BO as a special GPU object */
	ASAHI_BIND_OBJECT_OP_BIND = 0,
	/** Unbind a special GPU object */
	ASAHI_BIND_OBJECT_OP_UNBIND = 1,
};

/** Map a BO as a timestamp buffer */
#define ASAHI_BIND_OBJECT_USAGE_TIMESTAMPS	(1L << 0)

/** BO special object operations */
struct drm_asahi_gem_bind_object {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @obj: Bind operation (enum drm_asahi_bind_object_op) */
	__u32 op;

	/** @flags: One or more of ASAHI_BIND_OBJECT_* */
	__u32 flags;

	/** @obj: GEM object to bind/unbind (BIND) */
	__u32 handle;

	/** @vm_id: The ID of the VM to operate on (MBZ currently) */
	__u32 vm_id;

	/** @offset: Offset into the object (BIND only) */
	__u64 offset;

	/** @range: Number of bytes to bind/unbind (BIND only) */
	__u64 range;

	/** @addr: Object handle (out for BIND, in for UNBIND) */
	__u32 object_handle;

	/** @pad: MBZ */
	__u32 pad;
};

/** Command type */
enum drm_asahi_cmd_type {
	/** Render command (Render subqueue, Vert+Frag) */
	DRM_ASAHI_CMD_RENDER = 0,
	/** Blit command (Render subqueue, Frag only, not yet supported) */
	DRM_ASAHI_CMD_BLIT = 1,
	/** Compute command (Compute subqueue) */
	DRM_ASAHI_CMD_COMPUTE = 2,
};

/** Queue capabilities */
/* Note: this is an enum so that it can be resolved by Rust bindgen. */
enum drm_asahi_queue_cap {
	/** Supports render commands */
	DRM_ASAHI_QUEUE_CAP_RENDER	= (1UL << DRM_ASAHI_CMD_RENDER),
	/** Supports blit commands */
	DRM_ASAHI_QUEUE_CAP_BLIT	= (1UL << DRM_ASAHI_CMD_BLIT),
	/** Supports compute commands */
	DRM_ASAHI_QUEUE_CAP_COMPUTE	= (1UL << DRM_ASAHI_CMD_COMPUTE),
};

/** Create a queue */
struct drm_asahi_queue_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @flags: MBZ */
	__u32 flags;

	/** @vm_id: The ID of the VM this queue is bound to */
	__u32 vm_id;

	/** @type: Bitmask of DRM_ASAHI_QUEUE_CAP_* */
	__u32 queue_caps;

	/** @priority: Queue priority, 0-3 */
	__u32 priority;

	/** @queue_id: The returned queue ID */
	__u32 queue_id;

	/** @pad: MBZ */
	__u32 pad;
};

/** Destroy a queue */
struct drm_asahi_queue_destroy {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @queue_id: The queue ID to be destroyed */
	__u32 queue_id;

	/** @pad: MBZ */
	__u32 pad;
};

/** Sync item types */
enum drm_asahi_sync_type {
	/** Simple sync object */
	DRM_ASAHI_SYNC_SYNCOBJ = 0,
	/** Timeline sync object */
	DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ = 1,
};

/** Sync item */
struct drm_asahi_sync {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @sync_type: One of drm_asahi_sync_type */
	__u32 sync_type;

	/** @handle: The sync object handle */
	__u32 handle;

	/** @timeline_value: Timeline value for timeline sync objects */
	__u64 timeline_value;
};

/** Sub-queues within a queue */
enum drm_asahi_subqueue {
	/** Render subqueue (also blit) */
	DRM_ASAHI_SUBQUEUE_RENDER = 0,
	/** Compute subqueue */
	DRM_ASAHI_SUBQUEUE_COMPUTE = 1,
	/** Queue count, must remain multiple of 2 for struct alignment */
	DRM_ASAHI_SUBQUEUE_COUNT = 2,
};

/** Command index for no barrier */
#define DRM_ASAHI_BARRIER_NONE ~(0U)

/** Top level command structure */
struct drm_asahi_command {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @type: One of drm_asahi_cmd_type */
	__u32 cmd_type;

	/** @flags: Flags for command submission */
	__u32 flags;

	/** @cmdbuf: Pointer to the appropriate command buffer structure */
	__u64 cmd_buffer;

	/** @cmdbuf: Size of the command buffer structure */
	__u64 cmd_buffer_size;

	/** @cmdbuf: Offset into the result BO to return information about this command */
	__u64 result_offset;

	/** @cmdbuf: Size of the result data structure */
	__u64 result_size;

	/** @barriers: Array of command indices per subqueue to wait on */
	__u32 barriers[DRM_ASAHI_SUBQUEUE_COUNT];
};

/** Submit an array of commands to a queue */
struct drm_asahi_submit {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @in_syncs: An optional array of drm_asahi_sync to wait on before starting this job. */
	__u64 in_syncs;

	/** @in_syncs: An optional array of drm_asahi_sync objects to signal upon completion. */
	__u64 out_syncs;

	/** @commands: Pointer to the drm_asahi_command array of commands to submit. */
	__u64 commands;

	/** @flags: Flags for command submission (MBZ) */
	__u32 flags;

	/** @queue_id: The queue ID to be submitted to */
	__u32 queue_id;

	/** @result_handle: An optional BO handle to place result data in */
	__u32 result_handle;

	/** @in_sync_count: Number of sync objects to wait on before starting this job. */
	__u32 in_sync_count;

	/** @in_sync_count: Number of sync objects to signal upon completion of this job. */
	__u32 out_sync_count;

	/** @pad: Number of commands to be submitted */
	__u32 command_count;
};

/** An attachment definition for a shader stage */
struct drm_asahi_attachment {
	/** @pointer: Base address of the attachment */
	__u64 pointer;
	/** @size: Size of the attachment in bytes */
	__u64 size;
	/** @order: Power of 2 exponent related to attachment size (?) */
	__u32 order;
	/** @flags: MBZ */
	__u32 flags;
};

/** XXX investigate real meaning */
#define ASAHI_RENDER_NO_CLEAR_PIPELINE_TEXTURES (1UL << 0)
/** XXX investigate real meaning */
#define ASAHI_RENDER_SET_WHEN_RELOADING_Z_OR_S (1UL << 1)
/** Vertex stage shader spills */
#define ASAHI_RENDER_VERTEX_SPILLS (1UL << 2)
/** Process empty tiles through the fragment load/store */
#define ASAHI_RENDER_PROCESS_EMPTY_TILES (1UL << 3)
/** Run vertex stage on a single cluster (on multicluster GPUs) */
#define ASAHI_RENDER_NO_VERTEX_CLUSTERING (1UL << 4)
/** Enable MSAA for Z/S */
#define ASAHI_RENDER_MSAA_ZS (1UL << 5)
/** Disable preemption (XXX check) */
#define ASAHI_RENDER_NO_PREEMPTION (1UL << 6)

/** Render command submission data */
struct drm_asahi_cmd_render {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @flags: Zero or more of ASAHI_RENDER_* */
	__u64 flags;

	__u64 encoder_ptr;
	__u64 vertex_usc_base;
	__u64 fragment_usc_base;

	__u64 vertex_attachments;
	__u64 fragment_attachments;
	__u32 vertex_attachment_count;
	__u32 fragment_attachment_count;

	__u32 vertex_helper_program;
	__u32 fragment_helper_program;
	__u32 vertex_helper_cfg;
	__u32 fragment_helper_cfg;
	__u64 vertex_helper_arg;
	__u64 fragment_helper_arg;

	__u64 depth_buffer_load;
	__u64 depth_buffer_load_stride;
	__u64 depth_buffer_store;
	__u64 depth_buffer_store_stride;
	__u64 depth_buffer_partial;
	__u64 depth_buffer_partial_stride;
	__u64 depth_meta_buffer_load;
	__u64 depth_meta_buffer_load_stride;
	__u64 depth_meta_buffer_store;
	__u64 depth_meta_buffer_store_stride;
	__u64 depth_meta_buffer_partial;
	__u64 depth_meta_buffer_partial_stride;

	__u64 stencil_buffer_load;
	__u64 stencil_buffer_load_stride;
	__u64 stencil_buffer_store;
	__u64 stencil_buffer_store_stride;
	__u64 stencil_buffer_partial;
	__u64 stencil_buffer_partial_stride;
	__u64 stencil_meta_buffer_load;
	__u64 stencil_meta_buffer_load_stride;
	__u64 stencil_meta_buffer_store;
	__u64 stencil_meta_buffer_store_stride;
	__u64 stencil_meta_buffer_partial;
	__u64 stencil_meta_buffer_partial_stride;

	__u64 scissor_array;
	__u64 depth_bias_array;
	__u64 visibility_result_buffer;

	__u64 vertex_sampler_array;
	__u32 vertex_sampler_count;
	__u32 vertex_sampler_max;

	__u64 fragment_sampler_array;
	__u32 fragment_sampler_count;
	__u32 fragment_sampler_max;

	__u64 zls_ctrl;
	__u64 ppp_multisamplectl;
	__u32 ppp_ctrl;

	__u32 fb_width;
	__u32 fb_height;

	__u32 utile_width;
	__u32 utile_height;

	__u32 samples;
	__u32 layers;

	__u32 encoder_id;
	__u32 cmd_ta_id;
	__u32 cmd_3d_id;

	__u32 sample_size;
	__u32 tib_blocks;
	__u32 iogpu_unk_214;

	__u32 merge_upper_x;
	__u32 merge_upper_y;

	__u32 load_pipeline;
	__u32 load_pipeline_bind;

	__u32 store_pipeline;
	__u32 store_pipeline_bind;

	__u32 partial_reload_pipeline;
	__u32 partial_reload_pipeline_bind;

	__u32 partial_store_pipeline;
	__u32 partial_store_pipeline_bind;

	__u32 depth_dimensions;
	__u32 isp_bgobjdepth;
	__u32 isp_bgobjvals;
};

#define ASAHI_RENDER_UNK_UNK1			(1UL << 0)
#define ASAHI_RENDER_UNK_SET_TILE_CONFIG	(1UL << 1)
#define ASAHI_RENDER_UNK_SET_UTILE_CONFIG	(1UL << 2)
#define ASAHI_RENDER_UNK_SET_AUX_FB_UNK		(1UL << 3)
#define ASAHI_RENDER_UNK_SET_G14_UNK		(1UL << 4)

#define ASAHI_RENDER_UNK_SET_FRG_UNK_140	(1UL << 20)
#define ASAHI_RENDER_UNK_SET_FRG_UNK_158	(1UL << 21)
#define ASAHI_RENDER_UNK_SET_FRG_TILECFG	(1UL << 22)
#define ASAHI_RENDER_UNK_SET_LOAD_BGOBJVALS	(1UL << 23)
#define ASAHI_RENDER_UNK_SET_FRG_UNK_38		(1UL << 24)
#define ASAHI_RENDER_UNK_SET_FRG_UNK_3C		(1UL << 25)

#define ASAHI_RENDER_UNK_SET_RELOAD_ZLSCTRL	(1UL << 27)
#define ASAHI_RENDER_UNK_SET_UNK_BUF_10		(1UL << 28)
#define ASAHI_RENDER_UNK_SET_FRG_UNK_MASK	(1UL << 29)

#define ASAHI_RENDER_UNK_SET_IOGPU_UNK54	(1UL << 40)
#define ASAHI_RENDER_UNK_SET_IOGPU_UNK56	(1UL << 41)
#define ASAHI_RENDER_UNK_SET_TILING_CONTROL	(1UL << 42)
#define ASAHI_RENDER_UNK_SET_TILING_CONTROL_2	(1UL << 43)
#define ASAHI_RENDER_UNK_SET_VTX_UNK_F0		(1UL << 44)
#define ASAHI_RENDER_UNK_SET_VTX_UNK_F8		(1UL << 45)
#define ASAHI_RENDER_UNK_SET_VTX_UNK_118	(1UL << 46)
#define ASAHI_RENDER_UNK_SET_VTX_UNK_MASK	(1UL << 47)

#define ASAHI_RENDER_EXT_UNKNOWNS	0xff00

/* XXX: Do not upstream this struct */
struct drm_asahi_cmd_render_unknowns {
	/** @type: Type ID of this extension */
	__u32 type;
	__u32 pad;
	/** @next: Pointer to the next extension struct, if any */
	__u64 next;

	__u64 flags;

	__u64 tile_config;
	__u64 utile_config;

	__u64 aux_fb_unk;
	__u64 g14_unk;
	__u64 frg_unk_140;
	__u64 frg_unk_158;
	__u64 frg_tilecfg;
	__u64 load_bgobjvals;
	__u64 frg_unk_38;
	__u64 frg_unk_3c;
	__u64 reload_zlsctrl;
	__u64 unk_buf_10;
	__u64 frg_unk_mask;

	__u64 iogpu_unk54;
	__u64 iogpu_unk56;
	__u64 tiling_control;
	__u64 tiling_control_2;
	__u64 vtx_unk_f0;
	__u64 vtx_unk_f8;
	__u64 vtx_unk_118;
	__u64 vtx_unk_mask;
};

#define ASAHI_RENDER_EXT_TIMESTAMPS	0x0001

/** User timestamp buffers for render commands */
struct drm_asahi_cmd_render_user_timestamps {
	/** @type: Type ID of this extension */
	__u32 type;
	/** @pad: MBZ */
	__u32 pad;
	/** @next: Pointer to the next extension struct, if any */
	__u64 next;

	/** @vtx_start_handle: Handle of the timestamp buffer for the vertex start ts */
	__u32 vtx_start_handle;
	/** @vtx_start_offset: Offset into the timestamp buffer of the vertex start ts */
	__u32 vtx_start_offset;

	/** @vtx_end_handle: Handle of the timestamp buffer for the vertex end ts */
	__u32 vtx_end_handle;
	/** @vtx_end_offset: Offset into the timestamp buffer of the vertex end ts */
	__u32 vtx_end_offset;

	/** @frg_start_handle: Handle of the timestamp buffer for the fragment start ts */
	__u32 frg_start_handle;
	/** @frg_start_offset: Offset into the timestamp buffer of the fragment start ts */
	__u32 frg_start_offset;

	/** @frg_end_handle: Handle of the timestamp buffer for the fragment end ts */
	__u32 frg_end_handle;
	/** @frg_end_offset: Offset into the timestamp buffer of the fragment end ts */
	__u32 frg_end_offset;
};

/* XXX check */
#define ASAHI_COMPUTE_NO_PREEMPTION (1UL << 0)

/** Compute command submission data */
struct drm_asahi_cmd_compute {
	/* TODO: remove guards on next bump */
#if DRM_ASAHI_UNSTABLE_UABI_VERSION > 10011
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;
#endif

	__u64 flags;

	__u64 encoder_ptr;
	__u64 encoder_end;
	__u64 usc_base;

	__u64 attachments;
	__u32 attachment_count;
	__u32 pad;

	__u32 helper_program;
	__u32 helper_cfg;
	__u64 helper_arg;

	__u32 encoder_id;
	__u32 cmd_id;

	__u64 sampler_array;
	__u32 sampler_count;
	__u32 sampler_max;

	__u32 iogpu_unk_40;
	__u32 unk_mask;

#if DRM_ASAHI_UNSTABLE_UABI_VERSION <= 10011
	/* We forgot the extension pointer in <=10011... */
	__u64 extensions;
#endif
};

#define ASAHI_COMPUTE_EXT_TIMESTAMPS	0x0001

/** User timestamp buffers for compute commands */
struct drm_asahi_cmd_compute_user_timestamps {
	/** @type: Type ID of this extension */
	__u32 type;
	/** @pad: MBZ */
	__u32 pad;
	/** @next: Pointer to the next extension struct, if any */
	__u64 next;

	/** @start_handle: Handle of the timestamp buffer for the start ts */
	__u32 start_handle;
	/** @start_offset: Offset into the timestamp buffer of the start ts */
	__u32 start_offset;

	/** @end_handle: Handle of the timestamp buffer for the end ts */
	__u32 end_handle;
	/** @end_offset: Offset into the timestamp buffer of the end ts */
	__u32 end_offset;

};

/** Command completion status */
enum drm_asahi_status {
	DRM_ASAHI_STATUS_PENDING = 0,
	DRM_ASAHI_STATUS_COMPLETE,
	DRM_ASAHI_STATUS_UNKNOWN_ERROR,
	DRM_ASAHI_STATUS_TIMEOUT,
	DRM_ASAHI_STATUS_FAULT,
	DRM_ASAHI_STATUS_KILLED,
	DRM_ASAHI_STATUS_NO_DEVICE,
	DRM_ASAHI_STATUS_CHANNEL_ERROR,
};

/** GPU fault information */
enum drm_asahi_fault {
	DRM_ASAHI_FAULT_NONE = 0,
	DRM_ASAHI_FAULT_UNKNOWN,
	DRM_ASAHI_FAULT_UNMAPPED,
	DRM_ASAHI_FAULT_AF_FAULT,
	DRM_ASAHI_FAULT_WRITE_ONLY,
	DRM_ASAHI_FAULT_READ_ONLY,
	DRM_ASAHI_FAULT_NO_ACCESS,
};

/** Common command completion result information */
struct drm_asahi_result_info {
	/** @status: One of enum drm_asahi_status */
	__u32 status;

	/** @reason: One of drm_asahi_fault_type */
	__u32 fault_type;

	/** @unit: Unit number, hardware dependent */
	__u32 unit;

	/** @sideband: Sideband information, hardware dependent */
	__u32 sideband;

	/** @level: Page table level at which the fault occurred, hardware dependent */
	__u8 level;

	/** @read: Fault was a read */
	__u8 is_read;

	/** @pad: MBZ */
	__u16 pad;

	/** @unk_5: Extra bits, hardware dependent */
	__u32 extra;

	/** @address: Fault address, cache line aligned */
	__u64 address;
};

#define DRM_ASAHI_RESULT_RENDER_TVB_GROW_OVF (1UL << 0)
#define DRM_ASAHI_RESULT_RENDER_TVB_GROW_MIN (1UL << 1)
#define DRM_ASAHI_RESULT_RENDER_TVB_OVERFLOWED (1UL << 2)

/** Render command completion result information */
struct drm_asahi_result_render {
	/** @address: Common result information */
	struct drm_asahi_result_info info;

	/** @flags: Zero or more of of DRM_ASAHI_RESULT_RENDER_* */
	__u64 flags;

	/** @vertex_ts_start: Timestamp of the start of vertex processing */
	__u64 vertex_ts_start;

	/** @vertex_ts_end: Timestamp of the end of vertex processing */
	__u64 vertex_ts_end;

	/** @fragment_ts_start: Timestamp of the start of fragment processing */
	__u64 fragment_ts_start;

	/** @fragment_ts_end: Timestamp of the end of fragment processing */
	__u64 fragment_ts_end;

	/** @tvb_size_bytes: TVB size at the start of this render */
	__u64 tvb_size_bytes;

	/** @tvb_usage_bytes: Total TVB usage in bytes for this render */
	__u64 tvb_usage_bytes;

	/** @num_tvb_overflows: Number of TVB overflows that occurred for this render */
	__u32 num_tvb_overflows;

	/** @pad: MBZ */
	__u32 pad;
};

/** Compute command completion result information */
struct drm_asahi_result_compute {
	/** @address: Common result information */
	struct drm_asahi_result_info info;

	/** @flags: Zero or more of of DRM_ASAHI_RESULT_COMPUTE_* */
	__u64 flags;

	/** @ts_start: Timestamp of the start of this compute command */
	__u64 ts_start;

	/** @vertex_ts_end: Timestamp of the end of this compute command */
	__u64 ts_end;
};

/** Fetch the current GPU timestamp time */
struct drm_asahi_get_time {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @flags: MBZ. */
	__u64 flags;

	/** @gpu_timestamp: On return, the current GPU timestamp */
	__u64 gpu_timestamp;
};

/* Note: this is an enum so that it can be resolved by Rust bindgen. */
enum {
   DRM_IOCTL_ASAHI_GET_PARAMS       = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_GET_PARAMS, struct drm_asahi_get_params),
   DRM_IOCTL_ASAHI_VM_CREATE        = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_VM_CREATE, struct drm_asahi_vm_create),
   DRM_IOCTL_ASAHI_VM_DESTROY       = DRM_IOW(DRM_COMMAND_BASE + DRM_ASAHI_VM_DESTROY, struct drm_asahi_vm_destroy),
   DRM_IOCTL_ASAHI_GEM_CREATE       = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_GEM_CREATE, struct drm_asahi_gem_create),
   DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET  = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_GEM_MMAP_OFFSET, struct drm_asahi_gem_mmap_offset),
   DRM_IOCTL_ASAHI_GEM_BIND         = DRM_IOW(DRM_COMMAND_BASE + DRM_ASAHI_GEM_BIND, struct drm_asahi_gem_bind),
   DRM_IOCTL_ASAHI_QUEUE_CREATE     = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_QUEUE_CREATE, struct drm_asahi_queue_create),
   DRM_IOCTL_ASAHI_QUEUE_DESTROY    = DRM_IOW(DRM_COMMAND_BASE + DRM_ASAHI_QUEUE_DESTROY, struct drm_asahi_queue_destroy),
   DRM_IOCTL_ASAHI_SUBMIT           = DRM_IOW(DRM_COMMAND_BASE + DRM_ASAHI_SUBMIT, struct drm_asahi_submit),
   DRM_IOCTL_ASAHI_GET_TIME         = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_GET_TIME, struct drm_asahi_get_time),
   DRM_IOCTL_ASAHI_GEM_BIND_OBJECT  = DRM_IOWR(DRM_COMMAND_BASE + DRM_ASAHI_GEM_BIND_OBJECT, struct drm_asahi_gem_bind_object),
};

#if defined(__cplusplus)
}
#endif

#endif /* _ASAHI_DRM_H_ */
