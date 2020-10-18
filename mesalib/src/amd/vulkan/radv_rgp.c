/*
 * Copyright © 2020 Valve Corporation
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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "radv_private.h"

#include "util/u_process.h"

#include "drm-uapi/amdgpu_drm.h"

#define SQTT_FILE_MAGIC_NUMBER 0x50303042
#define SQTT_FILE_VERSION_MAJOR 1
#define SQTT_FILE_VERSION_MINOR 4

#define SQTT_GPU_NAME_MAX_SIZE 256
#define SQTT_MAX_NUM_SE 32
#define SQTT_SA_PER_SE 2

enum sqtt_version {
	SQTT_VERSION_NONE = 0x0,
	SQTT_VERSION_1_0  = 0x1,
	SQTT_VERSION_1_1  = 0x2,
	SQTT_VERSION_2_0  = 0x3, /* GFX6 */
	SQTT_VERSION_2_1  = 0x4, /* GFX7 */
	SQTT_VERSION_2_2  = 0x5, /* GFX8 */
	SQTT_VERSION_2_3  = 0x6, /* GFX9 */
	SQTT_VERSION_2_4  = 0x7  /* GFX10 */
};

/**
 * SQTT chunks.
 */
enum sqtt_file_chunk_type {
	SQTT_FILE_CHUNK_TYPE_ASIC_INFO,
	SQTT_FILE_CHUNK_TYPE_SQTT_DESC,
	SQTT_FILE_CHUNK_TYPE_SQTT_DATA,
	SQTT_FILE_CHUNK_TYPE_API_INFO,
	SQTT_FILE_CHUNK_TYPE_ISA_DATABASE,
	SQTT_FILE_CHUNK_TYPE_QUEUE_EVENT_TIMINGS,
	SQTT_FILE_CHUNK_TYPE_CLOCK_CALIBRATION,
	SQTT_FILE_CHUNK_TYPE_CPU_INFO,
	SQTT_FILE_CHUNK_TYPE_SPM_DB,
	SQTT_FILE_CHUNK_TYPE_CODE_OBJECT_DATABASE,
	SQTT_FILE_CHUNK_TYPE_CODE_OBJECT_LOADER_EVENTS,
	SQTT_FILE_CHUNK_TYPE_PSO_CORRELATION,
	SQTT_FILE_CHUNK_TYPE_INSTRUMENTATION_TABLE,
	SQTT_FILE_CHUNK_TYPE_COUNT
};

struct sqtt_file_chunk_id {
	enum sqtt_file_chunk_type type : 8;
	int32_t index : 8;
	int32_t reserved : 16;
};

struct sqtt_file_chunk_header {
	struct sqtt_file_chunk_id chunk_id;
	uint16_t minor_version;
	uint16_t major_version;
	int32_t size_in_bytes;
	int32_t padding;
};

/**
 * SQTT file header.
 */
struct sqtt_file_header_flags {
	union {
		struct {
			int32_t is_semaphore_queue_timing_etw : 1;
			int32_t no_queue_semaphore_timestamps : 1;
			int32_t reserved : 30;
		};

		uint32_t value;
	};
};

struct sqtt_file_header {
	uint32_t magic_number;
	uint32_t version_major;
	uint32_t version_minor;
	struct sqtt_file_header_flags flags;
	int32_t chunk_offset;
	int32_t second;
	int32_t minute;
	int32_t hour;
	int32_t day_in_month;
	int32_t month;
	int32_t year;
	int32_t day_in_week;
	int32_t day_in_year;
	int32_t is_daylight_savings;
};

static_assert(sizeof(struct sqtt_file_header) == 56,
	      "sqtt_file_header doesn't match RGP spec");

static void
radv_sqtt_fill_header(struct sqtt_file_header *header)
{
	struct tm *timep, result;
	time_t raw_time;

	header->magic_number = SQTT_FILE_MAGIC_NUMBER;
	header->version_major = SQTT_FILE_VERSION_MAJOR;
	header->version_minor = SQTT_FILE_VERSION_MINOR;
	header->flags.value = 0;
	header->flags.is_semaphore_queue_timing_etw = 1;
	header->flags.no_queue_semaphore_timestamps = 0;
	header->chunk_offset = sizeof(*header);

	time(&raw_time);
	timep = localtime_r(&raw_time, &result);

	header->second = timep->tm_sec;
	header->minute = timep->tm_min;
	header->hour = timep->tm_hour;
	header->day_in_month = timep->tm_mday;
	header->month = timep->tm_mon;
	header->year = timep->tm_year;
	header->day_in_week = timep->tm_wday;
	header->day_in_year = timep->tm_yday;
	header->is_daylight_savings = timep->tm_isdst;
}

/**
 * SQTT CPU info.
 */
struct sqtt_file_chunk_cpu_info {
	struct sqtt_file_chunk_header header;
	uint32_t vendor_id[4];
	uint32_t processor_brand[12];
	uint32_t reserved[2];
	uint64_t cpu_timestamp_freq;
	uint32_t clock_speed;
	uint32_t num_logical_cores;
	uint32_t num_physical_cores;
	uint32_t system_ram_size;
};

static_assert(sizeof(struct sqtt_file_chunk_cpu_info) == 112,
	      "sqtt_file_chunk_cpu_info doesn't match RGP spec");

static void
radv_sqtt_fill_cpu_info(struct sqtt_file_chunk_cpu_info *chunk)
{
	uint64_t system_ram_size = 0;

	chunk->header.chunk_id.type = SQTT_FILE_CHUNK_TYPE_CPU_INFO;
	chunk->header.chunk_id.index = 0;
	chunk->header.major_version = 0;
	chunk->header.minor_version = 0;
	chunk->header.size_in_bytes = sizeof(*chunk);

	chunk->cpu_timestamp_freq = 1000000000; /* tick set to 1ns */

	/* TODO: fill with real info. */

	strncpy((char *)chunk->vendor_id, "Unknown", sizeof(chunk->vendor_id));
	strncpy((char *)chunk->processor_brand, "Unknown", sizeof(chunk->processor_brand));
	chunk->clock_speed = 0;
	chunk->num_logical_cores = 0;
	chunk->num_physical_cores = 0;

	chunk->system_ram_size = 0;
	if (os_get_total_physical_memory(&system_ram_size))
		chunk->system_ram_size = system_ram_size / (1024 * 1024);
}

/**
 * SQTT ASIC info.
 */
enum sqtt_file_chunk_asic_info_flags
{
	SQTT_FILE_CHUNK_ASIC_INFO_FLAG_SC_PACKER_NUMBERING      = (1 << 0),
	SQTT_FILE_CHUNK_ASIC_INFO_FLAG_PS1_EVENT_TOKENS_ENABLED = (1 << 1)
};

enum sqtt_gpu_type {
	SQTT_GPU_TYPE_UNKNOWN    = 0x0,
	SQTT_GPU_TYPE_INTEGRATED = 0x1,
	SQTT_GPU_TYPE_DISCRETE   = 0x2,
	SQTT_GPU_TYPE_VIRTUAL    = 0x3
};

enum sqtt_gfxip_level {
	SQTT_GFXIP_LEVEL_NONE       = 0x0,
	SQTT_GFXIP_LEVEL_GFXIP_6    = 0x1,
	SQTT_GFXIP_LEVEL_GFXIP_7    = 0x2,
	SQTT_GFXIP_LEVEL_GFXIP_8    = 0x3,
	SQTT_GFXIP_LEVEL_GFXIP_8_1  = 0x4,
	SQTT_GFXIP_LEVEL_GFXIP_9    = 0x5,
	SQTT_GFXIP_LEVEL_GFXIP_10_1 = 0x7,
};

enum sqtt_memory_type {
	SQTT_MEMORY_TYPE_UNKNOWN = 0x0,
	SQTT_MEMORY_TYPE_DDR     = 0x1,
	SQTT_MEMORY_TYPE_DDR2    = 0x2,
	SQTT_MEMORY_TYPE_DDR3    = 0x3,
	SQTT_MEMORY_TYPE_DDR4    = 0x4,
	SQTT_MEMORY_TYPE_GDDR3   = 0x10,
	SQTT_MEMORY_TYPE_GDDR4   = 0x11,
	SQTT_MEMORY_TYPE_GDDR5   = 0x12,
	SQTT_MEMORY_TYPE_GDDR6   = 0x13,
	SQTT_MEMORY_TYPE_HBM     = 0x20,
	SQTT_MEMORY_TYPE_HBM2    = 0x21,
	SQTT_MEMORY_TYPE_HBM3    = 0x22,
};

struct sqtt_file_chunk_asic_info {
	struct sqtt_file_chunk_header header;
	uint64_t flags;
	uint64_t trace_shader_core_clock;
	uint64_t trace_memory_clock;
	int32_t device_id;
	int32_t device_revision_id;
	int32_t vgprs_per_simd;
	int32_t sgprs_per_simd;
	int32_t shader_engines;
	int32_t compute_unit_per_shader_engine;
	int32_t simd_per_compute_unit;
	int32_t wavefronts_per_simd;
	int32_t minimum_vgpr_alloc;
	int32_t vgpr_alloc_granularity;
	int32_t minimum_sgpr_alloc;
	int32_t sgpr_alloc_granularity;
	int32_t hardware_contexts;
	enum sqtt_gpu_type gpu_type;
	enum sqtt_gfxip_level gfxip_level;
	int32_t gpu_index;
	int32_t gds_size;
	int32_t gds_per_shader_engine;
	int32_t ce_ram_size;
	int32_t ce_ram_size_graphics;
	int32_t	ce_ram_size_compute;
	int32_t max_number_of_dedicated_cus;
	int64_t vram_size;
	int32_t vram_bus_width;
	int32_t l2_cache_size;
	int32_t l1_cache_size;
	int32_t	lds_size;
	char gpu_name[SQTT_GPU_NAME_MAX_SIZE];
	float alu_per_clock;
	float texture_per_clock;
	float prims_per_clock;
	float pixels_per_clock;
	uint64_t gpu_timestamp_frequency;
	uint64_t max_shader_core_clock;
	uint64_t max_memory_clock;
	uint32_t memory_ops_per_clock;
	enum sqtt_memory_type memory_chip_type;
	uint32_t lds_granularity;
	uint16_t cu_mask[SQTT_MAX_NUM_SE][SQTT_SA_PER_SE];
	char reserved1[128];
	char padding[4];
};

static_assert(sizeof(struct sqtt_file_chunk_asic_info) == 720,
	      "sqtt_file_chunk_asic_info doesn't match RGP spec");

static enum sqtt_gfxip_level
radv_chip_class_to_sqtt_gfxip_level(enum chip_class chip_class)
{
	switch (chip_class) {
	case GFX6:
		return SQTT_GFXIP_LEVEL_GFXIP_6;
	case GFX7:
		return SQTT_GFXIP_LEVEL_GFXIP_7;
	case GFX8:
		return SQTT_GFXIP_LEVEL_GFXIP_8;
	case GFX9:
		return SQTT_GFXIP_LEVEL_GFXIP_9;
	case GFX10:
		return SQTT_GFXIP_LEVEL_GFXIP_10_1;
	default:
		unreachable("Invalid chip class");
	}
}

static enum sqtt_memory_type
radv_vram_type_to_sqtt_memory_type(uint32_t vram_type)
{
	switch (vram_type) {
	case AMDGPU_VRAM_TYPE_UNKNOWN:
		return SQTT_MEMORY_TYPE_UNKNOWN;
	case AMDGPU_VRAM_TYPE_DDR2:
		return SQTT_MEMORY_TYPE_DDR2;
	case AMDGPU_VRAM_TYPE_DDR3:
		return SQTT_MEMORY_TYPE_DDR3;
	case AMDGPU_VRAM_TYPE_DDR4:
		return SQTT_MEMORY_TYPE_DDR4;
	case AMDGPU_VRAM_TYPE_GDDR5:
		return SQTT_MEMORY_TYPE_GDDR5;
	case AMDGPU_VRAM_TYPE_HBM:
		return SQTT_MEMORY_TYPE_HBM;
	case AMDGPU_VRAM_TYPE_GDDR6:
		return SQTT_MEMORY_TYPE_GDDR6;
	case AMDGPU_VRAM_TYPE_GDDR1:
	case AMDGPU_VRAM_TYPE_GDDR3:
	case AMDGPU_VRAM_TYPE_GDDR4:
	default:
		unreachable("Invalid vram type");
	}
}

static void
radv_fill_sqtt_asic_info(struct radv_device *device,
			 struct sqtt_file_chunk_asic_info *chunk)
{
	struct radeon_info *rad_info = &device->physical_device->rad_info;
	bool has_wave32 = rad_info->chip_class >= GFX10;

	chunk->header.chunk_id.type = SQTT_FILE_CHUNK_TYPE_ASIC_INFO;
	chunk->header.chunk_id.index = 0;
	chunk->header.major_version = 0;
	chunk->header.minor_version = 4;
	chunk->header.size_in_bytes = sizeof(*chunk);

	chunk->flags = 0;

	/* All chips older than GFX9 are affected by the "SPI not
	 * differentiating pkr_id for newwave commands" bug.
	 */
	if (rad_info->chip_class < GFX9)
		chunk->flags |= SQTT_FILE_CHUNK_ASIC_INFO_FLAG_SC_PACKER_NUMBERING;

	/* Only FIJI and GFX9+ support PS1 events. */
	if (rad_info->family == CHIP_FIJI || rad_info->chip_class >= GFX9)
		chunk->flags |= SQTT_FILE_CHUNK_ASIC_INFO_FLAG_PS1_EVENT_TOKENS_ENABLED;

	chunk->trace_shader_core_clock = rad_info->max_shader_clock * 1000000;
	chunk->trace_memory_clock = rad_info->max_memory_clock * 1000000;

	chunk->device_id = rad_info->pci_id;
	chunk->device_revision_id = rad_info->pci_rev_id;
	chunk->vgprs_per_simd = rad_info->num_physical_wave64_vgprs_per_simd *
	                        (has_wave32 ? 2 : 1);
	chunk->sgprs_per_simd = rad_info->num_physical_sgprs_per_simd;
	chunk->shader_engines = rad_info->max_se;
	chunk->compute_unit_per_shader_engine = rad_info->min_good_cu_per_sa *
	                                        rad_info->max_sh_per_se;
	chunk->simd_per_compute_unit = rad_info->num_simd_per_compute_unit;
	chunk->wavefronts_per_simd = rad_info->max_wave64_per_simd;

	chunk->minimum_vgpr_alloc = rad_info->min_wave64_vgpr_alloc;
	chunk->vgpr_alloc_granularity = rad_info->wave64_vgpr_alloc_granularity *
	                                (has_wave32 ? 2 : 1);
	chunk->minimum_sgpr_alloc = rad_info->min_sgpr_alloc;
	chunk->sgpr_alloc_granularity = rad_info->sgpr_alloc_granularity;

	chunk->hardware_contexts = 8;
	chunk->gpu_type = rad_info->has_dedicated_vram ? SQTT_GPU_TYPE_DISCRETE : SQTT_GPU_TYPE_INTEGRATED;
	chunk->gfxip_level = radv_chip_class_to_sqtt_gfxip_level(rad_info->chip_class);
	chunk->gpu_index = 0;

	chunk->max_number_of_dedicated_cus = 0;
	chunk->ce_ram_size = rad_info->ce_ram_size;
	chunk->ce_ram_size_graphics = 0;
	chunk->ce_ram_size_compute = 0;

	chunk->vram_bus_width = rad_info->vram_bit_width;
	chunk->vram_size = rad_info->vram_size;
	chunk->l2_cache_size = rad_info->l2_cache_size;
	chunk->l1_cache_size = rad_info->l1_cache_size;
	chunk->lds_size = rad_info->lds_size_per_workgroup;

	strncpy(chunk->gpu_name, device->physical_device->name, SQTT_GPU_NAME_MAX_SIZE);

	chunk->alu_per_clock = 0.0;
	chunk->texture_per_clock = 0.0;
	chunk->prims_per_clock = 0.0;
	chunk->pixels_per_clock = 0.0;

	chunk->gpu_timestamp_frequency = rad_info->clock_crystal_freq * 1000;
	chunk->max_shader_core_clock = rad_info->max_shader_clock * 1000000;
	chunk->max_memory_clock = rad_info->max_memory_clock * 1000000;
	chunk->memory_ops_per_clock = 0;
	chunk->memory_chip_type = radv_vram_type_to_sqtt_memory_type(rad_info->vram_type);
	chunk->lds_granularity = rad_info->lds_granularity;

	for (unsigned se = 0; se < 4; se++) {
		for (unsigned sa = 0; sa < 2; sa++) {
			chunk->cu_mask[se][sa] = rad_info->cu_mask[se][sa];
		}
	}
}

/**
 * SQTT API info.
 */
enum sqtt_api_type {
	SQTT_API_TYPE_DIRECTX_12,
	SQTT_API_TYPE_VULKAN,
	SQTT_API_TYPE_GENERIC,
	SQTT_API_TYPE_OPENCL
};

enum sqtt_instruction_trace_mode
{
	SQTT_INSTRUCTION_TRACE_DISABLED   = 0x0,
	SQTT_INSTRUCTION_TRACE_FULL_FRAME = 0x1,
	SQTT_INSTRUCTION_TRACE_API_PSO    = 0x2,
};

enum sqtt_profiling_mode {
	SQTT_PROFILING_MODE_PRESENT      = 0x0,
	SQTT_PROFILING_MODE_USER_MARKERS = 0x1,
	SQTT_PROFILING_MODE_INDEX        = 0x2,
	SQTT_PROFILING_MODE_TAG          = 0x3,
};

union sqtt_profiling_mode_data {
	struct {
		char start[256];
		char end[256];
	} user_marker_profiling_data;

	struct {
		uint32_t start;
		uint32_t end;
	} index_profiling_data;

	struct {
		uint32_t begin_hi;
		uint32_t begin_lo;
		uint32_t end_hi;
		uint32_t end_lo;
	} tag_profiling_data;
};

union sqtt_instruction_trace_data {
	struct {
		uint64_t api_pso_filter;
	} api_pso_data;

	struct {
		char start[256];
		char end[256];
	} user_marker_data;
};

struct sqtt_file_chunk_api_info {
	struct sqtt_file_chunk_header header;
	enum sqtt_api_type api_type;
	uint16_t major_version;
	uint16_t minor_version;
	enum sqtt_profiling_mode profiling_mode;
	uint32_t reserved;
	union sqtt_profiling_mode_data profiling_mode_data;
	enum sqtt_instruction_trace_mode instruction_trace_mode;
	uint32_t reserved2;
	union sqtt_instruction_trace_data instruction_trace_data;
};

static_assert(sizeof(struct sqtt_file_chunk_api_info) == 1064,
	      "sqtt_file_chunk_api_info doesn't match RGP spec");

static void
radv_sqtt_fill_api_info(struct sqtt_file_chunk_api_info *chunk)
{
	chunk->header.chunk_id.type = SQTT_FILE_CHUNK_TYPE_API_INFO;
	chunk->header.chunk_id.index = 0;
	chunk->header.major_version = 0;
	chunk->header.minor_version = 1;
	chunk->header.size_in_bytes = sizeof(*chunk);

	chunk->api_type = SQTT_API_TYPE_VULKAN;
	chunk->major_version = 0;
	chunk->minor_version = 0;
	chunk->profiling_mode = SQTT_PROFILING_MODE_PRESENT;
	chunk->instruction_trace_mode = SQTT_INSTRUCTION_TRACE_DISABLED;
}

/**
 * SQTT desc info.
 */
struct sqtt_file_chunk_sqtt_desc {
	struct sqtt_file_chunk_header header;
	int32_t shader_engine_index;
	enum sqtt_version sqtt_version;
	union {
		struct {
			int32_t instrumentation_version;
		} v0;
		struct {
			int16_t instrumentation_spec_version;
			int16_t instrumentation_api_version;
			int32_t compute_unit_index;
		} v1;
	};
};

static_assert(sizeof(struct sqtt_file_chunk_sqtt_desc) == 32,
	      "sqtt_file_chunk_sqtt_desc doesn't match RGP spec");

static enum sqtt_version
radv_chip_class_to_sqtt_version(enum chip_class chip_class)
{
	switch (chip_class) {
	case GFX6:
		return SQTT_VERSION_2_0;
	case GFX7:
		return SQTT_VERSION_2_1;
	case GFX8:
		return SQTT_VERSION_2_2;
	case GFX9:
		return SQTT_VERSION_2_3;
	case GFX10:
		return SQTT_VERSION_2_4;
	default:
		unreachable("Invalid chip class");
	}
}

static void
radv_sqtt_fill_sqtt_desc(struct radv_device *device,
			 struct sqtt_file_chunk_sqtt_desc *chunk,
			 int32_t chunk_index,
			 int32_t shader_engine_index,
			 int32_t compute_unit_index)
{
	chunk->header.chunk_id.type = SQTT_FILE_CHUNK_TYPE_SQTT_DESC;
	chunk->header.chunk_id.index = chunk_index;
	chunk->header.major_version = 0;
	chunk->header.minor_version = 2;
	chunk->header.size_in_bytes = sizeof(*chunk);

	chunk->sqtt_version = radv_chip_class_to_sqtt_version(device->physical_device->rad_info.chip_class);
	chunk->shader_engine_index = shader_engine_index;
	chunk->v1.instrumentation_spec_version = 1;
	chunk->v1.instrumentation_api_version = 0;
	chunk->v1.compute_unit_index = compute_unit_index;
}

/**
 * SQTT data info.
 */
struct sqtt_file_chunk_sqtt_data {
	struct sqtt_file_chunk_header header;
	int32_t offset; /* in bytes */
	int32_t size; /* in bytes */
};

static_assert(sizeof(struct sqtt_file_chunk_sqtt_data) == 24,
	      "sqtt_file_chunk_sqtt_data doesn't match RGP spec");

static void
radv_sqtt_fill_sqtt_data(struct sqtt_file_chunk_sqtt_data *chunk,
			 int32_t chunk_index, int32_t offset, int32_t size)
{
	chunk->header.chunk_id.type = SQTT_FILE_CHUNK_TYPE_SQTT_DATA;
	chunk->header.chunk_id.index = chunk_index;
	chunk->header.major_version = 0;
	chunk->header.minor_version = 0;
	chunk->header.size_in_bytes = sizeof(*chunk) + size;

	chunk->offset = sizeof(*chunk) + offset;
	chunk->size = size;
}

static void
radv_sqtt_dump_data(struct radv_device *device,
		    const struct radv_thread_trace *thread_trace,
		    FILE *output)
{
	struct sqtt_file_chunk_asic_info asic_info = {0};
	struct sqtt_file_chunk_cpu_info cpu_info = {0};
	struct sqtt_file_chunk_api_info api_info = {0};
	struct sqtt_file_header header = {0};
	size_t file_offset = 0;

	/* SQTT header file. */
	radv_sqtt_fill_header(&header);
	file_offset += sizeof(header);
	fwrite(&header, sizeof(header), 1, output);

	/* SQTT cpu chunk. */
	radv_sqtt_fill_cpu_info(&cpu_info);
	file_offset += sizeof(cpu_info);
	fwrite(&cpu_info, sizeof(cpu_info), 1, output);

	/* SQTT asic chunk. */
	radv_fill_sqtt_asic_info(device, &asic_info);
	file_offset += sizeof(asic_info);
	fwrite(&asic_info, sizeof(asic_info), 1, output);

	/* SQTT api chunk. */
	radv_sqtt_fill_api_info(&api_info);
	file_offset += sizeof(api_info);
	fwrite(&api_info, sizeof(api_info), 1, output);

	if (thread_trace) {
		for (unsigned i = 0; i < thread_trace->num_traces; i++) {
			const struct radv_thread_trace_se *se = &thread_trace->traces[i];
			const struct radv_thread_trace_info *info = &se->info;
			struct sqtt_file_chunk_sqtt_desc desc = {0};
			struct sqtt_file_chunk_sqtt_data data = {0};
			uint64_t size = info->cur_offset * 32; /* unit of 32 bytes */

			/* SQTT desc chunk. */
			radv_sqtt_fill_sqtt_desc(device, &desc, i,
						 se->shader_engine,
						 se->compute_unit);
			file_offset += sizeof(desc);
			fwrite(&desc, sizeof(desc), 1, output);

			/* SQTT data chunk. */
			radv_sqtt_fill_sqtt_data(&data, i, file_offset, size);
			file_offset += sizeof(data);
			fwrite(&data, sizeof(data), 1, output);

			/* Copy thread trace data generated by the hardware. */
			file_offset += size;
			fwrite(se->data_ptr, size, 1, output);
		}
	}
}

int
radv_dump_thread_trace(struct radv_device *device,
		       const struct radv_thread_trace *thread_trace)
{
	char filename[2048];
	struct tm now;
	time_t t;
	FILE *f;

	t = time(NULL);
	now = *localtime(&t);

	snprintf(filename, sizeof(filename), "/tmp/%s_%04d.%02d.%02d_%02d.%02d.rgp",
		 util_get_process_name(), 1900 + now.tm_year, now.tm_mon + 1,
		 now.tm_mday, now.tm_hour, now.tm_min);

	f = fopen(filename, "w+");
	if (!f)
		return -1;

	radv_sqtt_dump_data(device, thread_trace, f);

	fprintf(stderr, "Thread trace capture saved to '%s'\n", filename);

	fclose(f);
	return 0;
}

