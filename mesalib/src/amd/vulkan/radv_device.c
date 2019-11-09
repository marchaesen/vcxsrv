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

#include "dirent.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <llvm/Config/llvm-config.h>

#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_cs.h"
#include "util/disk_cache.h"
#include "vk_util.h"
#include <xf86drm.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>
#include "winsys/amdgpu/radv_amdgpu_winsys_public.h"
#include "ac_llvm_util.h"
#include "vk_format.h"
#include "sid.h"
#include "git_sha1.h"
#include "util/build_id.h"
#include "util/debug.h"
#include "util/mesa-sha1.h"
#include "util/timespec.h"
#include "util/u_atomic.h"
#include "compiler/glsl_types.h"
#include "util/xmlpool.h"

static struct radv_timeline_point *
radv_timeline_find_point_at_least_locked(struct radv_device *device,
                                         struct radv_timeline *timeline,
                                         uint64_t p);

static struct radv_timeline_point *
radv_timeline_add_point_locked(struct radv_device *device,
                               struct radv_timeline *timeline,
                               uint64_t p);

static void
radv_timeline_trigger_waiters_locked(struct radv_timeline *timeline,
                                     struct list_head *processing_list);

static
void radv_destroy_semaphore_part(struct radv_device *device,
                                 struct radv_semaphore_part *part);

static int
radv_device_get_cache_uuid(enum radeon_family family, void *uuid)
{
	struct mesa_sha1 ctx;
	unsigned char sha1[20];
	unsigned ptr_size = sizeof(void*);

	memset(uuid, 0, VK_UUID_SIZE);
	_mesa_sha1_init(&ctx);

	if (!disk_cache_get_function_identifier(radv_device_get_cache_uuid, &ctx) ||
	    !disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
		return -1;

	_mesa_sha1_update(&ctx, &family, sizeof(family));
	_mesa_sha1_update(&ctx, &ptr_size, sizeof(ptr_size));
	_mesa_sha1_final(&ctx, sha1);

	memcpy(uuid, sha1, VK_UUID_SIZE);
	return 0;
}

static void
radv_get_driver_uuid(void *uuid)
{
	ac_compute_driver_uuid(uuid, VK_UUID_SIZE);
}

static void
radv_get_device_uuid(struct radeon_info *info, void *uuid)
{
	ac_compute_device_uuid(info, uuid, VK_UUID_SIZE);
}

static uint64_t
radv_get_visible_vram_size(struct radv_physical_device *device)
{
	return MIN2(device->rad_info.vram_size, device->rad_info.vram_vis_size);
}

static uint64_t
radv_get_vram_size(struct radv_physical_device *device)
{
	return device->rad_info.vram_size - radv_get_visible_vram_size(device);
}

static void
radv_physical_device_init_mem_types(struct radv_physical_device *device)
{
	STATIC_ASSERT(RADV_MEM_HEAP_COUNT <= VK_MAX_MEMORY_HEAPS);
	uint64_t visible_vram_size = radv_get_visible_vram_size(device);
	uint64_t vram_size = radv_get_vram_size(device);
	int vram_index = -1, visible_vram_index = -1, gart_index = -1;
	device->memory_properties.memoryHeapCount = 0;
	if (vram_size > 0) {
		vram_index = device->memory_properties.memoryHeapCount++;
		device->memory_properties.memoryHeaps[vram_index] = (VkMemoryHeap) {
			.size = vram_size,
			.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
		};
	}
	if (visible_vram_size) {
		visible_vram_index = device->memory_properties.memoryHeapCount++;
		device->memory_properties.memoryHeaps[visible_vram_index] = (VkMemoryHeap) {
			.size = visible_vram_size,
			.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
		};
	}
	if (device->rad_info.gart_size > 0) {
		gart_index = device->memory_properties.memoryHeapCount++;
		device->memory_properties.memoryHeaps[gart_index] = (VkMemoryHeap) {
			.size = device->rad_info.gart_size,
			.flags = device->rad_info.has_dedicated_vram ? 0 : VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
		};
	}

	STATIC_ASSERT(RADV_MEM_TYPE_COUNT <= VK_MAX_MEMORY_TYPES);
	unsigned type_count = 0;
	if (vram_index >= 0) {
		device->mem_type_indices[type_count] = RADV_MEM_TYPE_VRAM;
		device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
			.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			.heapIndex = vram_index,
		};
	}
	if (gart_index >= 0 && device->rad_info.has_dedicated_vram) {
		device->mem_type_indices[type_count] = RADV_MEM_TYPE_GTT_WRITE_COMBINE;
		device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
			.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			.heapIndex = gart_index,
		};
	}
	if (visible_vram_index >= 0) {
		device->mem_type_indices[type_count] = RADV_MEM_TYPE_VRAM_CPU_ACCESS;
		device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
			.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			.heapIndex = visible_vram_index,
		};
	}
	if (gart_index >= 0 && !device->rad_info.has_dedicated_vram) {
		/* Put GTT after visible VRAM for GPUs without dedicated VRAM
		 * as they have identical property flags, and according to the
		 * spec, for types with identical flags, the one with greater
		 * performance must be given a lower index. */
		device->mem_type_indices[type_count] = RADV_MEM_TYPE_GTT_WRITE_COMBINE;
		device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
			.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			.heapIndex = gart_index,
		};
	}
	if (gart_index >= 0) {
		device->mem_type_indices[type_count] = RADV_MEM_TYPE_GTT_CACHED;
		device->memory_properties.memoryTypes[type_count++] = (VkMemoryType) {
			.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
			VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
			(device->rad_info.has_dedicated_vram ? 0 : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			.heapIndex = gart_index,
		};
	}
	device->memory_properties.memoryTypeCount = type_count;
}

static void
radv_handle_env_var_force_family(struct radv_physical_device *device)
{
	const char *family = getenv("RADV_FORCE_FAMILY");
	unsigned i;

	if (!family)
		return;

	for (i = CHIP_TAHITI; i < CHIP_LAST; i++) {
		if (!strcmp(family, ac_get_llvm_processor_name(i))) {
			/* Override family and chip_class. */
			device->rad_info.family = i;

			if (i >= CHIP_NAVI10)
				device->rad_info.chip_class = GFX10;
			else if (i >= CHIP_VEGA10)
				device->rad_info.chip_class = GFX9;
			else if (i >= CHIP_TONGA)
				device->rad_info.chip_class = GFX8;
			else if (i >= CHIP_BONAIRE)
				device->rad_info.chip_class = GFX7;
			else
				device->rad_info.chip_class = GFX6;

			return;
		}
	}

	fprintf(stderr, "radv: Unknown family: %s\n", family);
	exit(1);
}

static VkResult
radv_physical_device_init(struct radv_physical_device *device,
			  struct radv_instance *instance,
			  drmDevicePtr drm_device)
{
	const char *path = drm_device->nodes[DRM_NODE_RENDER];
	VkResult result;
	drmVersionPtr version;
	int fd;
	int master_fd = -1;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		if (instance->debug_flags & RADV_DEBUG_STARTUP)
			radv_logi("Could not open device '%s'", path);

		return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);
	}

	version = drmGetVersion(fd);
	if (!version) {
		close(fd);

		if (instance->debug_flags & RADV_DEBUG_STARTUP)
			radv_logi("Could not get the kernel driver version for device '%s'", path);

		return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
				 "failed to get version %s: %m", path);
	}

	if (strcmp(version->name, "amdgpu")) {
		drmFreeVersion(version);
		close(fd);

		if (instance->debug_flags & RADV_DEBUG_STARTUP)
			radv_logi("Device '%s' is not using the amdgpu kernel driver.", path);

		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}
	drmFreeVersion(version);

	if (instance->debug_flags & RADV_DEBUG_STARTUP)
			radv_logi("Found compatible device '%s'.", path);

	device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	device->instance = instance;

	device->ws = radv_amdgpu_winsys_create(fd, instance->debug_flags,
					       instance->perftest_flags);
	if (!device->ws) {
		result = vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);
		goto fail;
	}

	if (instance->enabled_extensions.KHR_display) {
		master_fd = open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
		if (master_fd >= 0) {
			uint32_t accel_working = 0;
			struct drm_amdgpu_info request = {
				.return_pointer = (uintptr_t)&accel_working,
				.return_size = sizeof(accel_working),
				.query = AMDGPU_INFO_ACCEL_WORKING
			};

			if (drmCommandWrite(master_fd, DRM_AMDGPU_INFO, &request, sizeof (struct drm_amdgpu_info)) < 0 || !accel_working) {
				close(master_fd);
				master_fd = -1;
			}
		}
	}

	device->master_fd = master_fd;
	device->local_fd = fd;
	device->ws->query_info(device->ws, &device->rad_info);

	radv_handle_env_var_force_family(device);

	device->use_aco = instance->perftest_flags & RADV_PERFTEST_ACO;
	if (device->rad_info.chip_class < GFX8 && device->use_aco) {
		fprintf(stderr, "WARNING: disabling ACO on unsupported GPUs.\n");
		device->use_aco = false;
	}

	snprintf(device->name, sizeof(device->name),
		 "AMD RADV%s %s (LLVM " MESA_LLVM_VERSION_STRING ")", device->use_aco ? "/ACO" : "",
		 device->rad_info.name);

	if (radv_device_get_cache_uuid(device->rad_info.family, device->cache_uuid)) {
		device->ws->destroy(device->ws);
		result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
				   "cannot generate UUID");
		goto fail;
	}

	/* These flags affect shader compilation. */
	uint64_t shader_env_flags =
		(device->instance->perftest_flags & RADV_PERFTEST_SISCHED ? 0x1 : 0) |
		(device->instance->debug_flags & RADV_DEBUG_UNSAFE_MATH ? 0x2 : 0) |
		(device->use_aco ? 0x4 : 0);

	/* The gpu id is already embedded in the uuid so we just pass "radv"
	 * when creating the cache.
	 */
	char buf[VK_UUID_SIZE * 2 + 1];
	disk_cache_format_hex_id(buf, device->cache_uuid, VK_UUID_SIZE * 2);
	device->disk_cache = disk_cache_create(device->name, buf, shader_env_flags);

	if (device->rad_info.chip_class < GFX8 ||
	    device->rad_info.chip_class > GFX9)
		fprintf(stderr, "WARNING: radv is not a conformant vulkan implementation, testing use only.\n");

	radv_get_driver_uuid(&device->driver_uuid);
	radv_get_device_uuid(&device->rad_info, &device->device_uuid);

	device->out_of_order_rast_allowed = device->rad_info.has_out_of_order_rast &&
					    !(device->instance->debug_flags & RADV_DEBUG_NO_OUT_OF_ORDER);

	device->dcc_msaa_allowed =
		(device->instance->perftest_flags & RADV_PERFTEST_DCC_MSAA);

	device->use_shader_ballot = device->rad_info.chip_class >= GFX8 &&
				    (device->use_aco || device->instance->perftest_flags & RADV_PERFTEST_SHADER_BALLOT);

	device->use_ngg = device->rad_info.chip_class >= GFX10 &&
			  device->rad_info.family != CHIP_NAVI14 &&
			  !(device->instance->debug_flags & RADV_DEBUG_NO_NGG);
	if (device->use_aco && device->use_ngg) {
		fprintf(stderr, "WARNING: disabling NGG because ACO is used.\n");
		device->use_ngg = false;
	}

	device->use_ngg_streamout = false;

	/* Determine the number of threads per wave for all stages. */
	device->cs_wave_size = 64;
	device->ps_wave_size = 64;
	device->ge_wave_size = 64;

	if (device->rad_info.chip_class >= GFX10) {
		if (device->instance->perftest_flags & RADV_PERFTEST_CS_WAVE_32)
			device->cs_wave_size = 32;

		/* For pixel shaders, wave64 is recommanded. */
		if (device->instance->perftest_flags & RADV_PERFTEST_PS_WAVE_32)
			device->ps_wave_size = 32;

		if (device->instance->perftest_flags & RADV_PERFTEST_GE_WAVE_32)
			device->ge_wave_size = 32;
	}

	radv_physical_device_init_mem_types(device);
	radv_fill_device_extension_table(device, &device->supported_extensions);

	device->bus_info = *drm_device->businfo.pci;

	if ((device->instance->debug_flags & RADV_DEBUG_INFO))
		ac_print_gpu_info(&device->rad_info);

	/* The WSI is structured as a layer on top of the driver, so this has
	 * to be the last part of initialization (at least until we get other
	 * semi-layers).
	 */
	result = radv_init_wsi(device);
	if (result != VK_SUCCESS) {
		device->ws->destroy(device->ws);
		vk_error(instance, result);
		goto fail;
	}

	return VK_SUCCESS;

fail:
	close(fd);
	if (master_fd != -1)
		close(master_fd);
	return result;
}

static void
radv_physical_device_finish(struct radv_physical_device *device)
{
	radv_finish_wsi(device);
	device->ws->destroy(device->ws);
	disk_cache_destroy(device->disk_cache);
	close(device->local_fd);
	if (device->master_fd != -1)
		close(device->master_fd);
}

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
	return malloc(size);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
	return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
	free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
	.pUserData = NULL,
	.pfnAllocation = default_alloc_func,
	.pfnReallocation = default_realloc_func,
	.pfnFree = default_free_func,
};

static const struct debug_control radv_debug_options[] = {
	{"nofastclears", RADV_DEBUG_NO_FAST_CLEARS},
	{"nodcc", RADV_DEBUG_NO_DCC},
	{"shaders", RADV_DEBUG_DUMP_SHADERS},
	{"nocache", RADV_DEBUG_NO_CACHE},
	{"shaderstats", RADV_DEBUG_DUMP_SHADER_STATS},
	{"nohiz", RADV_DEBUG_NO_HIZ},
	{"nocompute", RADV_DEBUG_NO_COMPUTE_QUEUE},
	{"unsafemath", RADV_DEBUG_UNSAFE_MATH},
	{"allbos", RADV_DEBUG_ALL_BOS},
	{"noibs", RADV_DEBUG_NO_IBS},
	{"spirv", RADV_DEBUG_DUMP_SPIRV},
	{"vmfaults", RADV_DEBUG_VM_FAULTS},
	{"zerovram", RADV_DEBUG_ZERO_VRAM},
	{"syncshaders", RADV_DEBUG_SYNC_SHADERS},
	{"nosisched", RADV_DEBUG_NO_SISCHED},
	{"preoptir", RADV_DEBUG_PREOPTIR},
	{"nodynamicbounds", RADV_DEBUG_NO_DYNAMIC_BOUNDS},
	{"nooutoforder", RADV_DEBUG_NO_OUT_OF_ORDER},
	{"info", RADV_DEBUG_INFO},
	{"errors", RADV_DEBUG_ERRORS},
	{"startup", RADV_DEBUG_STARTUP},
	{"checkir", RADV_DEBUG_CHECKIR},
	{"nothreadllvm", RADV_DEBUG_NOTHREADLLVM},
	{"nobinning", RADV_DEBUG_NOBINNING},
	{"noloadstoreopt", RADV_DEBUG_NO_LOAD_STORE_OPT},
	{"nongg", RADV_DEBUG_NO_NGG},
	{"noshaderballot", RADV_DEBUG_NO_SHADER_BALLOT},
	{"allentrypoints", RADV_DEBUG_ALL_ENTRYPOINTS},
	{"metashaders", RADV_DEBUG_DUMP_META_SHADERS},
	{"nomemorycache", RADV_DEBUG_NO_MEMORY_CACHE},
	{NULL, 0}
};

const char *
radv_get_debug_option_name(int id)
{
	assert(id < ARRAY_SIZE(radv_debug_options) - 1);
	return radv_debug_options[id].string;
}

static const struct debug_control radv_perftest_options[] = {
	{"nobatchchain", RADV_PERFTEST_NO_BATCHCHAIN},
	{"sisched", RADV_PERFTEST_SISCHED},
	{"localbos", RADV_PERFTEST_LOCAL_BOS},
	{"dccmsaa", RADV_PERFTEST_DCC_MSAA},
	{"bolist", RADV_PERFTEST_BO_LIST},
	{"shader_ballot", RADV_PERFTEST_SHADER_BALLOT},
	{"tccompatcmask", RADV_PERFTEST_TC_COMPAT_CMASK},
	{"cswave32", RADV_PERFTEST_CS_WAVE_32},
	{"pswave32", RADV_PERFTEST_PS_WAVE_32},
	{"gewave32", RADV_PERFTEST_GE_WAVE_32},
	{"dfsm", RADV_PERFTEST_DFSM},
	{"aco", RADV_PERFTEST_ACO},
	{NULL, 0}
};

const char *
radv_get_perftest_option_name(int id)
{
	assert(id < ARRAY_SIZE(radv_perftest_options) - 1);
	return radv_perftest_options[id].string;
}

static void
radv_handle_per_app_options(struct radv_instance *instance,
			    const VkApplicationInfo *info)
{
	const char *name = info ? info->pApplicationName : NULL;

	if (!name)
		return;

	if (!strcmp(name, "Talos - Linux - 32bit") ||
	    !strcmp(name, "Talos - Linux - 64bit")) {
		if (!(instance->debug_flags & RADV_DEBUG_NO_SISCHED)) {
			/* Force enable LLVM sisched for Talos because it looks
			 * safe and it gives few more FPS.
			 */
			instance->perftest_flags |= RADV_PERFTEST_SISCHED;
		}
	} else if (!strcmp(name, "DOOM_VFR")) {
		/* Work around a Doom VFR game bug */
		instance->debug_flags |= RADV_DEBUG_NO_DYNAMIC_BOUNDS;
	} else if (!strcmp(name, "MonsterHunterWorld.exe")) {
		/* Workaround for a WaW hazard when LLVM moves/merges
		 * load/store memory operations.
		 * See https://reviews.llvm.org/D61313
		 */
		if (LLVM_VERSION_MAJOR < 9)
			instance->debug_flags |= RADV_DEBUG_NO_LOAD_STORE_OPT;
	} else if (!strcmp(name, "Wolfenstein: Youngblood")) {
		if (!(instance->debug_flags & RADV_DEBUG_NO_SHADER_BALLOT)) {
			/* Force enable VK_AMD_shader_ballot because it looks
			 * safe and it gives a nice boost (+20% on Vega 56 at
			 * this time).
			 */
			instance->perftest_flags |= RADV_PERFTEST_SHADER_BALLOT;
		}
	} else if (!strcmp(name, "Fledge")) {
		/*
		 * Zero VRAM for "The Surge 2"
		 *
		 * This avoid a hang when when rendering any level. Likely
		 * uninitialized data in an indirect draw.
		 */
		instance->debug_flags |= RADV_DEBUG_ZERO_VRAM;
	}
}

static int radv_get_instance_extension_index(const char *name)
{
	for (unsigned i = 0; i < RADV_INSTANCE_EXTENSION_COUNT; ++i) {
		if (strcmp(name, radv_instance_extensions[i].extensionName) == 0)
			return i;
	}
	return -1;
}

static const char radv_dri_options_xml[] =
DRI_CONF_BEGIN
	DRI_CONF_SECTION_PERFORMANCE
		DRI_CONF_ADAPTIVE_SYNC("true")
		DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
		DRI_CONF_VK_X11_STRICT_IMAGE_COUNT("false")
	DRI_CONF_SECTION_END
DRI_CONF_END;

static void  radv_init_dri_options(struct radv_instance *instance)
{
	driParseOptionInfo(&instance->available_dri_options, radv_dri_options_xml);
	driParseConfigFiles(&instance->dri_options,
	                    &instance->available_dri_options,
	                    0, "radv", NULL,
	                    instance->engineName,
	                    instance->engineVersion);
}

VkResult radv_CreateInstance(
	const VkInstanceCreateInfo*                 pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkInstance*                                 pInstance)
{
	struct radv_instance *instance;
	VkResult result;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

	uint32_t client_version;
	if (pCreateInfo->pApplicationInfo &&
	    pCreateInfo->pApplicationInfo->apiVersion != 0) {
		client_version = pCreateInfo->pApplicationInfo->apiVersion;
	} else {
		client_version = VK_API_VERSION_1_0;
	}

	const char *engine_name = NULL;
	uint32_t engine_version = 0;
	if (pCreateInfo->pApplicationInfo) {
		engine_name = pCreateInfo->pApplicationInfo->pEngineName;
		engine_version = pCreateInfo->pApplicationInfo->engineVersion;
	}

	instance = vk_zalloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	if (!instance)
		return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

	instance->_loader_data.loaderMagic = ICD_LOADER_MAGIC;

	if (pAllocator)
		instance->alloc = *pAllocator;
	else
		instance->alloc = default_alloc;

	instance->apiVersion = client_version;
	instance->physicalDeviceCount = -1;

	/* Get secure compile thread count. NOTE: We cap this at 32 */
#define MAX_SC_PROCS 32
	char *num_sc_threads = getenv("RADV_SECURE_COMPILE_THREADS");
	if (num_sc_threads)
		instance->num_sc_threads = MIN2(strtoul(num_sc_threads, NULL, 10), MAX_SC_PROCS);

	instance->debug_flags = parse_debug_string(getenv("RADV_DEBUG"),
						   radv_debug_options);

	/* Disable memory cache when secure compile is set */
	if (radv_device_use_secure_compile(instance))
		instance->debug_flags |= RADV_DEBUG_NO_MEMORY_CACHE;

	instance->perftest_flags = parse_debug_string(getenv("RADV_PERFTEST"),
						   radv_perftest_options);

	if (instance->perftest_flags & RADV_PERFTEST_ACO)
		fprintf(stderr, "WARNING: Experimental compiler backend enabled. Here be dragons! Incorrect rendering, GPU hangs and/or resets are likely\n");

	if (instance->debug_flags & RADV_DEBUG_STARTUP)
		radv_logi("Created an instance");

	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		const char *ext_name = pCreateInfo->ppEnabledExtensionNames[i];
		int index = radv_get_instance_extension_index(ext_name);

		if (index < 0 || !radv_supported_instance_extensions.extensions[index]) {
			vk_free2(&default_alloc, pAllocator, instance);
			return vk_error(instance, VK_ERROR_EXTENSION_NOT_PRESENT);
		}

		instance->enabled_extensions.extensions[index] = true;
	}

	result = vk_debug_report_instance_init(&instance->debug_report_callbacks);
	if (result != VK_SUCCESS) {
		vk_free2(&default_alloc, pAllocator, instance);
		return vk_error(instance, result);
	}

	instance->engineName = vk_strdup(&instance->alloc, engine_name,
					 VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	instance->engineVersion = engine_version;

	glsl_type_singleton_init_or_ref();

	VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

	radv_init_dri_options(instance);
	radv_handle_per_app_options(instance, pCreateInfo->pApplicationInfo);

	*pInstance = radv_instance_to_handle(instance);

	return VK_SUCCESS;
}

void radv_DestroyInstance(
	VkInstance                                  _instance,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);

	if (!instance)
		return;

	for (int i = 0; i < instance->physicalDeviceCount; ++i) {
		radv_physical_device_finish(instance->physicalDevices + i);
	}

	vk_free(&instance->alloc, instance->engineName);

	VG(VALGRIND_DESTROY_MEMPOOL(instance));

	glsl_type_singleton_decref();

	driDestroyOptionCache(&instance->dri_options);
	driDestroyOptionInfo(&instance->available_dri_options);

	vk_debug_report_instance_destroy(&instance->debug_report_callbacks);

	vk_free(&instance->alloc, instance);
}

static VkResult
radv_enumerate_devices(struct radv_instance *instance)
{
	/* TODO: Check for more devices ? */
	drmDevicePtr devices[8];
	VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;
	int max_devices;

	instance->physicalDeviceCount = 0;

	max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

	if (instance->debug_flags & RADV_DEBUG_STARTUP)
		radv_logi("Found %d drm nodes", max_devices);

	if (max_devices < 1)
		return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);

	for (unsigned i = 0; i < (unsigned)max_devices; i++) {
		if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
		    devices[i]->bustype == DRM_BUS_PCI &&
		    devices[i]->deviceinfo.pci->vendor_id == ATI_VENDOR_ID) {

			result = radv_physical_device_init(instance->physicalDevices +
			                                   instance->physicalDeviceCount,
			                                   instance,
			                                   devices[i]);
			if (result == VK_SUCCESS)
				++instance->physicalDeviceCount;
			else if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
				break;
		}
	}
	drmFreeDevices(devices, max_devices);

	return result;
}

VkResult radv_EnumeratePhysicalDevices(
	VkInstance                                  _instance,
	uint32_t*                                   pPhysicalDeviceCount,
	VkPhysicalDevice*                           pPhysicalDevices)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	VkResult result;

	if (instance->physicalDeviceCount < 0) {
		result = radv_enumerate_devices(instance);
		if (result != VK_SUCCESS &&
		    result != VK_ERROR_INCOMPATIBLE_DRIVER)
			return result;
	}

	if (!pPhysicalDevices) {
		*pPhysicalDeviceCount = instance->physicalDeviceCount;
	} else {
		*pPhysicalDeviceCount = MIN2(*pPhysicalDeviceCount, instance->physicalDeviceCount);
		for (unsigned i = 0; i < *pPhysicalDeviceCount; ++i)
			pPhysicalDevices[i] = radv_physical_device_to_handle(instance->physicalDevices + i);
	}

	return *pPhysicalDeviceCount < instance->physicalDeviceCount ? VK_INCOMPLETE
	                                                             : VK_SUCCESS;
}

VkResult radv_EnumeratePhysicalDeviceGroups(
    VkInstance                                  _instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	VkResult result;

	if (instance->physicalDeviceCount < 0) {
		result = radv_enumerate_devices(instance);
		if (result != VK_SUCCESS &&
		    result != VK_ERROR_INCOMPATIBLE_DRIVER)
			return result;
	}

	if (!pPhysicalDeviceGroupProperties) {
		*pPhysicalDeviceGroupCount = instance->physicalDeviceCount;
	} else {
		*pPhysicalDeviceGroupCount = MIN2(*pPhysicalDeviceGroupCount, instance->physicalDeviceCount);
		for (unsigned i = 0; i < *pPhysicalDeviceGroupCount; ++i) {
			pPhysicalDeviceGroupProperties[i].physicalDeviceCount = 1;
			pPhysicalDeviceGroupProperties[i].physicalDevices[0] = radv_physical_device_to_handle(instance->physicalDevices + i);
			pPhysicalDeviceGroupProperties[i].subsetAllocation = false;
		}
	}
	return *pPhysicalDeviceGroupCount < instance->physicalDeviceCount ? VK_INCOMPLETE
	                                                                  : VK_SUCCESS;
}

void radv_GetPhysicalDeviceFeatures(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceFeatures*                   pFeatures)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	memset(pFeatures, 0, sizeof(*pFeatures));

	*pFeatures = (VkPhysicalDeviceFeatures) {
		.robustBufferAccess                       = true,
		.fullDrawIndexUint32                      = true,
		.imageCubeArray                           = true,
		.independentBlend                         = true,
		.geometryShader                           = true,
		.tessellationShader                       = true,
		.sampleRateShading                        = true,
		.dualSrcBlend                             = true,
		.logicOp                                  = true,
		.multiDrawIndirect                        = true,
		.drawIndirectFirstInstance                = true,
		.depthClamp                               = true,
		.depthBiasClamp                           = true,
		.fillModeNonSolid                         = true,
		.depthBounds                              = true,
		.wideLines                                = true,
		.largePoints                              = true,
		.alphaToOne                               = true,
		.multiViewport                            = true,
		.samplerAnisotropy                        = true,
		.textureCompressionETC2                   = radv_device_supports_etc(pdevice),
		.textureCompressionASTC_LDR               = false,
		.textureCompressionBC                     = true,
		.occlusionQueryPrecise                    = true,
		.pipelineStatisticsQuery                  = true,
		.vertexPipelineStoresAndAtomics           = true,
		.fragmentStoresAndAtomics                 = true,
		.shaderTessellationAndGeometryPointSize   = true,
		.shaderImageGatherExtended                = true,
		.shaderStorageImageExtendedFormats        = true,
		.shaderStorageImageMultisample            = pdevice->rad_info.chip_class >= GFX8,
		.shaderUniformBufferArrayDynamicIndexing  = true,
		.shaderSampledImageArrayDynamicIndexing   = true,
		.shaderStorageBufferArrayDynamicIndexing  = true,
		.shaderStorageImageArrayDynamicIndexing   = true,
		.shaderStorageImageReadWithoutFormat      = true,
		.shaderStorageImageWriteWithoutFormat     = true,
		.shaderClipDistance                       = true,
		.shaderCullDistance                       = true,
		.shaderFloat64                            = true,
		.shaderInt64                              = true,
		.shaderInt16                              = pdevice->rad_info.chip_class >= GFX9 && !pdevice->use_aco,
		.sparseBinding                            = true,
		.variableMultisampleRate                  = true,
		.inheritedQueries                         = true,
	};
}

void radv_GetPhysicalDeviceFeatures2(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceFeatures2                  *pFeatures)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	vk_foreach_struct(ext, pFeatures->pNext) {
		switch (ext->sType) {
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
			VkPhysicalDeviceVariablePointersFeatures *features = (void *)ext;
			features->variablePointersStorageBuffer = true;
			features->variablePointers = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
			VkPhysicalDeviceMultiviewFeatures *features = (VkPhysicalDeviceMultiviewFeatures*)ext;
			features->multiview = true;
			features->multiviewGeometryShader = true;
			features->multiviewTessellationShader = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
			VkPhysicalDeviceShaderDrawParametersFeatures *features =
			    (VkPhysicalDeviceShaderDrawParametersFeatures*)ext;
			features->shaderDrawParameters = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: {
			VkPhysicalDeviceProtectedMemoryFeatures *features =
			    (VkPhysicalDeviceProtectedMemoryFeatures*)ext;
			features->protectedMemory = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
			VkPhysicalDevice16BitStorageFeatures *features =
			    (VkPhysicalDevice16BitStorageFeatures*)ext;
			bool enabled = pdevice->rad_info.chip_class >= GFX8 && !pdevice->use_aco;
			features->storageBuffer16BitAccess = enabled;
			features->uniformAndStorageBuffer16BitAccess = enabled;
			features->storagePushConstant16 = enabled;
			features->storageInputOutput16 = enabled && LLVM_VERSION_MAJOR >= 9;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
			VkPhysicalDeviceSamplerYcbcrConversionFeatures *features =
			    (VkPhysicalDeviceSamplerYcbcrConversionFeatures*)ext;
			features->samplerYcbcrConversion = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT: {
			VkPhysicalDeviceDescriptorIndexingFeaturesEXT *features =
				(VkPhysicalDeviceDescriptorIndexingFeaturesEXT*)ext;
			features->shaderInputAttachmentArrayDynamicIndexing = true;
			features->shaderUniformTexelBufferArrayDynamicIndexing = true;
			features->shaderStorageTexelBufferArrayDynamicIndexing = true;
			features->shaderUniformBufferArrayNonUniformIndexing = true;
			features->shaderSampledImageArrayNonUniformIndexing = true;
			features->shaderStorageBufferArrayNonUniformIndexing = true;
			features->shaderStorageImageArrayNonUniformIndexing = true;
			features->shaderInputAttachmentArrayNonUniformIndexing = true;
			features->shaderUniformTexelBufferArrayNonUniformIndexing = true;
			features->shaderStorageTexelBufferArrayNonUniformIndexing = true;
			features->descriptorBindingUniformBufferUpdateAfterBind = true;
			features->descriptorBindingSampledImageUpdateAfterBind = true;
			features->descriptorBindingStorageImageUpdateAfterBind = true;
			features->descriptorBindingStorageBufferUpdateAfterBind = true;
			features->descriptorBindingUniformTexelBufferUpdateAfterBind = true;
			features->descriptorBindingStorageTexelBufferUpdateAfterBind = true;
			features->descriptorBindingUpdateUnusedWhilePending = true;
			features->descriptorBindingPartiallyBound = true;
			features->descriptorBindingVariableDescriptorCount = true;
			features->runtimeDescriptorArray = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: {
			VkPhysicalDeviceConditionalRenderingFeaturesEXT *features =
				(VkPhysicalDeviceConditionalRenderingFeaturesEXT*)ext;
			features->conditionalRendering = true;
			features->inheritedConditionalRendering = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
			VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
				(VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)ext;
			features->vertexAttributeInstanceRateDivisor = VK_TRUE;
			features->vertexAttributeInstanceRateZeroDivisor = VK_TRUE;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
			VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
				(VkPhysicalDeviceTransformFeedbackFeaturesEXT*)ext;
			features->transformFeedback = true;
			features->geometryStreams = !pdevice->use_ngg_streamout;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT: {
			VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *features =
				(VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *)ext;
			features->scalarBlockLayout = pdevice->rad_info.chip_class >= GFX7;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: {
			VkPhysicalDeviceMemoryPriorityFeaturesEXT *features =
				(VkPhysicalDeviceMemoryPriorityFeaturesEXT *)ext;
			features->memoryPriority = VK_TRUE;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: {
			VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *features =
				(VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *)ext;
			features->bufferDeviceAddress = true;
			features->bufferDeviceAddressCaptureReplay = false;
			features->bufferDeviceAddressMultiDevice = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
			VkPhysicalDeviceDepthClipEnableFeaturesEXT *features =
				(VkPhysicalDeviceDepthClipEnableFeaturesEXT *)ext;
			features->depthClipEnable = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT: {
			VkPhysicalDeviceHostQueryResetFeaturesEXT *features =
				(VkPhysicalDeviceHostQueryResetFeaturesEXT *)ext;
			features->hostQueryReset = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR: {
			VkPhysicalDevice8BitStorageFeaturesKHR *features =
			    (VkPhysicalDevice8BitStorageFeaturesKHR*)ext;
			bool enabled = pdevice->rad_info.chip_class >= GFX8 && !pdevice->use_aco;
			features->storageBuffer8BitAccess = enabled;
			features->uniformAndStorageBuffer8BitAccess = enabled;
			features->storagePushConstant8 = enabled;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR: {
			VkPhysicalDeviceShaderFloat16Int8FeaturesKHR *features =
				(VkPhysicalDeviceShaderFloat16Int8FeaturesKHR*)ext;
			features->shaderFloat16 = pdevice->rad_info.chip_class >= GFX8 && !pdevice->use_aco;
			features->shaderInt8 = !pdevice->use_aco;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR: {
			VkPhysicalDeviceShaderAtomicInt64FeaturesKHR *features =
				(VkPhysicalDeviceShaderAtomicInt64FeaturesKHR *)ext;
			features->shaderBufferInt64Atomics = LLVM_VERSION_MAJOR >= 9;
			features->shaderSharedInt64Atomics = LLVM_VERSION_MAJOR >= 9;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT: {
			VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *features =
				(VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *)ext;
			features->shaderDemoteToHelperInvocation = pdevice->use_aco;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT: {
			VkPhysicalDeviceInlineUniformBlockFeaturesEXT *features =
				(VkPhysicalDeviceInlineUniformBlockFeaturesEXT *)ext;

			features->inlineUniformBlock = true;
			features->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV: {
			VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *features =
				(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *)ext;
			features->computeDerivativeGroupQuads = false;
			features->computeDerivativeGroupLinear = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: {
			VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *features =
				(VkPhysicalDeviceYcbcrImageArraysFeaturesEXT*)ext;
			features->ycbcrImageArrays = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR: {
			VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR *features =
				(VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR *)ext;
			features->uniformBufferStandardLayout = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
			VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
				(VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
			features->indexTypeUint8 = pdevice->rad_info.chip_class >= GFX8;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR: {
			VkPhysicalDeviceImagelessFramebufferFeaturesKHR *features =
				(VkPhysicalDeviceImagelessFramebufferFeaturesKHR *)ext;
			features->imagelessFramebuffer = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: {
			VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *features =
				(VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)ext;
			features->pipelineExecutableInfo = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: {
			VkPhysicalDeviceShaderClockFeaturesKHR *features =
				(VkPhysicalDeviceShaderClockFeaturesKHR *)ext;
			features->shaderSubgroupClock = true;
			features->shaderDeviceClock = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
			VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
				(VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)ext;
			features->texelBufferAlignment = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR: {
			VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *features =
				(VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *) ext;
			features->timelineSemaphore = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT: {
			VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *features =
				(VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *)ext;
			features->subgroupSizeControl = true;
			features->computeFullSubgroups = true;
			break;
		}
		default:
			break;
		}
	}
	return radv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
}

void radv_GetPhysicalDeviceProperties(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceProperties*                 pProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	VkSampleCountFlags sample_counts = 0xf;

	/* make sure that the entire descriptor set is addressable with a signed
	 * 32-bit int. So the sum of all limits scaled by descriptor size has to
	 * be at most 2 GiB. the combined image & samples object count as one of
	 * both. This limit is for the pipeline layout, not for the set layout, but
	 * there is no set limit, so we just set a pipeline limit. I don't think
	 * any app is going to hit this soon. */
	size_t max_descriptor_set_size = ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS) /
	          (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
	           32 /* storage buffer, 32 due to potential space wasted on alignment */ +
	           32 /* sampler, largest when combined with image */ +
	           64 /* sampled image */ +
	           64 /* storage image */);

	VkPhysicalDeviceLimits limits = {
		.maxImageDimension1D                      = (1 << 14),
		.maxImageDimension2D                      = (1 << 14),
		.maxImageDimension3D                      = (1 << 11),
		.maxImageDimensionCube                    = (1 << 14),
		.maxImageArrayLayers                      = (1 << 11),
		.maxTexelBufferElements                   = 128 * 1024 * 1024,
		.maxUniformBufferRange                    = UINT32_MAX,
		.maxStorageBufferRange                    = UINT32_MAX,
		.maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
		.maxMemoryAllocationCount                 = UINT32_MAX,
		.maxSamplerAllocationCount                = 64 * 1024,
		.bufferImageGranularity                   = 64, /* A cache line */
		.sparseAddressSpaceSize                   = 0xffffffffu, /* buffer max size */
		.maxBoundDescriptorSets                   = MAX_SETS,
		.maxPerStageDescriptorSamplers            = max_descriptor_set_size,
		.maxPerStageDescriptorUniformBuffers      = max_descriptor_set_size,
		.maxPerStageDescriptorStorageBuffers      = max_descriptor_set_size,
		.maxPerStageDescriptorSampledImages       = max_descriptor_set_size,
		.maxPerStageDescriptorStorageImages       = max_descriptor_set_size,
		.maxPerStageDescriptorInputAttachments    = max_descriptor_set_size,
		.maxPerStageResources                     = max_descriptor_set_size,
		.maxDescriptorSetSamplers                 = max_descriptor_set_size,
		.maxDescriptorSetUniformBuffers           = max_descriptor_set_size,
		.maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_UNIFORM_BUFFERS,
		.maxDescriptorSetStorageBuffers           = max_descriptor_set_size,
		.maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_STORAGE_BUFFERS,
		.maxDescriptorSetSampledImages            = max_descriptor_set_size,
		.maxDescriptorSetStorageImages            = max_descriptor_set_size,
		.maxDescriptorSetInputAttachments         = max_descriptor_set_size,
		.maxVertexInputAttributes                 = MAX_VERTEX_ATTRIBS,
		.maxVertexInputBindings                   = MAX_VBS,
		.maxVertexInputAttributeOffset            = 2047,
		.maxVertexInputBindingStride              = 2048,
		.maxVertexOutputComponents                = 128,
		.maxTessellationGenerationLevel           = 64,
		.maxTessellationPatchSize                 = 32,
		.maxTessellationControlPerVertexInputComponents = 128,
		.maxTessellationControlPerVertexOutputComponents = 128,
		.maxTessellationControlPerPatchOutputComponents = 120,
		.maxTessellationControlTotalOutputComponents = 4096,
		.maxTessellationEvaluationInputComponents = 128,
		.maxTessellationEvaluationOutputComponents = 128,
		.maxGeometryShaderInvocations             = 127,
		.maxGeometryInputComponents               = 64,
		.maxGeometryOutputComponents              = 128,
		.maxGeometryOutputVertices                = 256,
		.maxGeometryTotalOutputComponents         = 1024,
		.maxFragmentInputComponents               = 128,
		.maxFragmentOutputAttachments             = 8,
		.maxFragmentDualSrcAttachments            = 1,
		.maxFragmentCombinedOutputResources       = 8,
		.maxComputeSharedMemorySize               = 32768,
		.maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
		.maxComputeWorkGroupInvocations           = 2048,
		.maxComputeWorkGroupSize = {
			2048,
			2048,
			2048
		},
		.subPixelPrecisionBits                    = 8,
		.subTexelPrecisionBits                    = 8,
		.mipmapPrecisionBits                      = 8,
		.maxDrawIndexedIndexValue                 = UINT32_MAX,
		.maxDrawIndirectCount                     = UINT32_MAX,
		.maxSamplerLodBias                        = 16,
		.maxSamplerAnisotropy                     = 16,
		.maxViewports                             = MAX_VIEWPORTS,
		.maxViewportDimensions                    = { (1 << 14), (1 << 14) },
		.viewportBoundsRange                      = { INT16_MIN, INT16_MAX },
		.viewportSubPixelBits                     = 8,
		.minMemoryMapAlignment                    = 4096, /* A page */
		.minTexelBufferOffsetAlignment            = 4,
		.minUniformBufferOffsetAlignment          = 4,
		.minStorageBufferOffsetAlignment          = 4,
		.minTexelOffset                           = -32,
		.maxTexelOffset                           = 31,
		.minTexelGatherOffset                     = -32,
		.maxTexelGatherOffset                     = 31,
		.minInterpolationOffset                   = -2,
		.maxInterpolationOffset                   = 2,
		.subPixelInterpolationOffsetBits          = 8,
		.maxFramebufferWidth                      = (1 << 14),
		.maxFramebufferHeight                     = (1 << 14),
		.maxFramebufferLayers                     = (1 << 10),
		.framebufferColorSampleCounts             = sample_counts,
		.framebufferDepthSampleCounts             = sample_counts,
		.framebufferStencilSampleCounts           = sample_counts,
		.framebufferNoAttachmentsSampleCounts     = sample_counts,
		.maxColorAttachments                      = MAX_RTS,
		.sampledImageColorSampleCounts            = sample_counts,
		.sampledImageIntegerSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
		.sampledImageDepthSampleCounts            = sample_counts,
		.sampledImageStencilSampleCounts          = sample_counts,
		.storageImageSampleCounts                 = pdevice->rad_info.chip_class >= GFX8 ? sample_counts : VK_SAMPLE_COUNT_1_BIT,
		.maxSampleMaskWords                       = 1,
		.timestampComputeAndGraphics              = true,
		.timestampPeriod                          = 1000000.0 / pdevice->rad_info.clock_crystal_freq,
		.maxClipDistances                         = 8,
		.maxCullDistances                         = 8,
		.maxCombinedClipAndCullDistances          = 8,
		.discreteQueuePriorities                  = 2,
		.pointSizeRange                           = { 0.0, 8192.0 },
		.lineWidthRange                           = { 0.0, 7.9921875 },
		.pointSizeGranularity                     = (1.0 / 8.0),
		.lineWidthGranularity                     = (1.0 / 128.0),
		.strictLines                              = false, /* FINISHME */
		.standardSampleLocations                  = true,
		.optimalBufferCopyOffsetAlignment         = 128,
		.optimalBufferCopyRowPitchAlignment       = 128,
		.nonCoherentAtomSize                      = 64,
	};

	*pProperties = (VkPhysicalDeviceProperties) {
		.apiVersion = radv_physical_device_api_version(pdevice),
		.driverVersion = vk_get_driver_version(),
		.vendorID = ATI_VENDOR_ID,
		.deviceID = pdevice->rad_info.pci_id,
		.deviceType = pdevice->rad_info.has_dedicated_vram ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
		.limits = limits,
		.sparseProperties = {0},
	};

	strcpy(pProperties->deviceName, pdevice->name);
	memcpy(pProperties->pipelineCacheUUID, pdevice->cache_uuid, VK_UUID_SIZE);
}

void radv_GetPhysicalDeviceProperties2(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceProperties2                *pProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	radv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

	vk_foreach_struct(ext, pProperties->pNext) {
		switch (ext->sType) {
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
			VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
				(VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;
			properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
			VkPhysicalDeviceIDProperties *properties = (VkPhysicalDeviceIDProperties*)ext;
			memcpy(properties->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
			memcpy(properties->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
			properties->deviceLUIDValid = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
			VkPhysicalDeviceMultiviewProperties *properties = (VkPhysicalDeviceMultiviewProperties*)ext;
			properties->maxMultiviewViewCount = MAX_VIEWS;
			properties->maxMultiviewInstanceIndex = INT_MAX;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
			VkPhysicalDevicePointClippingProperties *properties =
			    (VkPhysicalDevicePointClippingProperties*)ext;
			properties->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
			break;
		}
		case  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: {
			VkPhysicalDeviceDiscardRectanglePropertiesEXT *properties =
			    (VkPhysicalDeviceDiscardRectanglePropertiesEXT*)ext;
			properties->maxDiscardRectangles = MAX_DISCARD_RECTANGLES;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
			VkPhysicalDeviceExternalMemoryHostPropertiesEXT *properties =
			    (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *) ext;
			properties->minImportedHostPointerAlignment = 4096;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
			VkPhysicalDeviceSubgroupProperties *properties =
			    (VkPhysicalDeviceSubgroupProperties*)ext;
			properties->subgroupSize = 64;
			properties->supportedStages = VK_SHADER_STAGE_ALL;
			properties->supportedOperations =
							VK_SUBGROUP_FEATURE_BASIC_BIT |
							VK_SUBGROUP_FEATURE_BALLOT_BIT |
							VK_SUBGROUP_FEATURE_QUAD_BIT |
							VK_SUBGROUP_FEATURE_VOTE_BIT;
			if (pdevice->rad_info.chip_class >= GFX8) {
				properties->supportedOperations |=
							VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
							VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
							VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
							VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
			}
			properties->quadOperationsInAllStages = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
			VkPhysicalDeviceMaintenance3Properties *properties =
			    (VkPhysicalDeviceMaintenance3Properties*)ext;
			/* Make sure everything is addressable by a signed 32-bit int, and
			 * our largest descriptors are 96 bytes. */
			properties->maxPerSetDescriptors = (1ull << 31) / 96;
			/* Our buffer size fields allow only this much */
			properties->maxMemoryAllocationSize = 0xFFFFFFFFull;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES_EXT: {
			VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT *properties =
				(VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT *)ext;
			/* GFX6-8 only support single channel min/max filter. */
			properties->filterMinmaxImageComponentMapping = pdevice->rad_info.chip_class >= GFX9;
			properties->filterMinmaxSingleComponentFormats = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: {
			VkPhysicalDeviceShaderCorePropertiesAMD *properties =
				(VkPhysicalDeviceShaderCorePropertiesAMD *)ext;

			/* Shader engines. */
			properties->shaderEngineCount =
				pdevice->rad_info.max_se;
			properties->shaderArraysPerEngineCount =
				pdevice->rad_info.max_sh_per_se;
			properties->computeUnitsPerShaderArray =
				pdevice->rad_info.num_good_cu_per_sh;
			properties->simdPerComputeUnit = 4;
			properties->wavefrontsPerSimd =
				pdevice->rad_info.family == CHIP_TONGA ||
				pdevice->rad_info.family == CHIP_ICELAND ||
				pdevice->rad_info.family == CHIP_POLARIS10 ||
				pdevice->rad_info.family == CHIP_POLARIS11 ||
				pdevice->rad_info.family == CHIP_POLARIS12 ||
				pdevice->rad_info.family == CHIP_VEGAM ? 8 : 10;
			properties->wavefrontSize = 64;

			/* SGPR. */
			properties->sgprsPerSimd =
				pdevice->rad_info.num_physical_sgprs_per_simd;
			properties->minSgprAllocation =
				pdevice->rad_info.chip_class >= GFX8 ? 16 : 8;
			properties->maxSgprAllocation =
				pdevice->rad_info.family == CHIP_TONGA ||
				pdevice->rad_info.family == CHIP_ICELAND ? 96 : 104;
			properties->sgprAllocationGranularity =
				pdevice->rad_info.chip_class >= GFX8 ? 16 : 8;

			/* VGPR. */
			properties->vgprsPerSimd = RADV_NUM_PHYSICAL_VGPRS;
			properties->minVgprAllocation = 4;
			properties->maxVgprAllocation = 256;
			properties->vgprAllocationGranularity = 4;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: {
			VkPhysicalDeviceShaderCoreProperties2AMD *properties =
				(VkPhysicalDeviceShaderCoreProperties2AMD *)ext;

			properties->shaderCoreFeatures = 0;
			properties->activeComputeUnitCount =
				pdevice->rad_info.num_good_compute_units;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
			VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *properties =
				(VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
			properties->maxVertexAttribDivisor = UINT32_MAX;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT: {
			VkPhysicalDeviceDescriptorIndexingPropertiesEXT *properties =
				(VkPhysicalDeviceDescriptorIndexingPropertiesEXT*)ext;
			properties->maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX / 64;
			properties->shaderUniformBufferArrayNonUniformIndexingNative = false;
			properties->shaderSampledImageArrayNonUniformIndexingNative = false;
			properties->shaderStorageBufferArrayNonUniformIndexingNative = false;
			properties->shaderStorageImageArrayNonUniformIndexingNative = false;
			properties->shaderInputAttachmentArrayNonUniformIndexingNative = false;
			properties->robustBufferAccessUpdateAfterBind = false;
			properties->quadDivergentImplicitLod = false;

			size_t max_descriptor_set_size = ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS -
				MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_INLINE_UNIFORM_BLOCK_COUNT) /
			          (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
			           32 /* storage buffer, 32 due to potential space wasted on alignment */ +
			           32 /* sampler, largest when combined with image */ +
			           64 /* sampled image */ +
			           64 /* storage image */);
			properties->maxPerStageDescriptorUpdateAfterBindSamplers = max_descriptor_set_size;
			properties->maxPerStageDescriptorUpdateAfterBindUniformBuffers = max_descriptor_set_size;
			properties->maxPerStageDescriptorUpdateAfterBindStorageBuffers = max_descriptor_set_size;
			properties->maxPerStageDescriptorUpdateAfterBindSampledImages = max_descriptor_set_size;
			properties->maxPerStageDescriptorUpdateAfterBindStorageImages = max_descriptor_set_size;
			properties->maxPerStageDescriptorUpdateAfterBindInputAttachments = max_descriptor_set_size;
			properties->maxPerStageUpdateAfterBindResources = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindSamplers = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindUniformBuffers = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
			properties->maxDescriptorSetUpdateAfterBindStorageBuffers = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
			properties->maxDescriptorSetUpdateAfterBindSampledImages = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindStorageImages = max_descriptor_set_size;
			properties->maxDescriptorSetUpdateAfterBindInputAttachments = max_descriptor_set_size;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
			VkPhysicalDeviceProtectedMemoryProperties *properties =
				(VkPhysicalDeviceProtectedMemoryProperties *)ext;
			properties->protectedNoFault = false;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: {
			VkPhysicalDeviceConservativeRasterizationPropertiesEXT *properties =
				(VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)ext;
			properties->primitiveOverestimationSize = 0;
			properties->maxExtraPrimitiveOverestimationSize = 0;
			properties->extraPrimitiveOverestimationSizeGranularity = 0;
			properties->primitiveUnderestimation = VK_FALSE;
			properties->conservativePointAndLineRasterization = VK_FALSE;
			properties->degenerateTrianglesRasterized = VK_FALSE;
			properties->degenerateLinesRasterized = VK_FALSE;
			properties->fullyCoveredFragmentShaderInputVariable = VK_FALSE;
			properties->conservativeRasterizationPostDepthCoverage = VK_FALSE;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: {
			VkPhysicalDevicePCIBusInfoPropertiesEXT *properties =
				(VkPhysicalDevicePCIBusInfoPropertiesEXT *)ext;
			properties->pciDomain = pdevice->bus_info.domain;
			properties->pciBus = pdevice->bus_info.bus;
			properties->pciDevice = pdevice->bus_info.dev;
			properties->pciFunction = pdevice->bus_info.func;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR: {
			VkPhysicalDeviceDriverPropertiesKHR *driver_props =
				(VkPhysicalDeviceDriverPropertiesKHR *) ext;

			driver_props->driverID = VK_DRIVER_ID_MESA_RADV_KHR;
			snprintf(driver_props->driverName, VK_MAX_DRIVER_NAME_SIZE_KHR, "radv");
			snprintf(driver_props->driverInfo, VK_MAX_DRIVER_INFO_SIZE_KHR,
				"Mesa " PACKAGE_VERSION MESA_GIT_SHA1
				" (LLVM " MESA_LLVM_VERSION_STRING ")");

			driver_props->conformanceVersion = (VkConformanceVersionKHR) {
				.major = 1,
				.minor = 1,
				.subminor = 2,
				.patch = 0,
			};
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
			VkPhysicalDeviceTransformFeedbackPropertiesEXT *properties =
				(VkPhysicalDeviceTransformFeedbackPropertiesEXT *)ext;
			properties->maxTransformFeedbackStreams = MAX_SO_STREAMS;
			properties->maxTransformFeedbackBuffers = MAX_SO_BUFFERS;
			properties->maxTransformFeedbackBufferSize = UINT32_MAX;
			properties->maxTransformFeedbackStreamDataSize = 512;
			properties->maxTransformFeedbackBufferDataSize = UINT32_MAX;
			properties->maxTransformFeedbackBufferDataStride = 512;
			properties->transformFeedbackQueries = !pdevice->use_ngg_streamout;
			properties->transformFeedbackStreamsLinesTriangles = !pdevice->use_ngg_streamout;
			properties->transformFeedbackRasterizationStreamSelect = false;
			properties->transformFeedbackDraw = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT: {
			VkPhysicalDeviceInlineUniformBlockPropertiesEXT *props =
				(VkPhysicalDeviceInlineUniformBlockPropertiesEXT *)ext;

			props->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
			props->maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
			props->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
			props->maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
			props->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: {
			VkPhysicalDeviceSampleLocationsPropertiesEXT *properties =
				(VkPhysicalDeviceSampleLocationsPropertiesEXT *)ext;
			properties->sampleLocationSampleCounts = VK_SAMPLE_COUNT_2_BIT |
								 VK_SAMPLE_COUNT_4_BIT |
								 VK_SAMPLE_COUNT_8_BIT;
			properties->maxSampleLocationGridSize = (VkExtent2D){ 2 , 2 };
			properties->sampleLocationCoordinateRange[0] = 0.0f;
			properties->sampleLocationCoordinateRange[1] = 0.9375f;
			properties->sampleLocationSubPixelBits = 4;
			properties->variableSampleLocations = VK_FALSE;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES_KHR: {
			VkPhysicalDeviceDepthStencilResolvePropertiesKHR *properties =
				(VkPhysicalDeviceDepthStencilResolvePropertiesKHR *)ext;

			/* We support all of the depth resolve modes */
			properties->supportedDepthResolveModes =
				VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR |
				VK_RESOLVE_MODE_AVERAGE_BIT_KHR |
				VK_RESOLVE_MODE_MIN_BIT_KHR |
				VK_RESOLVE_MODE_MAX_BIT_KHR;

			/* Average doesn't make sense for stencil so we don't support that */
			properties->supportedStencilResolveModes =
				VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR |
				VK_RESOLVE_MODE_MIN_BIT_KHR |
				VK_RESOLVE_MODE_MAX_BIT_KHR;

			properties->independentResolveNone = VK_TRUE;
			properties->independentResolve = VK_TRUE;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES_EXT: {
			VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *properties =
				(VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *)ext;
			properties->storageTexelBufferOffsetAlignmentBytes = 4;
			properties->storageTexelBufferOffsetSingleTexelAlignment = true;
			properties->uniformTexelBufferOffsetAlignmentBytes = 4;
			properties->uniformTexelBufferOffsetSingleTexelAlignment = true;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR : {
			VkPhysicalDeviceFloatControlsPropertiesKHR *properties =
				(VkPhysicalDeviceFloatControlsPropertiesKHR *)ext;

			/* On AMD hardware, denormals and rounding modes for
			 * fp16/fp64 are controlled by the same config
			 * register.
			 */
			properties->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY_KHR;
			properties->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY_KHR;

			/* Do not allow both preserving and flushing denorms
			 * because different shaders in the same pipeline can
			 * have different settings and this won't work for
			 * merged shaders. To make it work, this requires LLVM
			 * support for changing the register. The same logic
			 * applies for the rounding modes because they are
			 * configured with the same config register.
			 */
			properties->shaderDenormFlushToZeroFloat32 = true;
			properties->shaderDenormPreserveFloat32 = false;
			properties->shaderRoundingModeRTEFloat32 = true;
			properties->shaderRoundingModeRTZFloat32 = false;
			properties->shaderSignedZeroInfNanPreserveFloat32 = true;

			properties->shaderDenormFlushToZeroFloat16 = false;
			properties->shaderDenormPreserveFloat16 = pdevice->rad_info.chip_class >= GFX8;
			properties->shaderRoundingModeRTEFloat16 = pdevice->rad_info.chip_class >= GFX8;
			properties->shaderRoundingModeRTZFloat16 = false;
			properties->shaderSignedZeroInfNanPreserveFloat16 = pdevice->rad_info.chip_class >= GFX8;

			properties->shaderDenormFlushToZeroFloat64 = false;
			properties->shaderDenormPreserveFloat64 = pdevice->rad_info.chip_class >= GFX8;
			properties->shaderRoundingModeRTEFloat64 = pdevice->rad_info.chip_class >= GFX8;
			properties->shaderRoundingModeRTZFloat64 = false;
			properties->shaderSignedZeroInfNanPreserveFloat64 = pdevice->rad_info.chip_class >= GFX8;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR: {
			VkPhysicalDeviceTimelineSemaphorePropertiesKHR *props =
				(VkPhysicalDeviceTimelineSemaphorePropertiesKHR *) ext;
			props->maxTimelineSemaphoreValueDifference = UINT64_MAX;
			break;
		}
		case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT: {
			VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *props =
				(VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *)ext;
			props->minSubgroupSize = 64;
			props->maxSubgroupSize = 64;
			props->maxComputeWorkgroupSubgroups = UINT32_MAX;
			props->requiredSubgroupSizeStages = 0;

			if (pdevice->rad_info.chip_class >= GFX10) {
				/* Only GFX10+ supports wave32. */
				props->minSubgroupSize = 32;
				props->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
			}
			break;
		}
		default:
			break;
		}
	}
}

static void radv_get_physical_device_queue_family_properties(
	struct radv_physical_device*                pdevice,
	uint32_t*                                   pCount,
	VkQueueFamilyProperties**                    pQueueFamilyProperties)
{
	int num_queue_families = 1;
	int idx;
	if (pdevice->rad_info.num_compute_rings > 0 &&
	    !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE))
		num_queue_families++;

	if (pQueueFamilyProperties == NULL) {
		*pCount = num_queue_families;
		return;
	}

	if (!*pCount)
		return;

	idx = 0;
	if (*pCount >= 1) {
		*pQueueFamilyProperties[idx] = (VkQueueFamilyProperties) {
			.queueFlags = VK_QUEUE_GRAPHICS_BIT |
			              VK_QUEUE_COMPUTE_BIT |
			              VK_QUEUE_TRANSFER_BIT |
			              VK_QUEUE_SPARSE_BINDING_BIT,
			.queueCount = 1,
			.timestampValidBits = 64,
			.minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
		};
		idx++;
	}

	if (pdevice->rad_info.num_compute_rings > 0 &&
	    !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
		if (*pCount > idx) {
			*pQueueFamilyProperties[idx] = (VkQueueFamilyProperties) {
				.queueFlags = VK_QUEUE_COMPUTE_BIT |
				              VK_QUEUE_TRANSFER_BIT |
				              VK_QUEUE_SPARSE_BINDING_BIT,
				.queueCount = pdevice->rad_info.num_compute_rings,
				.timestampValidBits = 64,
				.minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
			};
			idx++;
		}
	}
	*pCount = idx;
}

void radv_GetPhysicalDeviceQueueFamilyProperties(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pCount,
	VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	if (!pQueueFamilyProperties) {
		radv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
		return;
	}
	VkQueueFamilyProperties *properties[] = {
		pQueueFamilyProperties + 0,
		pQueueFamilyProperties + 1,
		pQueueFamilyProperties + 2,
	};
	radv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
	assert(*pCount <= 3);
}

void radv_GetPhysicalDeviceQueueFamilyProperties2(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pCount,
	VkQueueFamilyProperties2                   *pQueueFamilyProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	if (!pQueueFamilyProperties) {
		radv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
		return;
	}
	VkQueueFamilyProperties *properties[] = {
		&pQueueFamilyProperties[0].queueFamilyProperties,
		&pQueueFamilyProperties[1].queueFamilyProperties,
		&pQueueFamilyProperties[2].queueFamilyProperties,
	};
	radv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
	assert(*pCount <= 3);
}

void radv_GetPhysicalDeviceMemoryProperties(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceMemoryProperties           *pMemoryProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);

	*pMemoryProperties = physical_device->memory_properties;
}

static void
radv_get_memory_budget_properties(VkPhysicalDevice physicalDevice,
				  VkPhysicalDeviceMemoryBudgetPropertiesEXT *memoryBudget)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	VkPhysicalDeviceMemoryProperties *memory_properties = &device->memory_properties;
	uint64_t visible_vram_size = radv_get_visible_vram_size(device);
	uint64_t vram_size = radv_get_vram_size(device);
	uint64_t gtt_size = device->rad_info.gart_size;
	uint64_t heap_budget, heap_usage;

	/* For all memory heaps, the computation of budget is as follow:
	 *	heap_budget = heap_size - global_heap_usage + app_heap_usage
	 *
	 * The Vulkan spec 1.1.97 says that the budget should include any
	 * currently allocated device memory.
	 *
	 * Note that the application heap usages are not really accurate (eg.
	 * in presence of shared buffers).
	 */
	for (int i = 0; i < device->memory_properties.memoryTypeCount; i++) {
		uint32_t heap_index = device->memory_properties.memoryTypes[i].heapIndex;

		switch (device->mem_type_indices[i]) {
		case RADV_MEM_TYPE_VRAM:
			heap_usage = device->ws->query_value(device->ws,
							     RADEON_ALLOCATED_VRAM);

			heap_budget = vram_size -
				device->ws->query_value(device->ws, RADEON_VRAM_USAGE) +
				heap_usage;

			memoryBudget->heapBudget[heap_index] = heap_budget;
			memoryBudget->heapUsage[heap_index] = heap_usage;
			break;
		case RADV_MEM_TYPE_VRAM_CPU_ACCESS:
			heap_usage = device->ws->query_value(device->ws,
							     RADEON_ALLOCATED_VRAM_VIS);

			heap_budget = visible_vram_size -
				device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE) +
				heap_usage;

			memoryBudget->heapBudget[heap_index] = heap_budget;
			memoryBudget->heapUsage[heap_index] = heap_usage;
			break;
		case RADV_MEM_TYPE_GTT_WRITE_COMBINE:
			heap_usage = device->ws->query_value(device->ws,
							     RADEON_ALLOCATED_GTT);

			heap_budget = gtt_size -
				device->ws->query_value(device->ws, RADEON_GTT_USAGE) +
				heap_usage;

			memoryBudget->heapBudget[heap_index] = heap_budget;
			memoryBudget->heapUsage[heap_index] = heap_usage;
			break;
		default:
			break;
		}
	}

	/* The heapBudget and heapUsage values must be zero for array elements
	 * greater than or equal to
	 * VkPhysicalDeviceMemoryProperties::memoryHeapCount.
	 */
	for (uint32_t i = memory_properties->memoryHeapCount; i < VK_MAX_MEMORY_HEAPS; i++) {
		memoryBudget->heapBudget[i] = 0;
		memoryBudget->heapUsage[i] = 0;
	}
}

void radv_GetPhysicalDeviceMemoryProperties2(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceMemoryProperties2          *pMemoryProperties)
{
	radv_GetPhysicalDeviceMemoryProperties(physicalDevice,
					       &pMemoryProperties->memoryProperties);

	VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget =
		vk_find_struct(pMemoryProperties->pNext,
			       PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
	if (memory_budget)
		radv_get_memory_budget_properties(physicalDevice, memory_budget);
}

VkResult radv_GetMemoryHostPointerPropertiesEXT(
	VkDevice                                    _device,
	VkExternalMemoryHandleTypeFlagBits          handleType,
	const void                                 *pHostPointer,
	VkMemoryHostPointerPropertiesEXT           *pMemoryHostPointerProperties)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	switch (handleType)
	{
	case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT: {
		const struct radv_physical_device *physical_device = device->physical_device;
		uint32_t memoryTypeBits = 0;
		for (int i = 0; i < physical_device->memory_properties.memoryTypeCount; i++) {
			if (physical_device->mem_type_indices[i] == RADV_MEM_TYPE_GTT_CACHED) {
				memoryTypeBits = (1 << i);
				break;
			}
		}
		pMemoryHostPointerProperties->memoryTypeBits = memoryTypeBits;
		return VK_SUCCESS;
	}
	default:
		return VK_ERROR_INVALID_EXTERNAL_HANDLE;
	}
}

static enum radeon_ctx_priority
radv_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoEXT *pObj)
{
	/* Default to MEDIUM when a specific global priority isn't requested */
	if (!pObj)
		return RADEON_CTX_PRIORITY_MEDIUM;

	switch(pObj->globalPriority) {
	case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT:
		return RADEON_CTX_PRIORITY_REALTIME;
	case VK_QUEUE_GLOBAL_PRIORITY_HIGH_EXT:
		return RADEON_CTX_PRIORITY_HIGH;
	case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT:
		return RADEON_CTX_PRIORITY_MEDIUM;
	case VK_QUEUE_GLOBAL_PRIORITY_LOW_EXT:
		return RADEON_CTX_PRIORITY_LOW;
	default:
		unreachable("Illegal global priority value");
		return RADEON_CTX_PRIORITY_INVALID;
	}
}

static int
radv_queue_init(struct radv_device *device, struct radv_queue *queue,
		uint32_t queue_family_index, int idx,
		VkDeviceQueueCreateFlags flags,
		const VkDeviceQueueGlobalPriorityCreateInfoEXT *global_priority)
{
	queue->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	queue->device = device;
	queue->queue_family_index = queue_family_index;
	queue->queue_idx = idx;
	queue->priority = radv_get_queue_global_priority(global_priority);
	queue->flags = flags;

	queue->hw_ctx = device->ws->ctx_create(device->ws, queue->priority);
	if (!queue->hw_ctx)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	list_inithead(&queue->pending_submissions);
	pthread_mutex_init(&queue->pending_mutex, NULL);

	return VK_SUCCESS;
}

static void
radv_queue_finish(struct radv_queue *queue)
{
	pthread_mutex_destroy(&queue->pending_mutex);

	if (queue->hw_ctx)
		queue->device->ws->ctx_destroy(queue->hw_ctx);

	if (queue->initial_full_flush_preamble_cs)
		queue->device->ws->cs_destroy(queue->initial_full_flush_preamble_cs);
	if (queue->initial_preamble_cs)
		queue->device->ws->cs_destroy(queue->initial_preamble_cs);
	if (queue->continue_preamble_cs)
		queue->device->ws->cs_destroy(queue->continue_preamble_cs);
	if (queue->descriptor_bo)
		queue->device->ws->buffer_destroy(queue->descriptor_bo);
	if (queue->scratch_bo)
		queue->device->ws->buffer_destroy(queue->scratch_bo);
	if (queue->esgs_ring_bo)
		queue->device->ws->buffer_destroy(queue->esgs_ring_bo);
	if (queue->gsvs_ring_bo)
		queue->device->ws->buffer_destroy(queue->gsvs_ring_bo);
	if (queue->tess_rings_bo)
		queue->device->ws->buffer_destroy(queue->tess_rings_bo);
	if (queue->gds_bo)
		queue->device->ws->buffer_destroy(queue->gds_bo);
	if (queue->gds_oa_bo)
		queue->device->ws->buffer_destroy(queue->gds_oa_bo);
	if (queue->compute_scratch_bo)
		queue->device->ws->buffer_destroy(queue->compute_scratch_bo);
}

static void
radv_bo_list_init(struct radv_bo_list *bo_list)
{
	pthread_mutex_init(&bo_list->mutex, NULL);
	bo_list->list.count = bo_list->capacity = 0;
	bo_list->list.bos = NULL;
}

static void
radv_bo_list_finish(struct radv_bo_list *bo_list)
{
	free(bo_list->list.bos);
	pthread_mutex_destroy(&bo_list->mutex);
}

static VkResult radv_bo_list_add(struct radv_device *device,
				 struct radeon_winsys_bo *bo)
{
	struct radv_bo_list *bo_list = &device->bo_list;

	if (bo->is_local)
		return VK_SUCCESS;

	if (unlikely(!device->use_global_bo_list))
		return VK_SUCCESS;

	pthread_mutex_lock(&bo_list->mutex);
	if (bo_list->list.count == bo_list->capacity) {
		unsigned capacity = MAX2(4, bo_list->capacity * 2);
		void *data = realloc(bo_list->list.bos, capacity * sizeof(struct radeon_winsys_bo*));

		if (!data) {
			pthread_mutex_unlock(&bo_list->mutex);
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		bo_list->list.bos = (struct radeon_winsys_bo**)data;
		bo_list->capacity = capacity;
	}

	bo_list->list.bos[bo_list->list.count++] = bo;
	pthread_mutex_unlock(&bo_list->mutex);
	return VK_SUCCESS;
}

static void radv_bo_list_remove(struct radv_device *device,
				struct radeon_winsys_bo *bo)
{
	struct radv_bo_list *bo_list = &device->bo_list;

	if (bo->is_local)
		return;

	if (unlikely(!device->use_global_bo_list))
		return;

	pthread_mutex_lock(&bo_list->mutex);
	for(unsigned i = 0; i < bo_list->list.count; ++i) {
		if (bo_list->list.bos[i] == bo) {
			bo_list->list.bos[i] = bo_list->list.bos[bo_list->list.count - 1];
			--bo_list->list.count;
			break;
		}
	}
	pthread_mutex_unlock(&bo_list->mutex);
}

static void
radv_device_init_gs_info(struct radv_device *device)
{
	device->gs_table_depth = ac_get_gs_table_depth(device->physical_device->rad_info.chip_class,
						       device->physical_device->rad_info.family);
}

static int radv_get_device_extension_index(const char *name)
{
	for (unsigned i = 0; i < RADV_DEVICE_EXTENSION_COUNT; ++i) {
		if (strcmp(name, radv_device_extensions[i].extensionName) == 0)
			return i;
	}
	return -1;
}

static int
radv_get_int_debug_option(const char *name, int default_value)
{
	const char *str;
	int result;

	str = getenv(name);
	if (!str) {
		result = default_value;
	} else {
		char *endptr;

		result = strtol(str, &endptr, 0);
		if (str == endptr) {
			/* No digits founs. */
			result = default_value;
		}
	}

	return result;
}

static int install_seccomp_filter() {

	struct sock_filter filter[] = {
		/* Check arch is 64bit x86 */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, arch))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, AUDIT_ARCH_X86_64, 0, 12),

		/* Futex is required for mutex locks */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_select, 11, 0),

		/* Allow system exit calls for the forked process */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_exit_group, 9, 0),

		/* Allow system read calls */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_read, 7, 0),

		/* Allow system write calls */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_write, 5, 0),

		/* Allow system brk calls (we need this for malloc) */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_brk, 3, 0),

		/* Futex is required for mutex locks */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_futex, 1, 0),

		/* Return error if we hit a system call not on the whitelist */
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),

		/* Allow whitelisted system calls */
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
	};

	struct sock_fprog prog = {
		.len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
		.filter = filter,
	};

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		return -1;

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
		return -1;

	return 0;
}

/* Helper function with timeout support for reading from the pipe between
 * processes used for secure compile.
 */
bool radv_sc_read(int fd, void *buf, size_t size, bool timeout)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	while (true) {
		/* We can't rely on the value of tv after calling select() so
		 * we must reset it on each iteration of the loop.
		 */
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		int rval = select(fd + 1, &fds, NULL, NULL, timeout ? &tv : NULL);

		if (rval == -1) {
			/* select error */
			return false;
		} else if (rval) {
			ssize_t bytes_read = read(fd, buf, size);
			if (bytes_read < 0)
				return false;

			buf += bytes_read;
			size -= bytes_read;
			if (size == 0)
				return true;
		} else {
			/* select timeout */
			return false;
		}
	}
}

static bool radv_close_all_fds(const int *keep_fds, int keep_fd_count)
{
	DIR *d;
	struct dirent *dir;
	d = opendir("/proc/self/fd");
	if (!d)
		return false;
	int dir_fd = dirfd(d);

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_name[0] == '.')
			continue;

		int fd = atoi(dir->d_name);
		if (fd == dir_fd)
			continue;

		bool keep = false;
		for (int i = 0; !keep && i < keep_fd_count; ++i)
			if (keep_fds[i] == fd)
				keep = true;

		if (keep)
			continue;

		close(fd);
	}
	closedir(d);
	return true;
}

static void run_secure_compile_device(struct radv_device *device, unsigned process,
				      int fd_secure_input, int fd_secure_output)
{
	enum radv_secure_compile_type sc_type;

	const int needed_fds[] = {
		fd_secure_input,
		fd_secure_output,
	};
	if (!radv_close_all_fds(needed_fds, ARRAY_SIZE(needed_fds)) || install_seccomp_filter() == -1) {
		sc_type = RADV_SC_TYPE_INIT_FAILURE;
	} else {
		sc_type = RADV_SC_TYPE_INIT_SUCCESS;
		device->sc_state->secure_compile_processes[process].fd_secure_input = fd_secure_input;
		device->sc_state->secure_compile_processes[process].fd_secure_output = fd_secure_output;
	}

	write(fd_secure_output, &sc_type, sizeof(sc_type));

	if (sc_type == RADV_SC_TYPE_INIT_FAILURE)
		goto secure_compile_exit;

	while (true) {
		radv_sc_read(fd_secure_input, &sc_type, sizeof(sc_type), false);

		if (sc_type == RADV_SC_TYPE_COMPILE_PIPELINE) {
			struct radv_pipeline *pipeline;
			bool sc_read = true;

			pipeline = vk_zalloc2(&device->alloc, NULL, sizeof(*pipeline), 8,
					      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

			pipeline->device = device;

			/* Read pipeline layout */
			struct radv_pipeline_layout layout;
			sc_read = radv_sc_read(fd_secure_input, &layout, sizeof(struct radv_pipeline_layout), true);
			sc_read &= radv_sc_read(fd_secure_input, &layout.num_sets, sizeof(uint32_t), true);
			if (!sc_read)
				goto secure_compile_exit;

			for (uint32_t set = 0; set < layout.num_sets; set++) {
				uint32_t layout_size;
				sc_read &= radv_sc_read(fd_secure_input, &layout_size, sizeof(uint32_t), true);
				if (!sc_read)
					goto secure_compile_exit;

				layout.set[set].layout = malloc(layout_size);
				layout.set[set].layout->layout_size = layout_size;
				sc_read &= radv_sc_read(fd_secure_input, layout.set[set].layout,
							layout.set[set].layout->layout_size, true);
			}

			pipeline->layout = &layout;

			/* Read pipeline key */
			struct radv_pipeline_key key;
			sc_read &= radv_sc_read(fd_secure_input, &key, sizeof(struct radv_pipeline_key), true);

			/* Read pipeline create flags */
			VkPipelineCreateFlags flags;
			sc_read &= radv_sc_read(fd_secure_input, &flags, sizeof(VkPipelineCreateFlags), true);

			/* Read stage and shader information */
			uint32_t num_stages;
			const VkPipelineShaderStageCreateInfo *pStages[MESA_SHADER_STAGES] = { 0, };
			sc_read &= radv_sc_read(fd_secure_input, &num_stages, sizeof(uint32_t), true);
			if (!sc_read)
				goto secure_compile_exit;

			for (uint32_t i = 0; i < num_stages; i++) {

				/* Read stage */
				gl_shader_stage stage;
				sc_read &= radv_sc_read(fd_secure_input, &stage, sizeof(gl_shader_stage), true);

				VkPipelineShaderStageCreateInfo *pStage = calloc(1, sizeof(VkPipelineShaderStageCreateInfo));

				/* Read entry point name */
				size_t name_size;
				sc_read &= radv_sc_read(fd_secure_input, &name_size, sizeof(size_t), true);
				if (!sc_read)
					goto secure_compile_exit;

				char *ep_name = malloc(name_size);
				sc_read &= radv_sc_read(fd_secure_input, ep_name, name_size, true);
				pStage->pName = ep_name;

				/* Read shader module */
				size_t module_size;
				sc_read &= radv_sc_read(fd_secure_input, &module_size, sizeof(size_t), true);
				if (!sc_read)
					goto secure_compile_exit;

				struct radv_shader_module *module = malloc(module_size);
				sc_read &= radv_sc_read(fd_secure_input, module, module_size, true);
				pStage->module = radv_shader_module_to_handle(module);

				/* Read specialization info */
				bool has_spec_info;
				sc_read &= radv_sc_read(fd_secure_input, &has_spec_info, sizeof(bool), true);
				if (!sc_read)
					goto secure_compile_exit;

				if (has_spec_info) {
					VkSpecializationInfo *specInfo = malloc(sizeof(VkSpecializationInfo));
					pStage->pSpecializationInfo = specInfo;

					sc_read &= radv_sc_read(fd_secure_input, &specInfo->dataSize, sizeof(size_t), true);
					if (!sc_read)
						goto secure_compile_exit;

					void *si_data = malloc(specInfo->dataSize);
					sc_read &= radv_sc_read(fd_secure_input, si_data, specInfo->dataSize, true);
					specInfo->pData = si_data;

					sc_read &= radv_sc_read(fd_secure_input, &specInfo->mapEntryCount, sizeof(uint32_t), true);
					if (!sc_read)
						goto secure_compile_exit;

					VkSpecializationMapEntry *mapEntries = malloc(sizeof(VkSpecializationMapEntry) * specInfo->mapEntryCount);
					for (uint32_t j = 0; j < specInfo->mapEntryCount; j++) {
						sc_read &= radv_sc_read(fd_secure_input, &mapEntries[j], sizeof(VkSpecializationMapEntry), true);
						if (!sc_read)
							goto secure_compile_exit;
					}

					specInfo->pMapEntries = mapEntries;
				}

				pStages[stage] = pStage;
			}

			/* Compile the shaders */
			VkPipelineCreationFeedbackEXT *stage_feedbacks[MESA_SHADER_STAGES] = { 0 };
			radv_create_shaders(pipeline, device, NULL, &key, pStages, flags, NULL, stage_feedbacks);

			/* free memory allocated above */
			for (uint32_t set = 0; set < layout.num_sets; set++)
				free(layout.set[set].layout);

			for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
				if (!pStages[i])
					continue;

				free((void *) pStages[i]->pName);
				free(radv_shader_module_from_handle(pStages[i]->module));
				if (pStages[i]->pSpecializationInfo) {
					free((void *) pStages[i]->pSpecializationInfo->pData);
					free((void *) pStages[i]->pSpecializationInfo->pMapEntries);
					free((void *) pStages[i]->pSpecializationInfo);
				}
				free((void *) pStages[i]);
			}

			vk_free(&device->alloc, pipeline);

			sc_type = RADV_SC_TYPE_COMPILE_PIPELINE_FINISHED;
			write(fd_secure_output, &sc_type, sizeof(sc_type));

		} else if (sc_type == RADV_SC_TYPE_DESTROY_DEVICE) {
			goto secure_compile_exit;
		}
	}

secure_compile_exit:
	close(fd_secure_input);
	close(fd_secure_output);
	_exit(0);
}

static void destroy_secure_compile_device(struct radv_device *device, unsigned process)
{
	int fd_secure_input = device->sc_state->secure_compile_processes[process].fd_secure_input;

	enum radv_secure_compile_type sc_type = RADV_SC_TYPE_DESTROY_DEVICE;
	write(fd_secure_input, &sc_type, sizeof(sc_type));

	close(device->sc_state->secure_compile_processes[process].fd_secure_input);
	close(device->sc_state->secure_compile_processes[process].fd_secure_output);

	int status;
	waitpid(device->sc_state->secure_compile_processes[process].sc_pid, &status, 0);
}

static VkResult fork_secure_compile_device(struct radv_device *device)
{
	device->sc_state = vk_zalloc(&device->alloc,
				     sizeof(struct radv_secure_compile_state),
				     8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

	mtx_init(&device->sc_state->secure_compile_mutex, mtx_plain);

	uint8_t sc_threads = device->instance->num_sc_threads;
	int fd_secure_input[MAX_SC_PROCS][2];
	int fd_secure_output[MAX_SC_PROCS][2];

	/* create pipe descriptors (used to communicate between processes) */
	for (unsigned i = 0; i < sc_threads; i++) {
		if (pipe(fd_secure_input[i]) == -1 ||
		    pipe(fd_secure_output[i]) == -1) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}
	}

	device->sc_state->secure_compile_processes = vk_zalloc(&device->alloc,
								sizeof(struct radv_secure_compile_process) * sc_threads, 8,
								VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

	for (unsigned process = 0; process < sc_threads; process++) {
		if ((device->sc_state->secure_compile_processes[process].sc_pid = fork()) == 0) {
			device->sc_state->secure_compile_thread_counter = process;
			run_secure_compile_device(device, process, fd_secure_input[process][0], fd_secure_output[process][1]);
		} else {
			if (device->sc_state->secure_compile_processes[process].sc_pid == -1)
				return VK_ERROR_INITIALIZATION_FAILED;

			/* Read the init result returned from the secure process */
			enum radv_secure_compile_type sc_type;
			bool sc_read = radv_sc_read(fd_secure_output[process][0], &sc_type, sizeof(sc_type), true);

			if (sc_type == RADV_SC_TYPE_INIT_FAILURE || !sc_read) {
				close(fd_secure_input[process][0]);
				close(fd_secure_input[process][1]);
				close(fd_secure_output[process][1]);
				close(fd_secure_output[process][0]);
				int status;
				waitpid(device->sc_state->secure_compile_processes[process].sc_pid, &status, 0);

				/* Destroy any forks that were created sucessfully */
				for (unsigned i = 0; i < process; i++) {
					destroy_secure_compile_device(device, i);
				}

				return VK_ERROR_INITIALIZATION_FAILED;
			} else {
				assert(sc_type == RADV_SC_TYPE_INIT_SUCCESS);
				device->sc_state->secure_compile_processes[process].fd_secure_input = fd_secure_input[process][1];
				device->sc_state->secure_compile_processes[process].fd_secure_output = fd_secure_output[process][0];
			}
		}
	}
	return VK_SUCCESS;
}

static VkResult
radv_create_pthread_cond(pthread_cond_t *cond)
{
	pthread_condattr_t condattr;
	if (pthread_condattr_init(&condattr)) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC)) {
		pthread_condattr_destroy(&condattr);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (pthread_cond_init(cond, &condattr)) {
		pthread_condattr_destroy(&condattr);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	pthread_condattr_destroy(&condattr);
	return VK_SUCCESS;
}

VkResult radv_CreateDevice(
	VkPhysicalDevice                            physicalDevice,
	const VkDeviceCreateInfo*                   pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDevice*                                   pDevice)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
	VkResult result;
	struct radv_device *device;

	bool keep_shader_info = false;

	/* Check enabled features */
	if (pCreateInfo->pEnabledFeatures) {
		VkPhysicalDeviceFeatures supported_features;
		radv_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
		VkBool32 *supported_feature = (VkBool32 *)&supported_features;
		VkBool32 *enabled_feature = (VkBool32 *)pCreateInfo->pEnabledFeatures;
		unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
		for (uint32_t i = 0; i < num_features; i++) {
			if (enabled_feature[i] && !supported_feature[i])
				return vk_error(physical_device->instance, VK_ERROR_FEATURE_NOT_PRESENT);
		}
	}

	device = vk_zalloc2(&physical_device->instance->alloc, pAllocator,
			    sizeof(*device), 8,
			    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
	if (!device)
		return vk_error(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	device->instance = physical_device->instance;
	device->physical_device = physical_device;

	device->ws = physical_device->ws;
	if (pAllocator)
		device->alloc = *pAllocator;
	else
		device->alloc = physical_device->instance->alloc;

	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		const char *ext_name = pCreateInfo->ppEnabledExtensionNames[i];
		int index = radv_get_device_extension_index(ext_name);
		if (index < 0 || !physical_device->supported_extensions.extensions[index]) {
			vk_free(&device->alloc, device);
			return vk_error(physical_device->instance, VK_ERROR_EXTENSION_NOT_PRESENT);
		}

		device->enabled_extensions.extensions[index] = true;
	}

	keep_shader_info = device->enabled_extensions.AMD_shader_info;

	/* With update after bind we can't attach bo's to the command buffer
	 * from the descriptor set anymore, so we have to use a global BO list.
	 */
	device->use_global_bo_list =
		(device->instance->perftest_flags & RADV_PERFTEST_BO_LIST) ||
		device->enabled_extensions.EXT_descriptor_indexing ||
		device->enabled_extensions.EXT_buffer_device_address;

	device->robust_buffer_access = pCreateInfo->pEnabledFeatures &&
	                               pCreateInfo->pEnabledFeatures->robustBufferAccess;

	mtx_init(&device->shader_slab_mutex, mtx_plain);
	list_inithead(&device->shader_slabs);

	radv_bo_list_init(&device->bo_list);

	for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
		const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
		uint32_t qfi = queue_create->queueFamilyIndex;
		const VkDeviceQueueGlobalPriorityCreateInfoEXT *global_priority =
			vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT);

		assert(!global_priority || device->physical_device->rad_info.has_ctx_priority);

		device->queues[qfi] = vk_alloc(&device->alloc,
					       queue_create->queueCount * sizeof(struct radv_queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!device->queues[qfi]) {
			result = VK_ERROR_OUT_OF_HOST_MEMORY;
			goto fail;
		}

		memset(device->queues[qfi], 0, queue_create->queueCount * sizeof(struct radv_queue));

		device->queue_count[qfi] = queue_create->queueCount;

		for (unsigned q = 0; q < queue_create->queueCount; q++) {
			result = radv_queue_init(device, &device->queues[qfi][q],
						 qfi, q, queue_create->flags,
						 global_priority);
			if (result != VK_SUCCESS)
				goto fail;
		}
	}

	device->pbb_allowed = device->physical_device->rad_info.chip_class >= GFX9 &&
			      !(device->instance->debug_flags & RADV_DEBUG_NOBINNING);

	/* Disable DFSM by default. As of 2019-09-15 Talos on Low is still 3% slower on Raven. */
	device->dfsm_allowed = device->pbb_allowed &&
	                       (device->instance->perftest_flags & RADV_PERFTEST_DFSM);

	device->always_use_syncobj = device->physical_device->rad_info.has_syncobj_wait_for_submit;

	/* The maximum number of scratch waves. Scratch space isn't divided
	 * evenly between CUs. The number is only a function of the number of CUs.
	 * We can decrease the constant to decrease the scratch buffer size.
	 *
	 * sctx->scratch_waves must be >= the maximum possible size of
	 * 1 threadgroup, so that the hw doesn't hang from being unable
	 * to start any.
	 *
	 * The recommended value is 4 per CU at most. Higher numbers don't
	 * bring much benefit, but they still occupy chip resources (think
	 * async compute). I've seen ~2% performance difference between 4 and 32.
	 */
	uint32_t max_threads_per_block = 2048;
	device->scratch_waves = MAX2(32 * physical_device->rad_info.num_good_compute_units,
				     max_threads_per_block / 64);

	device->dispatch_initiator = S_00B800_COMPUTE_SHADER_EN(1);

	if (device->physical_device->rad_info.chip_class >= GFX7) {
		/* If the KMD allows it (there is a KMD hw register for it),
		 * allow launching waves out-of-order.
		 */
		device->dispatch_initiator |= S_00B800_ORDER_MODE(1);
	}

	radv_device_init_gs_info(device);

	device->tess_offchip_block_dw_size =
		device->physical_device->rad_info.family == CHIP_HAWAII ? 4096 : 8192;

	if (getenv("RADV_TRACE_FILE")) {
		const char *filename = getenv("RADV_TRACE_FILE");

		keep_shader_info = true;

		if (!radv_init_trace(device))
			goto fail;

		fprintf(stderr, "*****************************************************************************\n");
		fprintf(stderr, "* WARNING: RADV_TRACE_FILE is costly and should only be used for debugging! *\n");
		fprintf(stderr, "*****************************************************************************\n");

		fprintf(stderr, "Trace file will be dumped to %s\n", filename);
		radv_dump_enabled_options(device, stderr);
	}

	/* Temporarily disable secure compile while we create meta shaders, etc */
	uint8_t sc_threads = device->instance->num_sc_threads;
	if (sc_threads)
		device->instance->num_sc_threads = 0;

	device->keep_shader_info = keep_shader_info;
	result = radv_device_init_meta(device);
	if (result != VK_SUCCESS)
		goto fail;

	radv_device_init_msaa(device);

	for (int family = 0; family < RADV_MAX_QUEUE_FAMILIES; ++family) {
		device->empty_cs[family] = device->ws->cs_create(device->ws, family);
		switch (family) {
		case RADV_QUEUE_GENERAL:
			radeon_emit(device->empty_cs[family], PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
			radeon_emit(device->empty_cs[family], CONTEXT_CONTROL_LOAD_ENABLE(1));
			radeon_emit(device->empty_cs[family], CONTEXT_CONTROL_SHADOW_ENABLE(1));
			break;
		case RADV_QUEUE_COMPUTE:
			radeon_emit(device->empty_cs[family], PKT3(PKT3_NOP, 0, 0));
			radeon_emit(device->empty_cs[family], 0);
			break;
		}
		device->ws->cs_finalize(device->empty_cs[family]);
	}

	if (device->physical_device->rad_info.chip_class >= GFX7)
		cik_create_gfx_config(device);

	VkPipelineCacheCreateInfo ci;
	ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	ci.pNext = NULL;
	ci.flags = 0;
	ci.pInitialData = NULL;
	ci.initialDataSize = 0;
	VkPipelineCache pc;
	result = radv_CreatePipelineCache(radv_device_to_handle(device),
					  &ci, NULL, &pc);
	if (result != VK_SUCCESS)
		goto fail_meta;

	device->mem_cache = radv_pipeline_cache_from_handle(pc);

	result = radv_create_pthread_cond(&device->timeline_cond);
	if (result != VK_SUCCESS)
		goto fail_mem_cache;

	device->force_aniso =
		MIN2(16, radv_get_int_debug_option("RADV_TEX_ANISO", -1));
	if (device->force_aniso >= 0) {
		fprintf(stderr, "radv: Forcing anisotropy filter to %ix\n",
			1 << util_logbase2(device->force_aniso));
	}

	/* Fork device for secure compile as required */
	device->instance->num_sc_threads = sc_threads;
	if (radv_device_use_secure_compile(device->instance)) {
		result = fork_secure_compile_device(device);
		if (result != VK_SUCCESS)
			goto fail_meta;
	}

	*pDevice = radv_device_to_handle(device);
	return VK_SUCCESS;

fail_mem_cache:
	radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);
fail_meta:
	radv_device_finish_meta(device);
fail:
	radv_bo_list_finish(&device->bo_list);

	if (device->trace_bo)
		device->ws->buffer_destroy(device->trace_bo);

	if (device->gfx_init)
		device->ws->buffer_destroy(device->gfx_init);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++)
			radv_queue_finish(&device->queues[i][q]);
		if (device->queue_count[i])
			vk_free(&device->alloc, device->queues[i]);
	}

	vk_free(&device->alloc, device);
	return result;
}

void radv_DestroyDevice(
	VkDevice                                    _device,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	if (!device)
		return;

	if (device->trace_bo)
		device->ws->buffer_destroy(device->trace_bo);

	if (device->gfx_init)
		device->ws->buffer_destroy(device->gfx_init);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++)
			radv_queue_finish(&device->queues[i][q]);
		if (device->queue_count[i])
			vk_free(&device->alloc, device->queues[i]);
		if (device->empty_cs[i])
			device->ws->cs_destroy(device->empty_cs[i]);
	}
	radv_device_finish_meta(device);

	VkPipelineCache pc = radv_pipeline_cache_to_handle(device->mem_cache);
	radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);

	radv_destroy_shader_slabs(device);

	pthread_cond_destroy(&device->timeline_cond);
	radv_bo_list_finish(&device->bo_list);

	if (radv_device_use_secure_compile(device->instance)) {
		for (unsigned i = 0; i < device->instance->num_sc_threads; i++ ) {
			destroy_secure_compile_device(device, i);
		}
	}

	if (device->sc_state)
		vk_free(&device->alloc, device->sc_state->secure_compile_processes);
	vk_free(&device->alloc, device->sc_state);
	vk_free(&device->alloc, device);
}

VkResult radv_EnumerateInstanceLayerProperties(
	uint32_t*                                   pPropertyCount,
	VkLayerProperties*                          pProperties)
{
	if (pProperties == NULL) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	/* None supported at this time */
	return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult radv_EnumerateDeviceLayerProperties(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pPropertyCount,
	VkLayerProperties*                          pProperties)
{
	if (pProperties == NULL) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	/* None supported at this time */
	return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

void radv_GetDeviceQueue2(
	VkDevice                                    _device,
	const VkDeviceQueueInfo2*                   pQueueInfo,
	VkQueue*                                    pQueue)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_queue *queue;

	queue = &device->queues[pQueueInfo->queueFamilyIndex][pQueueInfo->queueIndex];
	if (pQueueInfo->flags != queue->flags) {
		/* From the Vulkan 1.1.70 spec:
		 *
		 * "The queue returned by vkGetDeviceQueue2 must have the same
		 * flags value from this structure as that used at device
		 * creation time in a VkDeviceQueueCreateInfo instance. If no
		 * matching flags were specified at device creation time then
		 * pQueue will return VK_NULL_HANDLE."
		 */
		*pQueue = VK_NULL_HANDLE;
		return;
	}

	*pQueue = radv_queue_to_handle(queue);
}

void radv_GetDeviceQueue(
	VkDevice                                    _device,
	uint32_t                                    queueFamilyIndex,
	uint32_t                                    queueIndex,
	VkQueue*                                    pQueue)
{
	const VkDeviceQueueInfo2 info = (VkDeviceQueueInfo2) {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
		.queueFamilyIndex = queueFamilyIndex,
		.queueIndex = queueIndex
	};

	radv_GetDeviceQueue2(_device, &info, pQueue);
}

static void
fill_geom_tess_rings(struct radv_queue *queue,
		     uint32_t *map,
		     bool add_sample_positions,
		     uint32_t esgs_ring_size,
		     struct radeon_winsys_bo *esgs_ring_bo,
		     uint32_t gsvs_ring_size,
		     struct radeon_winsys_bo *gsvs_ring_bo,
		     uint32_t tess_factor_ring_size,
		     uint32_t tess_offchip_ring_offset,
		     uint32_t tess_offchip_ring_size,
		     struct radeon_winsys_bo *tess_rings_bo)
{
	uint32_t *desc = &map[4];

	if (esgs_ring_bo) {
		uint64_t esgs_va = radv_buffer_get_va(esgs_ring_bo);

		/* stride 0, num records - size, add tid, swizzle, elsize4,
		   index stride 64 */
		desc[0] = esgs_va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32) |
			  S_008F04_SWIZZLE_ENABLE(true);
		desc[2] = esgs_ring_size;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			  S_008F0C_INDEX_STRIDE(3) |
			  S_008F0C_ADD_TID_ENABLE(1);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[3] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(2) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
				   S_008F0C_ELEMENT_SIZE(1);
		}

		/* GS entry for ES->GS ring */
		/* stride 0, num records - size, elsize0,
		   index stride 0 */
		desc[4] = esgs_va;
		desc[5] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32);
		desc[6] = esgs_ring_size;
		desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[7] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(2) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
		}
	}

	desc += 8;

	if (gsvs_ring_bo) {
		uint64_t gsvs_va = radv_buffer_get_va(gsvs_ring_bo);

		/* VS entry for GS->VS ring */
		/* stride 0, num records - size, elsize0,
		   index stride 0 */
		desc[0] = gsvs_va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32);
		desc[2] = gsvs_ring_size;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[3] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(2) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
		}

		/* stride gsvs_itemsize, num records 64
		   elsize 4, index stride 16 */
		/* shader will patch stride and desc[2] */
		desc[4] = gsvs_va;
		desc[5] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32) |
			  S_008F04_SWIZZLE_ENABLE(1);
		desc[6] = 0;
		desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			  S_008F0C_INDEX_STRIDE(1) |
			  S_008F0C_ADD_TID_ENABLE(true);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[7] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(2) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
				   S_008F0C_ELEMENT_SIZE(1);
		}

	}

	desc += 8;

	if (tess_rings_bo) {
		uint64_t tess_va = radv_buffer_get_va(tess_rings_bo);
		uint64_t tess_offchip_va = tess_va + tess_offchip_ring_offset;

		desc[0] = tess_va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(tess_va >> 32);
		desc[2] = tess_factor_ring_size;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[3] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(3) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
		}

		desc[4] = tess_offchip_va;
		desc[5] = S_008F04_BASE_ADDRESS_HI(tess_offchip_va >> 32);
		desc[6] = tess_offchip_ring_size;
		desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			desc[7] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
				   S_008F0C_OOB_SELECT(3) |
				   S_008F0C_RESOURCE_LEVEL(1);
		} else {
			desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
				   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
		}
	}

	desc += 8;

	if (add_sample_positions) {
		/* add sample positions after all rings */
		memcpy(desc, queue->device->sample_locations_1x, 8);
		desc += 2;
		memcpy(desc, queue->device->sample_locations_2x, 16);
		desc += 4;
		memcpy(desc, queue->device->sample_locations_4x, 32);
		desc += 8;
		memcpy(desc, queue->device->sample_locations_8x, 64);
	}
}

static unsigned
radv_get_hs_offchip_param(struct radv_device *device, uint32_t *max_offchip_buffers_p)
{
	bool double_offchip_buffers = device->physical_device->rad_info.chip_class >= GFX7 &&
		device->physical_device->rad_info.family != CHIP_CARRIZO &&
		device->physical_device->rad_info.family != CHIP_STONEY;
	unsigned max_offchip_buffers_per_se = double_offchip_buffers ? 128 : 64;
	unsigned max_offchip_buffers;
	unsigned offchip_granularity;
	unsigned hs_offchip_param;

	/*
	 * Per RadeonSI:
	 * This must be one less than the maximum number due to a hw limitation.
         * Various hardware bugs need thGFX7
	 *
	 * Per AMDVLK:
	 * Vega10 should limit max_offchip_buffers to 508 (4 * 127).
	 * Gfx7 should limit max_offchip_buffers to 508
	 * Gfx6 should limit max_offchip_buffers to 126 (2 * 63)
	 *
	 * Follow AMDVLK here.
	 */
	if (device->physical_device->rad_info.chip_class >= GFX10) {
		max_offchip_buffers_per_se = 256;
	} else if (device->physical_device->rad_info.family == CHIP_VEGA10 ||
		   device->physical_device->rad_info.chip_class == GFX7 ||
		   device->physical_device->rad_info.chip_class == GFX6)
		--max_offchip_buffers_per_se;

	max_offchip_buffers = max_offchip_buffers_per_se *
		device->physical_device->rad_info.max_se;

	/* Hawaii has a bug with offchip buffers > 256 that can be worked
	 * around by setting 4K granularity.
	 */
	if (device->tess_offchip_block_dw_size == 4096) {
		assert(device->physical_device->rad_info.family == CHIP_HAWAII);
		offchip_granularity = V_03093C_X_4K_DWORDS;
	} else {
		assert(device->tess_offchip_block_dw_size == 8192);
		offchip_granularity = V_03093C_X_8K_DWORDS;
	}

	switch (device->physical_device->rad_info.chip_class) {
	case GFX6:
		max_offchip_buffers = MIN2(max_offchip_buffers, 126);
		break;
	case GFX7:
	case GFX8:
	case GFX9:
		max_offchip_buffers = MIN2(max_offchip_buffers, 508);
		break;
	case GFX10:
		break;
	default:
		break;
	}

	*max_offchip_buffers_p = max_offchip_buffers;
	if (device->physical_device->rad_info.chip_class >= GFX7) {
		if (device->physical_device->rad_info.chip_class >= GFX8)
			--max_offchip_buffers;
		hs_offchip_param =
			S_03093C_OFFCHIP_BUFFERING(max_offchip_buffers) |
			S_03093C_OFFCHIP_GRANULARITY(offchip_granularity);
	} else {
		hs_offchip_param =
			S_0089B0_OFFCHIP_BUFFERING(max_offchip_buffers);
	}
	return hs_offchip_param;
}

static void
radv_emit_gs_ring_sizes(struct radv_queue *queue, struct radeon_cmdbuf *cs,
			struct radeon_winsys_bo *esgs_ring_bo,
			uint32_t esgs_ring_size,
			struct radeon_winsys_bo *gsvs_ring_bo,
			uint32_t gsvs_ring_size)
{
	if (!esgs_ring_bo && !gsvs_ring_bo)
		return;

	if (esgs_ring_bo)
		radv_cs_add_buffer(queue->device->ws, cs, esgs_ring_bo);

	if (gsvs_ring_bo)
		radv_cs_add_buffer(queue->device->ws, cs, gsvs_ring_bo);

	if (queue->device->physical_device->rad_info.chip_class >= GFX7) {
		radeon_set_uconfig_reg_seq(cs, R_030900_VGT_ESGS_RING_SIZE, 2);
		radeon_emit(cs, esgs_ring_size >> 8);
		radeon_emit(cs, gsvs_ring_size >> 8);
	} else {
		radeon_set_config_reg_seq(cs, R_0088C8_VGT_ESGS_RING_SIZE, 2);
		radeon_emit(cs, esgs_ring_size >> 8);
		radeon_emit(cs, gsvs_ring_size >> 8);
	}
}

static void
radv_emit_tess_factor_ring(struct radv_queue *queue, struct radeon_cmdbuf *cs,
			   unsigned hs_offchip_param, unsigned tf_ring_size,
			   struct radeon_winsys_bo *tess_rings_bo)
{
	uint64_t tf_va;

	if (!tess_rings_bo)
		return;

	tf_va = radv_buffer_get_va(tess_rings_bo);

	radv_cs_add_buffer(queue->device->ws, cs, tess_rings_bo);

	if (queue->device->physical_device->rad_info.chip_class >= GFX7) {
		radeon_set_uconfig_reg(cs, R_030938_VGT_TF_RING_SIZE,
				       S_030938_SIZE(tf_ring_size / 4));
		radeon_set_uconfig_reg(cs, R_030940_VGT_TF_MEMORY_BASE,
				       tf_va >> 8);

		if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
			radeon_set_uconfig_reg(cs, R_030984_VGT_TF_MEMORY_BASE_HI_UMD,
					       S_030984_BASE_HI(tf_va >> 40));
		} else if (queue->device->physical_device->rad_info.chip_class == GFX9) {
			radeon_set_uconfig_reg(cs, R_030944_VGT_TF_MEMORY_BASE_HI,
					       S_030944_BASE_HI(tf_va >> 40));
		}
		radeon_set_uconfig_reg(cs, R_03093C_VGT_HS_OFFCHIP_PARAM,
				       hs_offchip_param);
	} else {
		radeon_set_config_reg(cs, R_008988_VGT_TF_RING_SIZE,
				      S_008988_SIZE(tf_ring_size / 4));
		radeon_set_config_reg(cs, R_0089B8_VGT_TF_MEMORY_BASE,
				      tf_va >> 8);
		radeon_set_config_reg(cs, R_0089B0_VGT_HS_OFFCHIP_PARAM,
				     hs_offchip_param);
	}
}

static void
radv_emit_compute_scratch(struct radv_queue *queue, struct radeon_cmdbuf *cs,
			  struct radeon_winsys_bo *compute_scratch_bo)
{
	uint64_t scratch_va;

	if (!compute_scratch_bo)
		return;

	scratch_va = radv_buffer_get_va(compute_scratch_bo);

	radv_cs_add_buffer(queue->device->ws, cs, compute_scratch_bo);

	radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0, 2);
	radeon_emit(cs, scratch_va);
	radeon_emit(cs, S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) |
			S_008F04_SWIZZLE_ENABLE(1));
}

static void
radv_emit_global_shader_pointers(struct radv_queue *queue,
				 struct radeon_cmdbuf *cs,
				 struct radeon_winsys_bo *descriptor_bo)
{
	uint64_t va;

	if (!descriptor_bo)
		return;

	va = radv_buffer_get_va(descriptor_bo);

	radv_cs_add_buffer(queue->device->ws, cs, descriptor_bo);

	if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
		uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0,
				   R_00B130_SPI_SHADER_USER_DATA_VS_0,
				   R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
				   R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

		for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
			radv_emit_shader_pointer(queue->device, cs, regs[i],
						 va, true);
		}
	} else if (queue->device->physical_device->rad_info.chip_class == GFX9) {
		uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0,
				   R_00B130_SPI_SHADER_USER_DATA_VS_0,
				   R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
				   R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

		for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
			radv_emit_shader_pointer(queue->device, cs, regs[i],
						 va, true);
		}
	} else {
		uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0,
				   R_00B130_SPI_SHADER_USER_DATA_VS_0,
				   R_00B230_SPI_SHADER_USER_DATA_GS_0,
				   R_00B330_SPI_SHADER_USER_DATA_ES_0,
				   R_00B430_SPI_SHADER_USER_DATA_HS_0,
				   R_00B530_SPI_SHADER_USER_DATA_LS_0};

		for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
			radv_emit_shader_pointer(queue->device, cs, regs[i],
						 va, true);
		}
	}
}

static void
radv_init_graphics_state(struct radeon_cmdbuf *cs, struct radv_queue *queue)
{
	struct radv_device *device = queue->device;

	if (device->gfx_init) {
		uint64_t va = radv_buffer_get_va(device->gfx_init);

		radeon_emit(cs, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, device->gfx_init_size_dw & 0xffff);

		radv_cs_add_buffer(device->ws, cs, device->gfx_init);
	} else {
		struct radv_physical_device *physical_device = device->physical_device;
		si_emit_graphics(physical_device, cs);
	}
}

static void
radv_init_compute_state(struct radeon_cmdbuf *cs, struct radv_queue *queue)
{
	struct radv_physical_device *physical_device = queue->device->physical_device;
	si_emit_compute(physical_device, cs);
}

static VkResult
radv_get_preamble_cs(struct radv_queue *queue,
                     uint32_t scratch_size,
                     uint32_t compute_scratch_size,
		     uint32_t esgs_ring_size,
		     uint32_t gsvs_ring_size,
		     bool needs_tess_rings,
		     bool needs_gds,
		     bool needs_sample_positions,
		     struct radeon_cmdbuf **initial_full_flush_preamble_cs,
                     struct radeon_cmdbuf **initial_preamble_cs,
                     struct radeon_cmdbuf **continue_preamble_cs)
{
	struct radeon_winsys_bo *scratch_bo = NULL;
	struct radeon_winsys_bo *descriptor_bo = NULL;
	struct radeon_winsys_bo *compute_scratch_bo = NULL;
	struct radeon_winsys_bo *esgs_ring_bo = NULL;
	struct radeon_winsys_bo *gsvs_ring_bo = NULL;
	struct radeon_winsys_bo *tess_rings_bo = NULL;
	struct radeon_winsys_bo *gds_bo = NULL;
	struct radeon_winsys_bo *gds_oa_bo = NULL;
	struct radeon_cmdbuf *dest_cs[3] = {0};
	bool add_tess_rings = false, add_gds = false, add_sample_positions = false;
	unsigned tess_factor_ring_size = 0, tess_offchip_ring_size = 0;
	unsigned max_offchip_buffers;
	unsigned hs_offchip_param = 0;
	unsigned tess_offchip_ring_offset;
	uint32_t ring_bo_flags = RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING;
	if (!queue->has_tess_rings) {
		if (needs_tess_rings)
			add_tess_rings = true;
	}
	if (!queue->has_gds) {
		if (needs_gds)
			add_gds = true;
	}
	if (!queue->has_sample_positions) {
		if (needs_sample_positions)
			add_sample_positions = true;
	}
	tess_factor_ring_size = 32768 * queue->device->physical_device->rad_info.max_se;
	hs_offchip_param = radv_get_hs_offchip_param(queue->device,
						     &max_offchip_buffers);
	tess_offchip_ring_offset = align(tess_factor_ring_size, 64 * 1024);
	tess_offchip_ring_size = max_offchip_buffers *
		queue->device->tess_offchip_block_dw_size * 4;

	if (scratch_size <= queue->scratch_size &&
	    compute_scratch_size <= queue->compute_scratch_size &&
	    esgs_ring_size <= queue->esgs_ring_size &&
	    gsvs_ring_size <= queue->gsvs_ring_size &&
	    !add_tess_rings && !add_gds && !add_sample_positions &&
	    queue->initial_preamble_cs) {
		*initial_full_flush_preamble_cs = queue->initial_full_flush_preamble_cs;
		*initial_preamble_cs = queue->initial_preamble_cs;
		*continue_preamble_cs = queue->continue_preamble_cs;
		if (!scratch_size && !compute_scratch_size && !esgs_ring_size && !gsvs_ring_size &&
		    !needs_tess_rings && !needs_gds && !needs_sample_positions)
			*continue_preamble_cs = NULL;
		return VK_SUCCESS;
	}

	if (scratch_size > queue->scratch_size) {
		scratch_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                              scratch_size,
		                                              4096,
		                                              RADEON_DOMAIN_VRAM,
		                                              ring_bo_flags,
		                                              RADV_BO_PRIORITY_SCRATCH);
		if (!scratch_bo)
			goto fail;
	} else
		scratch_bo = queue->scratch_bo;

	if (compute_scratch_size > queue->compute_scratch_size) {
		compute_scratch_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                                      compute_scratch_size,
		                                                      4096,
		                                                      RADEON_DOMAIN_VRAM,
		                                                      ring_bo_flags,
		                                                      RADV_BO_PRIORITY_SCRATCH);
		if (!compute_scratch_bo)
			goto fail;

	} else
		compute_scratch_bo = queue->compute_scratch_bo;

	if (esgs_ring_size > queue->esgs_ring_size) {
		esgs_ring_bo = queue->device->ws->buffer_create(queue->device->ws,
								esgs_ring_size,
								4096,
								RADEON_DOMAIN_VRAM,
								ring_bo_flags,
								RADV_BO_PRIORITY_SCRATCH);
		if (!esgs_ring_bo)
			goto fail;
	} else {
		esgs_ring_bo = queue->esgs_ring_bo;
		esgs_ring_size = queue->esgs_ring_size;
	}

	if (gsvs_ring_size > queue->gsvs_ring_size) {
		gsvs_ring_bo = queue->device->ws->buffer_create(queue->device->ws,
								gsvs_ring_size,
								4096,
								RADEON_DOMAIN_VRAM,
								ring_bo_flags,
								RADV_BO_PRIORITY_SCRATCH);
		if (!gsvs_ring_bo)
			goto fail;
	} else {
		gsvs_ring_bo = queue->gsvs_ring_bo;
		gsvs_ring_size = queue->gsvs_ring_size;
	}

	if (add_tess_rings) {
		tess_rings_bo = queue->device->ws->buffer_create(queue->device->ws,
								 tess_offchip_ring_offset + tess_offchip_ring_size,
								 256,
								 RADEON_DOMAIN_VRAM,
								 ring_bo_flags,
								 RADV_BO_PRIORITY_SCRATCH);
		if (!tess_rings_bo)
			goto fail;
	} else {
		tess_rings_bo = queue->tess_rings_bo;
	}

	if (add_gds) {
		assert(queue->device->physical_device->rad_info.chip_class >= GFX10);

		/* 4 streamout GDS counters.
		 * We need 256B (64 dw) of GDS, otherwise streamout hangs.
		 */
		gds_bo = queue->device->ws->buffer_create(queue->device->ws,
							  256, 4,
							  RADEON_DOMAIN_GDS,
							  ring_bo_flags,
							  RADV_BO_PRIORITY_SCRATCH);
		if (!gds_bo)
			goto fail;

		gds_oa_bo = queue->device->ws->buffer_create(queue->device->ws,
							     4, 1,
							     RADEON_DOMAIN_OA,
							     ring_bo_flags,
							     RADV_BO_PRIORITY_SCRATCH);
		if (!gds_oa_bo)
			goto fail;
	} else {
		gds_bo = queue->gds_bo;
		gds_oa_bo = queue->gds_oa_bo;
	}

	if (scratch_bo != queue->scratch_bo ||
	    esgs_ring_bo != queue->esgs_ring_bo ||
	    gsvs_ring_bo != queue->gsvs_ring_bo ||
	    tess_rings_bo != queue->tess_rings_bo ||
	    add_sample_positions) {
		uint32_t size = 0;
		if (gsvs_ring_bo || esgs_ring_bo ||
		    tess_rings_bo || add_sample_positions) {
			size = 112; /* 2 dword + 2 padding + 4 dword * 6 */
			if (add_sample_positions)
				size += 128; /* 64+32+16+8 = 120 bytes */
		}
		else if (scratch_bo)
			size = 8; /* 2 dword */

		descriptor_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                                 size,
		                                                 4096,
		                                                 RADEON_DOMAIN_VRAM,
		                                                 RADEON_FLAG_CPU_ACCESS |
								 RADEON_FLAG_NO_INTERPROCESS_SHARING |
								 RADEON_FLAG_READ_ONLY,
								 RADV_BO_PRIORITY_DESCRIPTOR);
		if (!descriptor_bo)
			goto fail;
	} else
		descriptor_bo = queue->descriptor_bo;

	if (descriptor_bo != queue->descriptor_bo) {
		uint32_t *map = (uint32_t*)queue->device->ws->buffer_map(descriptor_bo);

		if (scratch_bo) {
			uint64_t scratch_va = radv_buffer_get_va(scratch_bo);
			uint32_t rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) |
				         S_008F04_SWIZZLE_ENABLE(1);
			map[0] = scratch_va;
			map[1] = rsrc1;
		}

		if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo || add_sample_positions)
			fill_geom_tess_rings(queue, map, add_sample_positions,
					     esgs_ring_size, esgs_ring_bo,
					     gsvs_ring_size, gsvs_ring_bo,
					     tess_factor_ring_size,
					     tess_offchip_ring_offset,
					     tess_offchip_ring_size,
					     tess_rings_bo);

		queue->device->ws->buffer_unmap(descriptor_bo);
	}

	for(int i = 0; i < 3; ++i) {
		struct radeon_cmdbuf *cs = NULL;
		cs = queue->device->ws->cs_create(queue->device->ws,
						  queue->queue_family_index ? RING_COMPUTE : RING_GFX);
		if (!cs)
			goto fail;

		dest_cs[i] = cs;

		if (scratch_bo)
			radv_cs_add_buffer(queue->device->ws, cs, scratch_bo);

		/* Emit initial configuration. */
		switch (queue->queue_family_index) {
		case RADV_QUEUE_GENERAL:
			radv_init_graphics_state(cs, queue);
			break;
		case RADV_QUEUE_COMPUTE:
			radv_init_compute_state(cs, queue);
			break;
		case RADV_QUEUE_TRANSFER:
			break;
		}

		if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo)  {
			radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
			radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));

			radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
			radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
		}

		radv_emit_gs_ring_sizes(queue, cs, esgs_ring_bo, esgs_ring_size,
					gsvs_ring_bo, gsvs_ring_size);
		radv_emit_tess_factor_ring(queue, cs, hs_offchip_param,
					   tess_factor_ring_size, tess_rings_bo);
		radv_emit_global_shader_pointers(queue, cs, descriptor_bo);
		radv_emit_compute_scratch(queue, cs, compute_scratch_bo);

		if (gds_bo)
			radv_cs_add_buffer(queue->device->ws, cs, gds_bo);
		if (gds_oa_bo)
			radv_cs_add_buffer(queue->device->ws, cs, gds_oa_bo);

		if (i == 0) {
			si_cs_emit_cache_flush(cs,
			                       queue->device->physical_device->rad_info.chip_class,
					       NULL, 0,
			                       queue->queue_family_index == RING_COMPUTE &&
			                         queue->device->physical_device->rad_info.chip_class >= GFX7,
			                       (queue->queue_family_index == RADV_QUEUE_COMPUTE ? RADV_CMD_FLAG_CS_PARTIAL_FLUSH : (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH)) |
			                       RADV_CMD_FLAG_INV_ICACHE |
			                       RADV_CMD_FLAG_INV_SCACHE |
			                       RADV_CMD_FLAG_INV_VCACHE |
			                       RADV_CMD_FLAG_INV_L2 |
					       RADV_CMD_FLAG_START_PIPELINE_STATS, 0);
		} else if (i == 1) {
			si_cs_emit_cache_flush(cs,
			                       queue->device->physical_device->rad_info.chip_class,
					       NULL, 0,
			                       queue->queue_family_index == RING_COMPUTE &&
			                         queue->device->physical_device->rad_info.chip_class >= GFX7,
			                       RADV_CMD_FLAG_INV_ICACHE |
			                       RADV_CMD_FLAG_INV_SCACHE |
			                       RADV_CMD_FLAG_INV_VCACHE |
			                       RADV_CMD_FLAG_INV_L2 |
					       RADV_CMD_FLAG_START_PIPELINE_STATS, 0);
		}

		if (!queue->device->ws->cs_finalize(cs))
			goto fail;
	}

	if (queue->initial_full_flush_preamble_cs)
			queue->device->ws->cs_destroy(queue->initial_full_flush_preamble_cs);

	if (queue->initial_preamble_cs)
			queue->device->ws->cs_destroy(queue->initial_preamble_cs);

	if (queue->continue_preamble_cs)
			queue->device->ws->cs_destroy(queue->continue_preamble_cs);

	queue->initial_full_flush_preamble_cs = dest_cs[0];
	queue->initial_preamble_cs = dest_cs[1];
	queue->continue_preamble_cs = dest_cs[2];

	if (scratch_bo != queue->scratch_bo) {
		if (queue->scratch_bo)
			queue->device->ws->buffer_destroy(queue->scratch_bo);
		queue->scratch_bo = scratch_bo;
		queue->scratch_size = scratch_size;
	}

	if (compute_scratch_bo != queue->compute_scratch_bo) {
		if (queue->compute_scratch_bo)
			queue->device->ws->buffer_destroy(queue->compute_scratch_bo);
		queue->compute_scratch_bo = compute_scratch_bo;
		queue->compute_scratch_size = compute_scratch_size;
	}

	if (esgs_ring_bo != queue->esgs_ring_bo) {
		if (queue->esgs_ring_bo)
			queue->device->ws->buffer_destroy(queue->esgs_ring_bo);
		queue->esgs_ring_bo = esgs_ring_bo;
		queue->esgs_ring_size = esgs_ring_size;
	}

	if (gsvs_ring_bo != queue->gsvs_ring_bo) {
		if (queue->gsvs_ring_bo)
			queue->device->ws->buffer_destroy(queue->gsvs_ring_bo);
		queue->gsvs_ring_bo = gsvs_ring_bo;
		queue->gsvs_ring_size = gsvs_ring_size;
	}

	if (tess_rings_bo != queue->tess_rings_bo) {
		queue->tess_rings_bo = tess_rings_bo;
		queue->has_tess_rings = true;
	}

	if (gds_bo != queue->gds_bo) {
		queue->gds_bo = gds_bo;
		queue->has_gds = true;
	}

	if (gds_oa_bo != queue->gds_oa_bo)
		queue->gds_oa_bo = gds_oa_bo;

	if (descriptor_bo != queue->descriptor_bo) {
		if (queue->descriptor_bo)
			queue->device->ws->buffer_destroy(queue->descriptor_bo);

		queue->descriptor_bo = descriptor_bo;
	}

	if (add_sample_positions)
		queue->has_sample_positions = true;

	*initial_full_flush_preamble_cs = queue->initial_full_flush_preamble_cs;
	*initial_preamble_cs = queue->initial_preamble_cs;
	*continue_preamble_cs = queue->continue_preamble_cs;
	if (!scratch_size && !compute_scratch_size && !esgs_ring_size && !gsvs_ring_size)
			*continue_preamble_cs = NULL;
	return VK_SUCCESS;
fail:
	for (int i = 0; i < ARRAY_SIZE(dest_cs); ++i)
		if (dest_cs[i])
			queue->device->ws->cs_destroy(dest_cs[i]);
	if (descriptor_bo && descriptor_bo != queue->descriptor_bo)
		queue->device->ws->buffer_destroy(descriptor_bo);
	if (scratch_bo && scratch_bo != queue->scratch_bo)
		queue->device->ws->buffer_destroy(scratch_bo);
	if (compute_scratch_bo && compute_scratch_bo != queue->compute_scratch_bo)
		queue->device->ws->buffer_destroy(compute_scratch_bo);
	if (esgs_ring_bo && esgs_ring_bo != queue->esgs_ring_bo)
		queue->device->ws->buffer_destroy(esgs_ring_bo);
	if (gsvs_ring_bo && gsvs_ring_bo != queue->gsvs_ring_bo)
		queue->device->ws->buffer_destroy(gsvs_ring_bo);
	if (tess_rings_bo && tess_rings_bo != queue->tess_rings_bo)
		queue->device->ws->buffer_destroy(tess_rings_bo);
	if (gds_bo && gds_bo != queue->gds_bo)
		queue->device->ws->buffer_destroy(gds_bo);
	if (gds_oa_bo && gds_oa_bo != queue->gds_oa_bo)
		queue->device->ws->buffer_destroy(gds_oa_bo);

	return vk_error(queue->device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static VkResult radv_alloc_sem_counts(struct radv_device *device,
				      struct radv_winsys_sem_counts *counts,
				      int num_sems,
				      struct radv_semaphore_part **sems,
				      const uint64_t *timeline_values,
				      VkFence _fence,
				      bool is_signal)
{
	int syncobj_idx = 0, sem_idx = 0;

	if (num_sems == 0 && _fence == VK_NULL_HANDLE)
		return VK_SUCCESS;

	for (uint32_t i = 0; i < num_sems; i++) {
		switch(sems[i]->kind) {
		case RADV_SEMAPHORE_SYNCOBJ:
			counts->syncobj_count++;
			break;
		case RADV_SEMAPHORE_WINSYS:
			counts->sem_count++;
			break;
		case RADV_SEMAPHORE_NONE:
			break;
		case RADV_SEMAPHORE_TIMELINE:
			counts->syncobj_count++;
			break;
		}
	}

	if (_fence != VK_NULL_HANDLE) {
		RADV_FROM_HANDLE(radv_fence, fence, _fence);
		if (fence->temp_syncobj || fence->syncobj)
			counts->syncobj_count++;
	}

	if (counts->syncobj_count) {
		counts->syncobj = (uint32_t *)malloc(sizeof(uint32_t) * counts->syncobj_count);
		if (!counts->syncobj)
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
	}

	if (counts->sem_count) {
		counts->sem = (struct radeon_winsys_sem **)malloc(sizeof(struct radeon_winsys_sem *) * counts->sem_count);
		if (!counts->sem) {
			free(counts->syncobj);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
	}

	for (uint32_t i = 0; i < num_sems; i++) {
		switch(sems[i]->kind) {
		case RADV_SEMAPHORE_NONE:
			unreachable("Empty semaphore");
			break;
		case RADV_SEMAPHORE_SYNCOBJ:
			counts->syncobj[syncobj_idx++] = sems[i]->syncobj;
			break;
		case RADV_SEMAPHORE_WINSYS:
			counts->sem[sem_idx++] = sems[i]->ws_sem;
			break;
		case RADV_SEMAPHORE_TIMELINE: {
			pthread_mutex_lock(&sems[i]->timeline.mutex);
			struct radv_timeline_point *point = NULL;
			if (is_signal) {
				point = radv_timeline_add_point_locked(device, &sems[i]->timeline, timeline_values[i]);
			} else {
				point = radv_timeline_find_point_at_least_locked(device, &sems[i]->timeline, timeline_values[i]);
			}

			pthread_mutex_unlock(&sems[i]->timeline.mutex);

			if (point) {
				counts->syncobj[syncobj_idx++] = point->syncobj;
			} else {
				/* Explicitly remove the semaphore so we might not find
				 * a point later post-submit. */
				sems[i] = NULL;
			}
			break;
		}
		}
	}

	if (_fence != VK_NULL_HANDLE) {
		RADV_FROM_HANDLE(radv_fence, fence, _fence);
		if (fence->temp_syncobj)
			counts->syncobj[syncobj_idx++] = fence->temp_syncobj;
		else if (fence->syncobj)
			counts->syncobj[syncobj_idx++] = fence->syncobj;
	}

	assert(syncobj_idx <= counts->syncobj_count);
	counts->syncobj_count = syncobj_idx;

	return VK_SUCCESS;
}

static void
radv_free_sem_info(struct radv_winsys_sem_info *sem_info)
{
	free(sem_info->wait.syncobj);
	free(sem_info->wait.sem);
	free(sem_info->signal.syncobj);
	free(sem_info->signal.sem);
}


static void radv_free_temp_syncobjs(struct radv_device *device,
				    int num_sems,
				    struct radv_semaphore_part *sems)
{
	for (uint32_t i = 0; i < num_sems; i++) {
		radv_destroy_semaphore_part(device, sems + i);
	}
}

static VkResult
radv_alloc_sem_info(struct radv_device *device,
		    struct radv_winsys_sem_info *sem_info,
		    int num_wait_sems,
		    struct radv_semaphore_part **wait_sems,
		    const uint64_t *wait_values,
		    int num_signal_sems,
		    struct radv_semaphore_part **signal_sems,
		    const uint64_t *signal_values,
		    VkFence fence)
{
	VkResult ret;
	memset(sem_info, 0, sizeof(*sem_info));

	ret = radv_alloc_sem_counts(device, &sem_info->wait, num_wait_sems, wait_sems, wait_values, VK_NULL_HANDLE, false);
	if (ret)
		return ret;
	ret = radv_alloc_sem_counts(device, &sem_info->signal, num_signal_sems, signal_sems, signal_values, fence, true);
	if (ret)
		radv_free_sem_info(sem_info);

	/* caller can override these */
	sem_info->cs_emit_wait = true;
	sem_info->cs_emit_signal = true;
	return ret;
}

static void
radv_finalize_timelines(struct radv_device *device,
                        uint32_t num_wait_sems,
                        struct radv_semaphore_part **wait_sems,
                        const uint64_t *wait_values,
                        uint32_t num_signal_sems,
                        struct radv_semaphore_part **signal_sems,
                        const uint64_t *signal_values,
                        struct list_head *processing_list)
{
	for (uint32_t i = 0; i < num_wait_sems; ++i) {
		if (wait_sems[i] && wait_sems[i]->kind == RADV_SEMAPHORE_TIMELINE) {
			pthread_mutex_lock(&wait_sems[i]->timeline.mutex);
			struct radv_timeline_point *point =
				radv_timeline_find_point_at_least_locked(device, &wait_sems[i]->timeline, wait_values[i]);
			if (point)
				--point->wait_count;
			pthread_mutex_unlock(&wait_sems[i]->timeline.mutex);
		}
	}
	for (uint32_t i = 0; i < num_signal_sems; ++i) {
		if (signal_sems[i] && signal_sems[i]->kind == RADV_SEMAPHORE_TIMELINE) {
			pthread_mutex_lock(&signal_sems[i]->timeline.mutex);
			struct radv_timeline_point *point =
				radv_timeline_find_point_at_least_locked(device, &signal_sems[i]->timeline, signal_values[i]);
			if (point) {
				signal_sems[i]->timeline.highest_submitted =
					MAX2(signal_sems[i]->timeline.highest_submitted, point->value);
				point->wait_count--;
			}
			radv_timeline_trigger_waiters_locked(&signal_sems[i]->timeline, processing_list);
			pthread_mutex_unlock(&signal_sems[i]->timeline.mutex);
		}
	}
}

static void
radv_sparse_buffer_bind_memory(struct radv_device *device,
                               const VkSparseBufferMemoryBindInfo *bind)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, bind->buffer);

	for (uint32_t i = 0; i < bind->bindCount; ++i) {
		struct radv_device_memory *mem = NULL;

		if (bind->pBinds[i].memory != VK_NULL_HANDLE)
			mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

		device->ws->buffer_virtual_bind(buffer->bo,
		                                bind->pBinds[i].resourceOffset,
		                                bind->pBinds[i].size,
		                                mem ? mem->bo : NULL,
		                                bind->pBinds[i].memoryOffset);
	}
}

static void
radv_sparse_image_opaque_bind_memory(struct radv_device *device,
                                     const VkSparseImageOpaqueMemoryBindInfo *bind)
{
	RADV_FROM_HANDLE(radv_image, image, bind->image);

	for (uint32_t i = 0; i < bind->bindCount; ++i) {
		struct radv_device_memory *mem = NULL;

		if (bind->pBinds[i].memory != VK_NULL_HANDLE)
			mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

		device->ws->buffer_virtual_bind(image->bo,
		                                bind->pBinds[i].resourceOffset,
		                                bind->pBinds[i].size,
		                                mem ? mem->bo : NULL,
		                                bind->pBinds[i].memoryOffset);
	}
}

static VkResult
radv_get_preambles(struct radv_queue *queue,
                   const VkCommandBuffer *cmd_buffers,
                   uint32_t cmd_buffer_count,
                   struct radeon_cmdbuf **initial_full_flush_preamble_cs,
                   struct radeon_cmdbuf **initial_preamble_cs,
                   struct radeon_cmdbuf **continue_preamble_cs)
{
	uint32_t scratch_size = 0;
	uint32_t compute_scratch_size = 0;
	uint32_t esgs_ring_size = 0, gsvs_ring_size = 0;
	bool tess_rings_needed = false;
	bool gds_needed = false;
	bool sample_positions_needed = false;

	for (uint32_t j = 0; j < cmd_buffer_count; j++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer,
				 cmd_buffers[j]);

		scratch_size = MAX2(scratch_size, cmd_buffer->scratch_size_needed);
		compute_scratch_size = MAX2(compute_scratch_size,
		                            cmd_buffer->compute_scratch_size_needed);
		esgs_ring_size = MAX2(esgs_ring_size, cmd_buffer->esgs_ring_size_needed);
		gsvs_ring_size = MAX2(gsvs_ring_size, cmd_buffer->gsvs_ring_size_needed);
		tess_rings_needed |= cmd_buffer->tess_rings_needed;
		gds_needed |= cmd_buffer->gds_needed;
		sample_positions_needed |= cmd_buffer->sample_positions_needed;
	}

	return radv_get_preamble_cs(queue, scratch_size, compute_scratch_size,
	                              esgs_ring_size, gsvs_ring_size, tess_rings_needed,
	                              gds_needed, sample_positions_needed,
	                              initial_full_flush_preamble_cs,
	                              initial_preamble_cs, continue_preamble_cs);
}

struct radv_deferred_queue_submission {
	struct radv_queue *queue;
	VkCommandBuffer *cmd_buffers;
	uint32_t cmd_buffer_count;

	/* Sparse bindings that happen on a queue. */
	VkSparseBufferMemoryBindInfo *buffer_binds;
	uint32_t buffer_bind_count;
	VkSparseImageOpaqueMemoryBindInfo *image_opaque_binds;
	uint32_t image_opaque_bind_count;

	bool flush_caches;
	VkShaderStageFlags wait_dst_stage_mask;
	struct radv_semaphore_part **wait_semaphores;
	uint32_t wait_semaphore_count;
	struct radv_semaphore_part **signal_semaphores;
	uint32_t signal_semaphore_count;
	VkFence fence;

	uint64_t *wait_values;
	uint64_t *signal_values;

	struct radv_semaphore_part *temporary_semaphore_parts;
	uint32_t temporary_semaphore_part_count;

	struct list_head queue_pending_list;
	uint32_t submission_wait_count;
	struct radv_timeline_waiter *wait_nodes;

	struct list_head processing_list;
};

struct radv_queue_submission {
	const VkCommandBuffer *cmd_buffers;
	uint32_t cmd_buffer_count;

	/* Sparse bindings that happen on a queue. */
	const VkSparseBufferMemoryBindInfo *buffer_binds;
	uint32_t buffer_bind_count;
	const VkSparseImageOpaqueMemoryBindInfo *image_opaque_binds;
	uint32_t image_opaque_bind_count;

	bool flush_caches;
	VkPipelineStageFlags wait_dst_stage_mask;
	const VkSemaphore *wait_semaphores;
	uint32_t wait_semaphore_count;
	const VkSemaphore *signal_semaphores;
	uint32_t signal_semaphore_count;
	VkFence fence;

	const uint64_t *wait_values;
	uint32_t wait_value_count;
	const uint64_t *signal_values;
	uint32_t signal_value_count;
};

static VkResult
radv_create_deferred_submission(struct radv_queue *queue,
                                const struct radv_queue_submission *submission,
                                struct radv_deferred_queue_submission **out)
{
	struct radv_deferred_queue_submission *deferred = NULL;
	size_t size = sizeof(struct radv_deferred_queue_submission);

	uint32_t temporary_count = 0;
	for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
		RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->wait_semaphores[i]);
		if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE)
			++temporary_count;
	}

	size += submission->cmd_buffer_count * sizeof(VkCommandBuffer);
	size += submission->buffer_bind_count * sizeof(VkSparseBufferMemoryBindInfo);
	size += submission->image_opaque_bind_count * sizeof(VkSparseImageOpaqueMemoryBindInfo);
	size += submission->wait_semaphore_count * sizeof(struct radv_semaphore_part *);
	size += temporary_count * sizeof(struct radv_semaphore_part);
	size += submission->signal_semaphore_count * sizeof(struct radv_semaphore_part *);
	size += submission->wait_value_count * sizeof(uint64_t);
	size += submission->signal_value_count * sizeof(uint64_t);
	size += submission->wait_semaphore_count * sizeof(struct radv_timeline_waiter);

	deferred = calloc(1, size);
	if (!deferred)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	deferred->queue = queue;

	deferred->cmd_buffers = (void*)(deferred + 1);
	deferred->cmd_buffer_count = submission->cmd_buffer_count;
	memcpy(deferred->cmd_buffers, submission->cmd_buffers,
	       submission->cmd_buffer_count * sizeof(*deferred->cmd_buffers));

	deferred->buffer_binds = (void*)(deferred->cmd_buffers + submission->cmd_buffer_count);
	deferred->buffer_bind_count = submission->buffer_bind_count;
	memcpy(deferred->buffer_binds, submission->buffer_binds,
	       submission->buffer_bind_count * sizeof(*deferred->buffer_binds));

	deferred->image_opaque_binds = (void*)(deferred->buffer_binds + submission->buffer_bind_count);
	deferred->image_opaque_bind_count = submission->image_opaque_bind_count;
	memcpy(deferred->image_opaque_binds, submission->image_opaque_binds,
	       submission->image_opaque_bind_count * sizeof(*deferred->image_opaque_binds));

	deferred->flush_caches = submission->flush_caches;
	deferred->wait_dst_stage_mask = submission->wait_dst_stage_mask;

	deferred->wait_semaphores = (void*)(deferred->image_opaque_binds + deferred->image_opaque_bind_count);
	deferred->wait_semaphore_count = submission->wait_semaphore_count;

	deferred->signal_semaphores = (void*)(deferred->wait_semaphores + deferred->wait_semaphore_count);
	deferred->signal_semaphore_count = submission->signal_semaphore_count;

	deferred->fence = submission->fence;

	deferred->temporary_semaphore_parts = (void*)(deferred->signal_semaphores + deferred->signal_semaphore_count);
	deferred->temporary_semaphore_part_count = temporary_count;

	uint32_t temporary_idx = 0;
	for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
		RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->wait_semaphores[i]);
		if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE) {
			deferred->wait_semaphores[i] = &deferred->temporary_semaphore_parts[temporary_idx];
			deferred->temporary_semaphore_parts[temporary_idx] = semaphore->temporary;
			semaphore->temporary.kind = RADV_SEMAPHORE_NONE;
			++temporary_idx;
		} else
			deferred->wait_semaphores[i] = &semaphore->permanent;
	}

	for (uint32_t i = 0; i < submission->signal_semaphore_count; ++i) {
		RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->signal_semaphores[i]);
		if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE) {
			deferred->signal_semaphores[i] = &semaphore->temporary;
		} else {
			deferred->signal_semaphores[i] = &semaphore->permanent;
		}
	}

	deferred->wait_values = (void*)(deferred->temporary_semaphore_parts + temporary_count);
	memcpy(deferred->wait_values, submission->wait_values, submission->wait_value_count * sizeof(uint64_t));
	deferred->signal_values = deferred->wait_values + submission->wait_value_count;
	memcpy(deferred->signal_values, submission->signal_values, submission->signal_value_count * sizeof(uint64_t));

	deferred->wait_nodes = (void*)(deferred->signal_values + submission->signal_value_count);
	/* This is worst-case. radv_queue_enqueue_submission will fill in further, but this
	 * ensure the submission is not accidentally triggered early when adding wait timelines. */
	deferred->submission_wait_count = 1 + submission->wait_semaphore_count;

	*out = deferred;
	return VK_SUCCESS;
}

static void
radv_queue_enqueue_submission(struct radv_deferred_queue_submission *submission,
                              struct list_head *processing_list)
{
	uint32_t wait_cnt = 0;
	struct radv_timeline_waiter *waiter = submission->wait_nodes;
	for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
		if (submission->wait_semaphores[i]->kind == RADV_SEMAPHORE_TIMELINE) {
			pthread_mutex_lock(&submission->wait_semaphores[i]->timeline.mutex);
			if (submission->wait_semaphores[i]->timeline.highest_submitted < submission->wait_values[i]) {
				++wait_cnt;
				waiter->value = submission->wait_values[i];
				waiter->submission = submission;
				list_addtail(&waiter->list, &submission->wait_semaphores[i]->timeline.waiters);
				++waiter;
			}
			pthread_mutex_unlock(&submission->wait_semaphores[i]->timeline.mutex);
		}
	}

	pthread_mutex_lock(&submission->queue->pending_mutex);

	bool is_first = list_is_empty(&submission->queue->pending_submissions);
	list_addtail(&submission->queue_pending_list, &submission->queue->pending_submissions);

	pthread_mutex_unlock(&submission->queue->pending_mutex);

	/* If there is already a submission in the queue, that will decrement the counter by 1 when
	 * submitted, but if the queue was empty, we decrement ourselves as there is no previous
	 * submission. */
	uint32_t decrement = submission->wait_semaphore_count - wait_cnt + (is_first ? 1 : 0);
	if (__atomic_sub_fetch(&submission->submission_wait_count, decrement, __ATOMIC_ACQ_REL) == 0) {
		list_addtail(&submission->processing_list, processing_list);
	}
}

static void
radv_queue_submission_update_queue(struct radv_deferred_queue_submission *submission,
                                   struct list_head *processing_list)
{
	pthread_mutex_lock(&submission->queue->pending_mutex);
	list_del(&submission->queue_pending_list);

	/* trigger the next submission in the queue. */
	if (!list_is_empty(&submission->queue->pending_submissions)) {
		struct radv_deferred_queue_submission *next_submission =
			list_first_entry(&submission->queue->pending_submissions,
			                 struct radv_deferred_queue_submission,
			                 queue_pending_list);
		if (p_atomic_dec_zero(&next_submission->submission_wait_count)) {
			list_addtail(&next_submission->processing_list, processing_list);
		}
	}
	pthread_mutex_unlock(&submission->queue->pending_mutex);

	pthread_cond_broadcast(&submission->queue->device->timeline_cond);
}

static VkResult
radv_queue_submit_deferred(struct radv_deferred_queue_submission *submission,
                           struct list_head *processing_list)
{
	RADV_FROM_HANDLE(radv_fence, fence, submission->fence);
	struct radv_queue *queue = submission->queue;
	struct radeon_winsys_ctx *ctx = queue->hw_ctx;
	uint32_t max_cs_submission = queue->device->trace_bo ? 1 : RADV_MAX_IBS_PER_SUBMIT;
	struct radeon_winsys_fence *base_fence = fence ? fence->fence : NULL;
	bool do_flush = submission->flush_caches || submission->wait_dst_stage_mask;
	bool can_patch = true;
	uint32_t advance;
	struct radv_winsys_sem_info sem_info;
	VkResult result;
	int ret;
	struct radeon_cmdbuf *initial_preamble_cs = NULL;
	struct radeon_cmdbuf *initial_flush_preamble_cs = NULL;
	struct radeon_cmdbuf *continue_preamble_cs = NULL;

	result = radv_get_preambles(queue, submission->cmd_buffers,
	                            submission->cmd_buffer_count,
	                            &initial_preamble_cs,
	                            &initial_flush_preamble_cs,
	                            &continue_preamble_cs);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_alloc_sem_info(queue->device,
				     &sem_info,
				     submission->wait_semaphore_count,
				     submission->wait_semaphores,
				     submission->wait_values,
				     submission->signal_semaphore_count,
				     submission->signal_semaphores,
				     submission->signal_values,
				     submission->fence);
	if (result != VK_SUCCESS)
		goto fail;

	for (uint32_t i = 0; i < submission->buffer_bind_count; ++i) {
		radv_sparse_buffer_bind_memory(queue->device,
		                               submission->buffer_binds + i);
	}

	for (uint32_t i = 0; i < submission->image_opaque_bind_count; ++i) {
		radv_sparse_image_opaque_bind_memory(queue->device,
		                                     submission->image_opaque_binds + i);
	}

	if (!submission->cmd_buffer_count) {
		ret = queue->device->ws->cs_submit(ctx, queue->queue_idx,
						   &queue->device->empty_cs[queue->queue_family_index],
						   1, NULL, NULL,
						   &sem_info, NULL,
						   false, base_fence);
		if (ret) {
			radv_loge("failed to submit CS\n");
			abort();
		}

		goto success;
	} else {
		struct radeon_cmdbuf **cs_array = malloc(sizeof(struct radeon_cmdbuf *) *
		                                         (submission->cmd_buffer_count));

		for (uint32_t j = 0; j < submission->cmd_buffer_count; j++) {
			RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, submission->cmd_buffers[j]);
			assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

			cs_array[j] = cmd_buffer->cs;
			if ((cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
				can_patch = false;

			cmd_buffer->status = RADV_CMD_BUFFER_STATUS_PENDING;
		}

		for (uint32_t j = 0; j < submission->cmd_buffer_count; j += advance) {
			struct radeon_cmdbuf *initial_preamble = (do_flush && !j) ? initial_flush_preamble_cs : initial_preamble_cs;
			const struct radv_winsys_bo_list *bo_list = NULL;

			advance = MIN2(max_cs_submission,
			               submission->cmd_buffer_count - j);

			if (queue->device->trace_bo)
				*queue->device->trace_id_ptr = 0;

			sem_info.cs_emit_wait = j == 0;
			sem_info.cs_emit_signal = j + advance == submission->cmd_buffer_count;

			if (unlikely(queue->device->use_global_bo_list)) {
				pthread_mutex_lock(&queue->device->bo_list.mutex);
				bo_list = &queue->device->bo_list.list;
			}

			ret = queue->device->ws->cs_submit(ctx, queue->queue_idx, cs_array + j,
			                                   advance, initial_preamble, continue_preamble_cs,
			                                   &sem_info, bo_list,
			                                   can_patch, base_fence);

			if (unlikely(queue->device->use_global_bo_list))
				pthread_mutex_unlock(&queue->device->bo_list.mutex);

			if (ret) {
				radv_loge("failed to submit CS\n");
				abort();
			}
			if (queue->device->trace_bo) {
				radv_check_gpu_hangs(queue, cs_array[j]);
			}
		}

		free(cs_array);
	}

success:
	radv_free_temp_syncobjs(queue->device,
				submission->temporary_semaphore_part_count,
				submission->temporary_semaphore_parts);
	radv_finalize_timelines(queue->device,
	                        submission->wait_semaphore_count,
	                        submission->wait_semaphores,
	                        submission->wait_values,
	                        submission->signal_semaphore_count,
	                        submission->signal_semaphores,
	                        submission->signal_values,
	                        processing_list);
	/* Has to happen after timeline finalization to make sure the
	 * condition variable is only triggered when timelines and queue have
	 * been updated. */
	radv_queue_submission_update_queue(submission, processing_list);
	radv_free_sem_info(&sem_info);
	free(submission);
	return VK_SUCCESS;

fail:
	radv_free_temp_syncobjs(queue->device,
				submission->temporary_semaphore_part_count,
				submission->temporary_semaphore_parts);
	free(submission);
	return VK_ERROR_DEVICE_LOST;
}

static VkResult
radv_process_submissions(struct list_head *processing_list)
{
	while(!list_is_empty(processing_list)) {
		struct radv_deferred_queue_submission *submission =
			list_first_entry(processing_list, struct radv_deferred_queue_submission, processing_list);
		list_del(&submission->processing_list);

		VkResult result = radv_queue_submit_deferred(submission, processing_list);
		if (result != VK_SUCCESS)
			return result;
	}
	return VK_SUCCESS;
}

static VkResult radv_queue_submit(struct radv_queue *queue,
                                  const struct radv_queue_submission *submission)
{
	struct radv_deferred_queue_submission *deferred = NULL;

	VkResult result = radv_create_deferred_submission(queue, submission, &deferred);
	if (result != VK_SUCCESS)
		return result;

	struct list_head processing_list;
	list_inithead(&processing_list);

	radv_queue_enqueue_submission(deferred, &processing_list);
	return radv_process_submissions(&processing_list);
}

/* Signals fence as soon as all the work currently put on queue is done. */
static VkResult radv_signal_fence(struct radv_queue *queue,
                              VkFence fence)
{
	return radv_queue_submit(queue, &(struct radv_queue_submission) {
			.fence = fence
		});
}

static bool radv_submit_has_effects(const VkSubmitInfo *info)
{
	return info->commandBufferCount ||
	       info->waitSemaphoreCount ||
	       info->signalSemaphoreCount;
}

VkResult radv_QueueSubmit(
	VkQueue                                     _queue,
	uint32_t                                    submitCount,
	const VkSubmitInfo*                         pSubmits,
	VkFence                                     fence)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	VkResult result;
	uint32_t fence_idx = 0;
	bool flushed_caches = false;

	if (fence != VK_NULL_HANDLE) {
		for (uint32_t i = 0; i < submitCount; ++i)
			if (radv_submit_has_effects(pSubmits + i))
				fence_idx = i;
	} else
		fence_idx = UINT32_MAX;

	for (uint32_t i = 0; i < submitCount; i++) {
		if (!radv_submit_has_effects(pSubmits + i) && fence_idx != i)
			continue;

		VkPipelineStageFlags wait_dst_stage_mask = 0;
		for (unsigned j = 0; j < pSubmits[i].waitSemaphoreCount; ++j) {
			wait_dst_stage_mask |= pSubmits[i].pWaitDstStageMask[j];
		}

		const VkTimelineSemaphoreSubmitInfoKHR *timeline_info =
			vk_find_struct_const(pSubmits[i].pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR);

		result = radv_queue_submit(queue, &(struct radv_queue_submission) {
				.cmd_buffers = pSubmits[i].pCommandBuffers,
				.cmd_buffer_count = pSubmits[i].commandBufferCount,
				.wait_dst_stage_mask = wait_dst_stage_mask,
				.flush_caches = !flushed_caches,
				.wait_semaphores = pSubmits[i].pWaitSemaphores,
				.wait_semaphore_count = pSubmits[i].waitSemaphoreCount,
				.signal_semaphores = pSubmits[i].pSignalSemaphores,
				.signal_semaphore_count = pSubmits[i].signalSemaphoreCount,
				.fence = i == fence_idx ? fence : VK_NULL_HANDLE,
				.wait_values = timeline_info ? timeline_info->pWaitSemaphoreValues : NULL,
				.wait_value_count = timeline_info && timeline_info->pWaitSemaphoreValues ? timeline_info->waitSemaphoreValueCount : 0,
				.signal_values = timeline_info ? timeline_info->pSignalSemaphoreValues : NULL,
				.signal_value_count = timeline_info && timeline_info->pSignalSemaphoreValues ? timeline_info->signalSemaphoreValueCount : 0,
			});
		if (result != VK_SUCCESS)
			return result;

		flushed_caches  = true;
	}

	if (fence != VK_NULL_HANDLE && !submitCount) {
		result = radv_signal_fence(queue, fence);
		if (result != VK_SUCCESS)
			return result;
	}

	return VK_SUCCESS;
}

VkResult radv_QueueWaitIdle(
	VkQueue                                     _queue)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);

	pthread_mutex_lock(&queue->pending_mutex);
	while (!list_is_empty(&queue->pending_submissions)) {
		pthread_cond_wait(&queue->device->timeline_cond, &queue->pending_mutex);
	}
	pthread_mutex_unlock(&queue->pending_mutex);

	queue->device->ws->ctx_wait_idle(queue->hw_ctx,
	                                 radv_queue_family_to_ring(queue->queue_family_index),
	                                 queue->queue_idx);
	return VK_SUCCESS;
}

VkResult radv_DeviceWaitIdle(
	VkDevice                                    _device)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++) {
			radv_QueueWaitIdle(radv_queue_to_handle(&device->queues[i][q]));
		}
	}
	return VK_SUCCESS;
}

VkResult radv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
	VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

	for (int i = 0; i < RADV_INSTANCE_EXTENSION_COUNT; i++) {
		if (radv_supported_instance_extensions.extensions[i]) {
			vk_outarray_append(&out, prop) {
				*prop = radv_instance_extensions[i];
			}
		}
	}

	return vk_outarray_status(&out);
}

VkResult radv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

	for (int i = 0; i < RADV_DEVICE_EXTENSION_COUNT; i++) {
		if (device->supported_extensions.extensions[i]) {
			vk_outarray_append(&out, prop) {
				*prop = radv_device_extensions[i];
			}
		}
	}

	return vk_outarray_status(&out);
}

PFN_vkVoidFunction radv_GetInstanceProcAddr(
	VkInstance                                  _instance,
	const char*                                 pName)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	bool unchecked = instance ? instance->debug_flags & RADV_DEBUG_ALL_ENTRYPOINTS : false;

	if (unchecked) {
		return radv_lookup_entrypoint_unchecked(pName);
	} else {
		return radv_lookup_entrypoint_checked(pName,
						      instance ? instance->apiVersion : 0,
						      instance ? &instance->enabled_extensions : NULL,
						      NULL);
	}
}

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
	VkInstance                                  instance,
	const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
	VkInstance                                  instance,
	const char*                                 pName)
{
	return radv_GetInstanceProcAddr(instance, pName);
}

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
	VkInstance                                  _instance,
	const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
	VkInstance                                  _instance,
	const char*                                 pName)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);

	return radv_lookup_physical_device_entrypoint_checked(pName,
	                                                      instance ? instance->apiVersion : 0,
	                                                      instance ? &instance->enabled_extensions : NULL);
}

PFN_vkVoidFunction radv_GetDeviceProcAddr(
	VkDevice                                    _device,
	const char*                                 pName)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	bool unchecked = device ? device->instance->debug_flags & RADV_DEBUG_ALL_ENTRYPOINTS : false;

	if (unchecked) {
		return radv_lookup_entrypoint_unchecked(pName);
	} else {
		return radv_lookup_entrypoint_checked(pName,
						      device->instance->apiVersion,
						      &device->instance->enabled_extensions,
						      &device->enabled_extensions);
	}
}

bool radv_get_memory_fd(struct radv_device *device,
			struct radv_device_memory *memory,
			int *pFD)
{
	struct radeon_bo_metadata metadata;

	if (memory->image) {
		radv_init_metadata(device, memory->image, &metadata);
		device->ws->buffer_set_metadata(memory->bo, &metadata);
	}

	return device->ws->buffer_get_fd(device->ws, memory->bo,
					 pFD);
}


static void radv_free_memory(struct radv_device *device,
			     const VkAllocationCallbacks* pAllocator,
			     struct radv_device_memory *mem)
{
	if (mem == NULL)
		return;

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	if (mem->android_hardware_buffer)
		AHardwareBuffer_release(mem->android_hardware_buffer);
#endif

	if (mem->bo) {
		radv_bo_list_remove(device, mem->bo);
		device->ws->buffer_destroy(mem->bo);
		mem->bo = NULL;
	}

	vk_free2(&device->alloc, pAllocator, mem);
}

static VkResult radv_alloc_memory(struct radv_device *device,
				  const VkMemoryAllocateInfo*     pAllocateInfo,
				  const VkAllocationCallbacks*    pAllocator,
				  VkDeviceMemory*                 pMem)
{
	struct radv_device_memory *mem;
	VkResult result;
	enum radeon_bo_domain domain;
	uint32_t flags = 0;
	enum radv_mem_type mem_type_index = device->physical_device->mem_type_indices[pAllocateInfo->memoryTypeIndex];

	assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

	const VkImportMemoryFdInfoKHR *import_info =
		vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
	const VkMemoryDedicatedAllocateInfo *dedicate_info =
		vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
	const VkExportMemoryAllocateInfo *export_info =
		vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
	const struct VkImportAndroidHardwareBufferInfoANDROID *ahb_import_info =
		vk_find_struct_const(pAllocateInfo->pNext,
		                     IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
	const VkImportMemoryHostPointerInfoEXT *host_ptr_info =
		vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_HOST_POINTER_INFO_EXT);

	const struct wsi_memory_allocate_info *wsi_info =
		vk_find_struct_const(pAllocateInfo->pNext, WSI_MEMORY_ALLOCATE_INFO_MESA);

	if (pAllocateInfo->allocationSize == 0 && !ahb_import_info &&
	    !(export_info && (export_info->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID))) {
		/* Apparently, this is allowed */
		*pMem = VK_NULL_HANDLE;
		return VK_SUCCESS;
	}

	mem = vk_zalloc2(&device->alloc, pAllocator, sizeof(*mem), 8,
			  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (mem == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	if (wsi_info && wsi_info->implicit_sync)
		flags |= RADEON_FLAG_IMPLICIT_SYNC;

	if (dedicate_info) {
		mem->image = radv_image_from_handle(dedicate_info->image);
		mem->buffer = radv_buffer_from_handle(dedicate_info->buffer);
	} else {
		mem->image = NULL;
		mem->buffer = NULL;
	}

	float priority_float = 0.5;
	const struct VkMemoryPriorityAllocateInfoEXT *priority_ext =
		vk_find_struct_const(pAllocateInfo->pNext,
				     MEMORY_PRIORITY_ALLOCATE_INFO_EXT);
	if (priority_ext)
		priority_float = priority_ext->priority;

	unsigned priority = MIN2(RADV_BO_PRIORITY_APPLICATION_MAX - 1,
	                         (int)(priority_float * RADV_BO_PRIORITY_APPLICATION_MAX));

	mem->user_ptr = NULL;
	mem->bo = NULL;

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	mem->android_hardware_buffer = NULL;
#endif

	if (ahb_import_info) {
		result = radv_import_ahb_memory(device, mem, priority, ahb_import_info);
		if (result != VK_SUCCESS)
			goto fail;
	} else if(export_info && (export_info->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)) {
		result = radv_create_ahb_memory(device, mem, priority, pAllocateInfo);
		if (result != VK_SUCCESS)
			goto fail;
	} else if (import_info) {
		assert(import_info->handleType ==
		       VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
		       import_info->handleType ==
		       VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
		mem->bo = device->ws->buffer_from_fd(device->ws, import_info->fd,
						     priority, NULL);
		if (!mem->bo) {
			result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
			goto fail;
		} else {
			close(import_info->fd);
		}
	} else if (host_ptr_info) {
		assert(host_ptr_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);
		assert(mem_type_index == RADV_MEM_TYPE_GTT_CACHED);
		mem->bo = device->ws->buffer_from_ptr(device->ws, host_ptr_info->pHostPointer,
		                                      pAllocateInfo->allocationSize,
		                                      priority);
		if (!mem->bo) {
			result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
			goto fail;
		} else {
			mem->user_ptr = host_ptr_info->pHostPointer;
		}
	} else {
		uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);
		if (mem_type_index == RADV_MEM_TYPE_GTT_WRITE_COMBINE ||
		    mem_type_index == RADV_MEM_TYPE_GTT_CACHED)
			domain = RADEON_DOMAIN_GTT;
		else
			domain = RADEON_DOMAIN_VRAM;

		if (mem_type_index == RADV_MEM_TYPE_VRAM)
			flags |= RADEON_FLAG_NO_CPU_ACCESS;
		else
			flags |= RADEON_FLAG_CPU_ACCESS;

		if (mem_type_index == RADV_MEM_TYPE_GTT_WRITE_COMBINE)
			flags |= RADEON_FLAG_GTT_WC;

		if (!dedicate_info && !import_info && (!export_info || !export_info->handleTypes)) {
			flags |= RADEON_FLAG_NO_INTERPROCESS_SHARING;
			if (device->use_global_bo_list) {
				flags |= RADEON_FLAG_PREFER_LOCAL_BO;
			}
		}

		mem->bo = device->ws->buffer_create(device->ws, alloc_size, device->physical_device->rad_info.max_alignment,
		                                    domain, flags, priority);

		if (!mem->bo) {
			result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
			goto fail;
		}
		mem->type_index = mem_type_index;
	}

	result = radv_bo_list_add(device, mem->bo);
	if (result != VK_SUCCESS)
		goto fail;

	*pMem = radv_device_memory_to_handle(mem);

	return VK_SUCCESS;

fail:
	radv_free_memory(device, pAllocator,mem);
	vk_free2(&device->alloc, pAllocator, mem);

	return result;
}

VkResult radv_AllocateMemory(
	VkDevice                                    _device,
	const VkMemoryAllocateInfo*                 pAllocateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDeviceMemory*                             pMem)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	return radv_alloc_memory(device, pAllocateInfo, pAllocator, pMem);
}

void radv_FreeMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _mem,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

	radv_free_memory(device, pAllocator, mem);
}

VkResult radv_MapMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _memory,
	VkDeviceSize                                offset,
	VkDeviceSize                                size,
	VkMemoryMapFlags                            flags,
	void**                                      ppData)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

	if (mem == NULL) {
		*ppData = NULL;
		return VK_SUCCESS;
	}

	if (mem->user_ptr)
		*ppData = mem->user_ptr;
	else
		*ppData = device->ws->buffer_map(mem->bo);

	if (*ppData) {
		*ppData += offset;
		return VK_SUCCESS;
	}

	return vk_error(device->instance, VK_ERROR_MEMORY_MAP_FAILED);
}

void radv_UnmapMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _memory)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

	if (mem == NULL)
		return;

	if (mem->user_ptr == NULL)
		device->ws->buffer_unmap(mem->bo);
}

VkResult radv_FlushMappedMemoryRanges(
	VkDevice                                    _device,
	uint32_t                                    memoryRangeCount,
	const VkMappedMemoryRange*                  pMemoryRanges)
{
	return VK_SUCCESS;
}

VkResult radv_InvalidateMappedMemoryRanges(
	VkDevice                                    _device,
	uint32_t                                    memoryRangeCount,
	const VkMappedMemoryRange*                  pMemoryRanges)
{
	return VK_SUCCESS;
}

void radv_GetBufferMemoryRequirements(
	VkDevice                                    _device,
	VkBuffer                                    _buffer,
	VkMemoryRequirements*                       pMemoryRequirements)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

	pMemoryRequirements->memoryTypeBits = (1u << device->physical_device->memory_properties.memoryTypeCount) - 1;

	if (buffer->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
		pMemoryRequirements->alignment = 4096;
	else
		pMemoryRequirements->alignment = 16;

	pMemoryRequirements->size = align64(buffer->size, pMemoryRequirements->alignment);
}

void radv_GetBufferMemoryRequirements2(
	VkDevice                                     device,
	const VkBufferMemoryRequirementsInfo2       *pInfo,
	VkMemoryRequirements2                       *pMemoryRequirements)
{
	radv_GetBufferMemoryRequirements(device, pInfo->buffer,
                                        &pMemoryRequirements->memoryRequirements);
	RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
	vk_foreach_struct(ext, pMemoryRequirements->pNext) {
		switch (ext->sType) {
		case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
			VkMemoryDedicatedRequirements *req =
			               (VkMemoryDedicatedRequirements *) ext;
			req->requiresDedicatedAllocation = buffer->shareable;
			req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
			break;
		}
		default:
			break;
		}
	}
}

void radv_GetImageMemoryRequirements(
	VkDevice                                    _device,
	VkImage                                     _image,
	VkMemoryRequirements*                       pMemoryRequirements)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_image, image, _image);

	pMemoryRequirements->memoryTypeBits = (1u << device->physical_device->memory_properties.memoryTypeCount) - 1;

	pMemoryRequirements->size = image->size;
	pMemoryRequirements->alignment = image->alignment;
}

void radv_GetImageMemoryRequirements2(
	VkDevice                                    device,
	const VkImageMemoryRequirementsInfo2       *pInfo,
	VkMemoryRequirements2                      *pMemoryRequirements)
{
	radv_GetImageMemoryRequirements(device, pInfo->image,
                                        &pMemoryRequirements->memoryRequirements);

	RADV_FROM_HANDLE(radv_image, image, pInfo->image);

	vk_foreach_struct(ext, pMemoryRequirements->pNext) {
		switch (ext->sType) {
		case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
			VkMemoryDedicatedRequirements *req =
			               (VkMemoryDedicatedRequirements *) ext;
			req->requiresDedicatedAllocation = image->shareable;
			req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
			break;
		}
		default:
			break;
		}
	}
}

void radv_GetImageSparseMemoryRequirements(
	VkDevice                                    device,
	VkImage                                     image,
	uint32_t*                                   pSparseMemoryRequirementCount,
	VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
	stub();
}

void radv_GetImageSparseMemoryRequirements2(
	VkDevice                                    device,
	const VkImageSparseMemoryRequirementsInfo2 *pInfo,
	uint32_t*                                   pSparseMemoryRequirementCount,
	VkSparseImageMemoryRequirements2           *pSparseMemoryRequirements)
{
	stub();
}

void radv_GetDeviceMemoryCommitment(
	VkDevice                                    device,
	VkDeviceMemory                              memory,
	VkDeviceSize*                               pCommittedMemoryInBytes)
{
	*pCommittedMemoryInBytes = 0;
}

VkResult radv_BindBufferMemory2(VkDevice device,
                                uint32_t bindInfoCount,
                                const VkBindBufferMemoryInfo *pBindInfos)
{
	for (uint32_t i = 0; i < bindInfoCount; ++i) {
		RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
		RADV_FROM_HANDLE(radv_buffer, buffer, pBindInfos[i].buffer);

		if (mem) {
			buffer->bo = mem->bo;
			buffer->offset = pBindInfos[i].memoryOffset;
		} else {
			buffer->bo = NULL;
		}
	}
	return VK_SUCCESS;
}

VkResult radv_BindBufferMemory(
	VkDevice                                    device,
	VkBuffer                                    buffer,
	VkDeviceMemory                              memory,
	VkDeviceSize                                memoryOffset)
{
	const VkBindBufferMemoryInfo info = {
		.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
		.buffer = buffer,
		.memory = memory,
		.memoryOffset = memoryOffset
	};

	return radv_BindBufferMemory2(device, 1, &info);
}

VkResult radv_BindImageMemory2(VkDevice device,
                               uint32_t bindInfoCount,
                               const VkBindImageMemoryInfo *pBindInfos)
{
	for (uint32_t i = 0; i < bindInfoCount; ++i) {
		RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
		RADV_FROM_HANDLE(radv_image, image, pBindInfos[i].image);

		if (mem) {
			image->bo = mem->bo;
			image->offset = pBindInfos[i].memoryOffset;
		} else {
			image->bo = NULL;
			image->offset = 0;
		}
	}
	return VK_SUCCESS;
}


VkResult radv_BindImageMemory(
	VkDevice                                    device,
	VkImage                                     image,
	VkDeviceMemory                              memory,
	VkDeviceSize                                memoryOffset)
{
	const VkBindImageMemoryInfo info = {
		.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
		.image = image,
		.memory = memory,
		.memoryOffset = memoryOffset
	};

	return radv_BindImageMemory2(device, 1, &info);
}

static bool radv_sparse_bind_has_effects(const VkBindSparseInfo *info)
{
	return info->bufferBindCount ||
	       info->imageOpaqueBindCount ||
	       info->imageBindCount ||
	       info->waitSemaphoreCount ||
	       info->signalSemaphoreCount;
}

 VkResult radv_QueueBindSparse(
	VkQueue                                     _queue,
	uint32_t                                    bindInfoCount,
	const VkBindSparseInfo*                     pBindInfo,
	VkFence                                     fence)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	VkResult result;
	uint32_t fence_idx = 0;

	if (fence != VK_NULL_HANDLE) {
		for (uint32_t i = 0; i < bindInfoCount; ++i)
			if (radv_sparse_bind_has_effects(pBindInfo + i))
				fence_idx = i;
	} else
		fence_idx = UINT32_MAX;

	for (uint32_t i = 0; i < bindInfoCount; ++i) {
		if (i != fence_idx && !radv_sparse_bind_has_effects(pBindInfo + i))
			continue;

		const VkTimelineSemaphoreSubmitInfoKHR *timeline_info =
			vk_find_struct_const(pBindInfo[i].pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR);

		VkResult result = radv_queue_submit(queue, &(struct radv_queue_submission) {
				.buffer_binds = pBindInfo[i].pBufferBinds,
				.buffer_bind_count = pBindInfo[i].bufferBindCount,
				.image_opaque_binds = pBindInfo[i].pImageOpaqueBinds,
				.image_opaque_bind_count = pBindInfo[i].imageOpaqueBindCount,
				.wait_semaphores = pBindInfo[i].pWaitSemaphores,
				.wait_semaphore_count = pBindInfo[i].waitSemaphoreCount,
				.signal_semaphores = pBindInfo[i].pSignalSemaphores,
				.signal_semaphore_count = pBindInfo[i].signalSemaphoreCount,
				.fence = i == fence_idx ? fence : VK_NULL_HANDLE,
				.wait_values = timeline_info ? timeline_info->pWaitSemaphoreValues : NULL,
				.wait_value_count = timeline_info && timeline_info->pWaitSemaphoreValues ? timeline_info->waitSemaphoreValueCount : 0,
				.signal_values = timeline_info ? timeline_info->pSignalSemaphoreValues : NULL,
				.signal_value_count = timeline_info && timeline_info->pSignalSemaphoreValues ? timeline_info->signalSemaphoreValueCount : 0,
			});

		if (result != VK_SUCCESS)
			return result;
	}

	if (fence != VK_NULL_HANDLE && !bindInfoCount) {
		result = radv_signal_fence(queue, fence);
		if (result != VK_SUCCESS)
			return result;
	}

	return VK_SUCCESS;
}

VkResult radv_CreateFence(
	VkDevice                                    _device,
	const VkFenceCreateInfo*                    pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkFence*                                    pFence)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	const VkExportFenceCreateInfo *export =
		vk_find_struct_const(pCreateInfo->pNext, EXPORT_FENCE_CREATE_INFO);
	VkExternalFenceHandleTypeFlags handleTypes =
		export ? export->handleTypes : 0;

	struct radv_fence *fence = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*fence), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!fence)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	fence->fence_wsi = NULL;
	fence->temp_syncobj = 0;
	if (device->always_use_syncobj || handleTypes) {
		int ret = device->ws->create_syncobj(device->ws, &fence->syncobj);
		if (ret) {
			vk_free2(&device->alloc, pAllocator, fence);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) {
			device->ws->signal_syncobj(device->ws, fence->syncobj);
		}
		fence->fence = NULL;
	} else {
		fence->fence = device->ws->create_fence();
		if (!fence->fence) {
			vk_free2(&device->alloc, pAllocator, fence);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		fence->syncobj = 0;
		if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
			device->ws->signal_fence(fence->fence);
	}

	*pFence = radv_fence_to_handle(fence);

	return VK_SUCCESS;
}

void radv_DestroyFence(
	VkDevice                                    _device,
	VkFence                                     _fence,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	if (!fence)
		return;

	if (fence->temp_syncobj)
		device->ws->destroy_syncobj(device->ws, fence->temp_syncobj);
	if (fence->syncobj)
		device->ws->destroy_syncobj(device->ws, fence->syncobj);
	if (fence->fence)
		device->ws->destroy_fence(fence->fence);
	if (fence->fence_wsi)
		fence->fence_wsi->destroy(fence->fence_wsi);
	vk_free2(&device->alloc, pAllocator, fence);
}


uint64_t radv_get_current_time(void)
{
	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	return tv.tv_nsec + tv.tv_sec*1000000000ull;
}

static uint64_t radv_get_absolute_timeout(uint64_t timeout)
{
	uint64_t current_time = radv_get_current_time();

	timeout = MIN2(UINT64_MAX - current_time, timeout);

	return current_time + timeout;
}


static bool radv_all_fences_plain_and_submitted(struct radv_device *device,
                                                uint32_t fenceCount, const VkFence *pFences)
{
	for (uint32_t i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		if (fence->fence == NULL || fence->syncobj ||
		    fence->temp_syncobj || fence->fence_wsi ||
		    (!device->ws->is_fence_waitable(fence->fence)))
			return false;
	}
	return true;
}

static bool radv_all_fences_syncobj(uint32_t fenceCount, const VkFence *pFences)
{
	for (uint32_t i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		if (fence->syncobj == 0 && fence->temp_syncobj == 0)
			return false;
	}
	return true;
}

VkResult radv_WaitForFences(
	VkDevice                                    _device,
	uint32_t                                    fenceCount,
	const VkFence*                              pFences,
	VkBool32                                    waitAll,
	uint64_t                                    timeout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	timeout = radv_get_absolute_timeout(timeout);

	if (device->always_use_syncobj &&
	    radv_all_fences_syncobj(fenceCount, pFences))
	{
		uint32_t *handles = malloc(sizeof(uint32_t) * fenceCount);
		if (!handles)
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

		for (uint32_t i = 0; i < fenceCount; ++i) {
			RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
			handles[i] = fence->temp_syncobj ? fence->temp_syncobj : fence->syncobj;
		}

		bool success = device->ws->wait_syncobj(device->ws, handles, fenceCount, waitAll, timeout);

		free(handles);
		return success ? VK_SUCCESS : VK_TIMEOUT;
	}

	if (!waitAll && fenceCount > 1) {
		/* Not doing this by default for waitAll, due to needing to allocate twice. */
		if (device->physical_device->rad_info.drm_minor >= 10 && radv_all_fences_plain_and_submitted(device, fenceCount, pFences)) {
			uint32_t wait_count = 0;
			struct radeon_winsys_fence **fences = malloc(sizeof(struct radeon_winsys_fence *) * fenceCount);
			if (!fences)
				return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

			for (uint32_t i = 0; i < fenceCount; ++i) {
				RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);

				if (device->ws->fence_wait(device->ws, fence->fence, false, 0)) {
					free(fences);
					return VK_SUCCESS;
				}

				fences[wait_count++] = fence->fence;
			}

			bool success = device->ws->fences_wait(device->ws, fences, wait_count,
							       waitAll, timeout - radv_get_current_time());

			free(fences);
			return success ? VK_SUCCESS : VK_TIMEOUT;
		}

		while(radv_get_current_time() <= timeout) {
			for (uint32_t i = 0; i < fenceCount; ++i) {
				if (radv_GetFenceStatus(_device, pFences[i]) == VK_SUCCESS)
					return VK_SUCCESS;
			}
		}
		return VK_TIMEOUT;
	}

	for (uint32_t i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		bool expired = false;

		if (fence->temp_syncobj) {
			if (!device->ws->wait_syncobj(device->ws, &fence->temp_syncobj, 1, true, timeout))
				return VK_TIMEOUT;
			continue;
		}

		if (fence->syncobj) {
			if (!device->ws->wait_syncobj(device->ws, &fence->syncobj, 1, true, timeout))
				return VK_TIMEOUT;
			continue;
		}

		if (fence->fence) {
			if (!device->ws->is_fence_waitable(fence->fence)) {
				while(!device->ws->is_fence_waitable(fence->fence) &&
				      radv_get_current_time() <= timeout)
					/* Do nothing */;
			}

			expired = device->ws->fence_wait(device->ws,
							 fence->fence,
							 true, timeout);
			if (!expired)
				return VK_TIMEOUT;
		}

		if (fence->fence_wsi) {
			VkResult result = fence->fence_wsi->wait(fence->fence_wsi, timeout);
			if (result != VK_SUCCESS)
				return result;
		}
	}

	return VK_SUCCESS;
}

VkResult radv_ResetFences(VkDevice _device,
			  uint32_t fenceCount,
			  const VkFence *pFences)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	for (unsigned i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		if (fence->fence)
			device->ws->reset_fence(fence->fence);

		/* Per spec, we first restore the permanent payload, and then reset, so
		 * having a temp syncobj should not skip resetting the permanent syncobj. */
		if (fence->temp_syncobj) {
			device->ws->destroy_syncobj(device->ws, fence->temp_syncobj);
			fence->temp_syncobj = 0;
		}

		if (fence->syncobj) {
			device->ws->reset_syncobj(device->ws, fence->syncobj);
		}
	}

	return VK_SUCCESS;
}

VkResult radv_GetFenceStatus(VkDevice _device, VkFence _fence)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	if (fence->temp_syncobj) {
			bool success = device->ws->wait_syncobj(device->ws, &fence->temp_syncobj, 1, true, 0);
			return success ? VK_SUCCESS : VK_NOT_READY;
	}

	if (fence->syncobj) {
			bool success = device->ws->wait_syncobj(device->ws, &fence->syncobj, 1, true, 0);
			return success ? VK_SUCCESS : VK_NOT_READY;
	}

	if (fence->fence) {
		if (!device->ws->fence_wait(device->ws, fence->fence, false, 0))
			return VK_NOT_READY;
	}
	if (fence->fence_wsi) {
		VkResult result = fence->fence_wsi->wait(fence->fence_wsi, 0);

		if (result != VK_SUCCESS) {
			if (result == VK_TIMEOUT)
				return VK_NOT_READY;
			return result;
		}
	}
	return VK_SUCCESS;
}


// Queue semaphore functions

static void
radv_create_timeline(struct radv_timeline *timeline, uint64_t value)
{
	timeline->highest_signaled = value;
	timeline->highest_submitted = value;
	list_inithead(&timeline->points);
	list_inithead(&timeline->free_points);
	list_inithead(&timeline->waiters);
	pthread_mutex_init(&timeline->mutex, NULL);
}

static void
radv_destroy_timeline(struct radv_device *device,
                      struct radv_timeline *timeline)
{
	list_for_each_entry_safe(struct radv_timeline_point, point,
	                         &timeline->free_points, list) {
		list_del(&point->list);
		device->ws->destroy_syncobj(device->ws, point->syncobj);
		free(point);
	}
	list_for_each_entry_safe(struct radv_timeline_point, point,
	                         &timeline->points, list) {
		list_del(&point->list);
		device->ws->destroy_syncobj(device->ws, point->syncobj);
		free(point);
	}
	pthread_mutex_destroy(&timeline->mutex);
}

static void
radv_timeline_gc_locked(struct radv_device *device,
                        struct radv_timeline *timeline)
{
	list_for_each_entry_safe(struct radv_timeline_point, point,
	                         &timeline->points, list) {
		if (point->wait_count || point->value > timeline->highest_submitted)
			return;

		if (device->ws->wait_syncobj(device->ws, &point->syncobj, 1, true, 0)) {
			timeline->highest_signaled = point->value;
			list_del(&point->list);
			list_add(&point->list, &timeline->free_points);
		}
	}
}

static struct radv_timeline_point *
radv_timeline_find_point_at_least_locked(struct radv_device *device,
                                         struct radv_timeline *timeline,
                                         uint64_t p)
{
	radv_timeline_gc_locked(device, timeline);

	if (p <= timeline->highest_signaled)
		return NULL;

	list_for_each_entry(struct radv_timeline_point, point,
	                    &timeline->points, list) {
		if (point->value >= p) {
			++point->wait_count;
			return point;
		}
	}
	return NULL;
}

static struct radv_timeline_point *
radv_timeline_add_point_locked(struct radv_device *device,
                               struct radv_timeline *timeline,
                               uint64_t p)
{
	radv_timeline_gc_locked(device, timeline);

	struct radv_timeline_point *ret = NULL;
	struct radv_timeline_point *prev = NULL;

	if (p <= timeline->highest_signaled)
		return NULL;

	list_for_each_entry(struct radv_timeline_point, point,
	                    &timeline->points, list) {
		if (point->value == p) {
			return NULL;
		}

		if (point->value < p)
			prev = point;
	}

	if (list_is_empty(&timeline->free_points)) {
		ret = malloc(sizeof(struct radv_timeline_point));
		device->ws->create_syncobj(device->ws, &ret->syncobj);
	} else {
		ret = list_first_entry(&timeline->free_points, struct radv_timeline_point, list);
		list_del(&ret->list);

		device->ws->reset_syncobj(device->ws, ret->syncobj);
	}

	ret->value = p;
	ret->wait_count = 1;

	if (prev) {
		list_add(&ret->list, &prev->list);
	} else {
		list_addtail(&ret->list, &timeline->points);
	}
	return ret;
}


static VkResult
radv_timeline_wait_locked(struct radv_device *device,
                          struct radv_timeline *timeline,
                          uint64_t value,
                          uint64_t abs_timeout)
{
	while(timeline->highest_submitted < value) {
		struct timespec abstime;
		timespec_from_nsec(&abstime, abs_timeout);

		pthread_cond_timedwait(&device->timeline_cond, &timeline->mutex, &abstime);

		if (radv_get_current_time() >= abs_timeout && timeline->highest_submitted < value)
			return VK_TIMEOUT;
	}

	struct radv_timeline_point *point = radv_timeline_find_point_at_least_locked(device, timeline, value);
	if (!point)
		return VK_SUCCESS;

	point->wait_count++;

	pthread_mutex_unlock(&timeline->mutex);

	bool success = device->ws->wait_syncobj(device->ws, &point->syncobj, 1, true, abs_timeout);

	pthread_mutex_lock(&timeline->mutex);
	point->wait_count--;
	return success ? VK_SUCCESS : VK_TIMEOUT;
}

static void
radv_timeline_trigger_waiters_locked(struct radv_timeline *timeline,
                                     struct list_head *processing_list)
{
	list_for_each_entry_safe(struct radv_timeline_waiter, waiter,
	                         &timeline->waiters, list) {
		if (waiter->value > timeline->highest_submitted)
			continue;

		if (p_atomic_dec_zero(&waiter->submission->submission_wait_count)) {
			list_addtail(&waiter->submission->processing_list, processing_list);
		}
		list_del(&waiter->list);
	}
}

static
void radv_destroy_semaphore_part(struct radv_device *device,
                                 struct radv_semaphore_part *part)
{
	switch(part->kind) {
	case RADV_SEMAPHORE_NONE:
		break;
	case RADV_SEMAPHORE_WINSYS:
		device->ws->destroy_sem(part->ws_sem);
		break;
	case RADV_SEMAPHORE_TIMELINE:
		radv_destroy_timeline(device, &part->timeline);
		break;
	case RADV_SEMAPHORE_SYNCOBJ:
		device->ws->destroy_syncobj(device->ws, part->syncobj);
		break;
	}
	part->kind = RADV_SEMAPHORE_NONE;
}

static VkSemaphoreTypeKHR
radv_get_semaphore_type(const void *pNext, uint64_t *initial_value)
{
	const VkSemaphoreTypeCreateInfoKHR *type_info =
		vk_find_struct_const(pNext, SEMAPHORE_TYPE_CREATE_INFO_KHR);

	if (!type_info)
		return VK_SEMAPHORE_TYPE_BINARY_KHR;

	if (initial_value)
		*initial_value = type_info->initialValue;
	return type_info->semaphoreType;
}

VkResult radv_CreateSemaphore(
	VkDevice                                    _device,
	const VkSemaphoreCreateInfo*                pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkSemaphore*                                pSemaphore)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	const VkExportSemaphoreCreateInfo *export =
		vk_find_struct_const(pCreateInfo->pNext, EXPORT_SEMAPHORE_CREATE_INFO);
	VkExternalSemaphoreHandleTypeFlags handleTypes =
		export ? export->handleTypes : 0;
	uint64_t initial_value = 0;
	VkSemaphoreTypeKHR type = radv_get_semaphore_type(pCreateInfo->pNext, &initial_value);

	struct radv_semaphore *sem = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*sem), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!sem)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	sem->temporary.kind = RADV_SEMAPHORE_NONE;
	sem->permanent.kind = RADV_SEMAPHORE_NONE;

	if (type == VK_SEMAPHORE_TYPE_TIMELINE_KHR) {
		radv_create_timeline(&sem->permanent.timeline, initial_value);
		sem->permanent.kind = RADV_SEMAPHORE_TIMELINE;
	} else if (device->always_use_syncobj || handleTypes) {
		assert (device->physical_device->rad_info.has_syncobj);
		int ret = device->ws->create_syncobj(device->ws, &sem->permanent.syncobj);
		if (ret) {
			vk_free2(&device->alloc, pAllocator, sem);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		sem->permanent.kind = RADV_SEMAPHORE_SYNCOBJ;
	} else {
		sem->permanent.ws_sem = device->ws->create_sem(device->ws);
		if (!sem->permanent.ws_sem) {
			vk_free2(&device->alloc, pAllocator, sem);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		sem->permanent.kind = RADV_SEMAPHORE_WINSYS;
	}

	*pSemaphore = radv_semaphore_to_handle(sem);
	return VK_SUCCESS;
}

void radv_DestroySemaphore(
	VkDevice                                    _device,
	VkSemaphore                                 _semaphore,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_semaphore, sem, _semaphore);
	if (!_semaphore)
		return;

	radv_destroy_semaphore_part(device, &sem->temporary);
	radv_destroy_semaphore_part(device, &sem->permanent);
	vk_free2(&device->alloc, pAllocator, sem);
}

VkResult
radv_GetSemaphoreCounterValueKHR(VkDevice _device,
                                 VkSemaphore _semaphore,
                                 uint64_t* pValue)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_semaphore, semaphore, _semaphore);

	struct radv_semaphore_part *part =
		semaphore->temporary.kind != RADV_SEMAPHORE_NONE ? &semaphore->temporary : &semaphore->permanent;

	switch (part->kind) {
	case RADV_SEMAPHORE_TIMELINE: {
		pthread_mutex_lock(&part->timeline.mutex);
		radv_timeline_gc_locked(device, &part->timeline);
		*pValue = part->timeline.highest_signaled;
		pthread_mutex_unlock(&part->timeline.mutex);
		return VK_SUCCESS;
	}
	case RADV_SEMAPHORE_NONE:
	case RADV_SEMAPHORE_SYNCOBJ:
	case RADV_SEMAPHORE_WINSYS:
		unreachable("Invalid semaphore type");
	}
	unreachable("Unhandled semaphore type");
}


static VkResult
radv_wait_timelines(struct radv_device *device,
                    const VkSemaphoreWaitInfoKHR* pWaitInfo,
                    uint64_t abs_timeout)
{
	if ((pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT_KHR) && pWaitInfo->semaphoreCount > 1) {
		for (;;) {
			for(uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
				RADV_FROM_HANDLE(radv_semaphore, semaphore, pWaitInfo->pSemaphores[i]);
				pthread_mutex_lock(&semaphore->permanent.timeline.mutex);
				VkResult result = radv_timeline_wait_locked(device, &semaphore->permanent.timeline, pWaitInfo->pValues[i], 0);
				pthread_mutex_unlock(&semaphore->permanent.timeline.mutex);

				if (result == VK_SUCCESS)
					return VK_SUCCESS;
			}
			if (radv_get_current_time() > abs_timeout)
				return VK_TIMEOUT;
		}
	}

	for(uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
		RADV_FROM_HANDLE(radv_semaphore, semaphore, pWaitInfo->pSemaphores[i]);
		pthread_mutex_lock(&semaphore->permanent.timeline.mutex);
		VkResult result = radv_timeline_wait_locked(device, &semaphore->permanent.timeline, pWaitInfo->pValues[i], abs_timeout);
		pthread_mutex_unlock(&semaphore->permanent.timeline.mutex);

		if (result != VK_SUCCESS)
			return result;
	}
	return VK_SUCCESS;
}
VkResult
radv_WaitSemaphoresKHR(VkDevice _device,
                       const VkSemaphoreWaitInfoKHR* pWaitInfo,
                       uint64_t timeout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	uint64_t abs_timeout = radv_get_absolute_timeout(timeout);
	return radv_wait_timelines(device, pWaitInfo, abs_timeout);
}

VkResult
radv_SignalSemaphoreKHR(VkDevice _device,
                        const VkSemaphoreSignalInfoKHR* pSignalInfo)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_semaphore, semaphore, pSignalInfo->semaphore);

	struct radv_semaphore_part *part =
		semaphore->temporary.kind != RADV_SEMAPHORE_NONE ? &semaphore->temporary : &semaphore->permanent;

	switch(part->kind) {
	case RADV_SEMAPHORE_TIMELINE: {
		pthread_mutex_lock(&part->timeline.mutex);
		radv_timeline_gc_locked(device, &part->timeline);
		part->timeline.highest_submitted = MAX2(part->timeline.highest_submitted, pSignalInfo->value);
		part->timeline.highest_signaled = MAX2(part->timeline.highest_signaled, pSignalInfo->value);

		struct list_head processing_list;
		list_inithead(&processing_list);
		radv_timeline_trigger_waiters_locked(&part->timeline, &processing_list);
		pthread_mutex_unlock(&part->timeline.mutex);

		return radv_process_submissions(&processing_list);
	}
	case RADV_SEMAPHORE_NONE:
	case RADV_SEMAPHORE_SYNCOBJ:
	case RADV_SEMAPHORE_WINSYS:
		unreachable("Invalid semaphore type");
	}
	return VK_SUCCESS;
}



VkResult radv_CreateEvent(
	VkDevice                                    _device,
	const VkEventCreateInfo*                    pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkEvent*                                    pEvent)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_event *event = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*event), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!event)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	event->bo = device->ws->buffer_create(device->ws, 8, 8,
					      RADEON_DOMAIN_GTT,
					      RADEON_FLAG_VA_UNCACHED | RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING,
					      RADV_BO_PRIORITY_FENCE);
	if (!event->bo) {
		vk_free2(&device->alloc, pAllocator, event);
		return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
	}

	event->map = (uint64_t*)device->ws->buffer_map(event->bo);

	*pEvent = radv_event_to_handle(event);

	return VK_SUCCESS;
}

void radv_DestroyEvent(
	VkDevice                                    _device,
	VkEvent                                     _event,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_event, event, _event);

	if (!event)
		return;
	device->ws->buffer_destroy(event->bo);
	vk_free2(&device->alloc, pAllocator, event);
}

VkResult radv_GetEventStatus(
	VkDevice                                    _device,
	VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);

	if (*event->map == 1)
		return VK_EVENT_SET;
	return VK_EVENT_RESET;
}

VkResult radv_SetEvent(
	VkDevice                                    _device,
	VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);
	*event->map = 1;

	return VK_SUCCESS;
}

VkResult radv_ResetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);
	*event->map = 0;

	return VK_SUCCESS;
}

VkResult radv_CreateBuffer(
	VkDevice                                    _device,
	const VkBufferCreateInfo*                   pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkBuffer*                                   pBuffer)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_buffer *buffer;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

	buffer = vk_alloc2(&device->alloc, pAllocator, sizeof(*buffer), 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (buffer == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	buffer->size = pCreateInfo->size;
	buffer->usage = pCreateInfo->usage;
	buffer->bo = NULL;
	buffer->offset = 0;
	buffer->flags = pCreateInfo->flags;

	buffer->shareable = vk_find_struct_const(pCreateInfo->pNext,
						 EXTERNAL_MEMORY_BUFFER_CREATE_INFO) != NULL;

	if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
		buffer->bo = device->ws->buffer_create(device->ws,
		                                       align64(buffer->size, 4096),
		                                       4096, 0, RADEON_FLAG_VIRTUAL,
		                                       RADV_BO_PRIORITY_VIRTUAL);
		if (!buffer->bo) {
			vk_free2(&device->alloc, pAllocator, buffer);
			return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
		}
	}

	*pBuffer = radv_buffer_to_handle(buffer);

	return VK_SUCCESS;
}

void radv_DestroyBuffer(
	VkDevice                                    _device,
	VkBuffer                                    _buffer,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

	if (!buffer)
		return;

	if (buffer->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
		device->ws->buffer_destroy(buffer->bo);

	vk_free2(&device->alloc, pAllocator, buffer);
}

VkDeviceAddress radv_GetBufferDeviceAddressEXT(
	VkDevice                                    device,
	const VkBufferDeviceAddressInfoEXT*         pInfo)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
	return radv_buffer_get_va(buffer->bo) + buffer->offset;
}


static inline unsigned
si_tile_mode_index(const struct radv_image_plane *plane, unsigned level, bool stencil)
{
	if (stencil)
		return plane->surface.u.legacy.stencil_tiling_index[level];
	else
		return plane->surface.u.legacy.tiling_index[level];
}

static uint32_t radv_surface_max_layer_count(struct radv_image_view *iview)
{
	return iview->type == VK_IMAGE_VIEW_TYPE_3D ? iview->extent.depth : (iview->base_layer + iview->layer_count);
}

static uint32_t
radv_init_dcc_control_reg(struct radv_device *device,
			  struct radv_image_view *iview)
{
	unsigned max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_256B;
	unsigned min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_32B;
	unsigned max_compressed_block_size;
	unsigned independent_128b_blocks;
	unsigned independent_64b_blocks;

	if (!radv_dcc_enabled(iview->image, iview->base_mip))
		return 0;

	if (!device->physical_device->rad_info.has_dedicated_vram) {
		/* amdvlk: [min-compressed-block-size] should be set to 32 for
		 * dGPU and 64 for APU because all of our APUs to date use
		 * DIMMs which have a request granularity size of 64B while all
		 * other chips have a 32B request size.
		 */
		min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_64B;
	}

	if (device->physical_device->rad_info.chip_class >= GFX10) {
		max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
		independent_64b_blocks = 0;
		independent_128b_blocks = 1;
	} else {
		independent_128b_blocks = 0;

		if (iview->image->info.samples > 1) {
			if (iview->image->planes[0].surface.bpe == 1)
				max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
			else if (iview->image->planes[0].surface.bpe == 2)
				max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
		}

		if (iview->image->usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
					   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
			/* If this DCC image is potentially going to be used in texture
			 * fetches, we need some special settings.
			 */
			independent_64b_blocks = 1;
			max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
		} else {
			/* MAX_UNCOMPRESSED_BLOCK_SIZE must be >=
			 * MAX_COMPRESSED_BLOCK_SIZE. Set MAX_COMPRESSED_BLOCK_SIZE as
			 * big as possible for better compression state.
			 */
			independent_64b_blocks = 0;
			max_compressed_block_size = max_uncompressed_block_size;
		}
	}

	return S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
	       S_028C78_MAX_COMPRESSED_BLOCK_SIZE(max_compressed_block_size) |
	       S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
	       S_028C78_INDEPENDENT_64B_BLOCKS(independent_64b_blocks) |
	       S_028C78_INDEPENDENT_128B_BLOCKS(independent_128b_blocks);
}

void
radv_initialise_color_surface(struct radv_device *device,
			      struct radv_color_buffer_info *cb,
			      struct radv_image_view *iview)
{
	const struct vk_format_description *desc;
	unsigned ntype, format, swap, endian;
	unsigned blend_clamp = 0, blend_bypass = 0;
	uint64_t va;
	const struct radv_image_plane *plane = &iview->image->planes[iview->plane_id];
	const struct radeon_surf *surf = &plane->surface;

	desc = vk_format_description(iview->vk_format);

	memset(cb, 0, sizeof(*cb));

	/* Intensity is implemented as Red, so treat it that way. */
	cb->cb_color_attrib = S_028C74_FORCE_DST_ALPHA_1(desc->swizzle[3] == VK_SWIZZLE_1);

	va = radv_buffer_get_va(iview->bo) + iview->image->offset + plane->offset;

	cb->cb_color_base = va >> 8;

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		struct gfx9_surf_meta_flags meta;
		if (iview->image->dcc_offset)
			meta = surf->u.gfx9.dcc;
		else
			meta = surf->u.gfx9.cmask;

		if (device->physical_device->rad_info.chip_class >= GFX10) {
			cb->cb_color_attrib3 |=	S_028EE0_COLOR_SW_MODE(surf->u.gfx9.surf.swizzle_mode) |
				S_028EE0_FMASK_SW_MODE(surf->u.gfx9.fmask.swizzle_mode) |
				S_028EE0_CMASK_PIPE_ALIGNED(surf->u.gfx9.cmask.pipe_aligned) |
				S_028EE0_DCC_PIPE_ALIGNED(surf->u.gfx9.dcc.pipe_aligned);
		} else {
			cb->cb_color_attrib |= S_028C74_COLOR_SW_MODE(surf->u.gfx9.surf.swizzle_mode) |
				S_028C74_FMASK_SW_MODE(surf->u.gfx9.fmask.swizzle_mode) |
				S_028C74_RB_ALIGNED(meta.rb_aligned) |
				S_028C74_PIPE_ALIGNED(meta.pipe_aligned);
			cb->cb_mrt_epitch = S_0287A0_EPITCH(surf->u.gfx9.surf.epitch);
		}

		cb->cb_color_base += surf->u.gfx9.surf_offset >> 8;
		cb->cb_color_base |= surf->tile_swizzle;
	} else {
		const struct legacy_surf_level *level_info = &surf->u.legacy.level[iview->base_mip];
		unsigned pitch_tile_max, slice_tile_max, tile_mode_index;

		cb->cb_color_base += level_info->offset >> 8;
		if (level_info->mode == RADEON_SURF_MODE_2D)
			cb->cb_color_base |= surf->tile_swizzle;

		pitch_tile_max = level_info->nblk_x / 8 - 1;
		slice_tile_max = (level_info->nblk_x * level_info->nblk_y) / 64 - 1;
		tile_mode_index = si_tile_mode_index(plane, iview->base_mip, false);

		cb->cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
		cb->cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);
		cb->cb_color_cmask_slice = surf->u.legacy.cmask_slice_tile_max;

		cb->cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);

		if (radv_image_has_fmask(iview->image)) {
			if (device->physical_device->rad_info.chip_class >= GFX7)
				cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(surf->u.legacy.fmask.pitch_in_pixels / 8 - 1);
			cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(surf->u.legacy.fmask.tiling_index);
			cb->cb_color_fmask_slice = S_028C88_TILE_MAX(surf->u.legacy.fmask.slice_tile_max);
		} else {
			/* This must be set for fast clear to work without FMASK. */
			if (device->physical_device->rad_info.chip_class >= GFX7)
				cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
			cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
			cb->cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
		}
	}

	/* CMASK variables */
	va = radv_buffer_get_va(iview->bo) + iview->image->offset;
	va += iview->image->cmask_offset;
	cb->cb_color_cmask = va >> 8;

	va = radv_buffer_get_va(iview->bo) + iview->image->offset;
	va += iview->image->dcc_offset;

	if (radv_dcc_enabled(iview->image, iview->base_mip) &&
	    device->physical_device->rad_info.chip_class <= GFX8)
		va += plane->surface.u.legacy.level[iview->base_mip].dcc_offset;

	unsigned dcc_tile_swizzle = surf->tile_swizzle;
	dcc_tile_swizzle &= (surf->dcc_alignment - 1) >> 8;

	cb->cb_dcc_base = va >> 8;
	cb->cb_dcc_base |= dcc_tile_swizzle;

	/* GFX10 field has the same base shift as the GFX6 field. */
	uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
	cb->cb_color_view = S_028C6C_SLICE_START(iview->base_layer) |
		S_028C6C_SLICE_MAX_GFX10(max_slice);

	if (iview->image->info.samples > 1) {
		unsigned log_samples = util_logbase2(iview->image->info.samples);

		cb->cb_color_attrib |= S_028C74_NUM_SAMPLES(log_samples) |
			S_028C74_NUM_FRAGMENTS(log_samples);
	}

	if (radv_image_has_fmask(iview->image)) {
		va = radv_buffer_get_va(iview->bo) + iview->image->offset + iview->image->fmask_offset;
		cb->cb_color_fmask = va >> 8;
		cb->cb_color_fmask |= surf->fmask_tile_swizzle;
	} else {
		cb->cb_color_fmask = cb->cb_color_base;
	}

	ntype = radv_translate_color_numformat(iview->vk_format,
					       desc,
					       vk_format_get_first_non_void_channel(iview->vk_format));
	format = radv_translate_colorformat(iview->vk_format);
	if (format == V_028C70_COLOR_INVALID || ntype == ~0u)
		radv_finishme("Illegal color\n");
	swap = radv_translate_colorswap(iview->vk_format, false);
	endian = radv_colorformat_endian_swap(format);

	/* blend clamp should be set for all NORM/SRGB types */
	if (ntype == V_028C70_NUMBER_UNORM ||
	    ntype == V_028C70_NUMBER_SNORM ||
	    ntype == V_028C70_NUMBER_SRGB)
		blend_clamp = 1;

	/* set blend bypass according to docs if SINT/UINT or
	   8/24 COLOR variants */
	if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
	    format == V_028C70_COLOR_8_24 || format == V_028C70_COLOR_24_8 ||
	    format == V_028C70_COLOR_X24_8_32_FLOAT) {
		blend_clamp = 0;
		blend_bypass = 1;
	}
#if 0
	if ((ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) &&
	    (format == V_028C70_COLOR_8 ||
	     format == V_028C70_COLOR_8_8 ||
	     format == V_028C70_COLOR_8_8_8_8))
		->color_is_int8 = true;
#endif
	cb->cb_color_info = S_028C70_FORMAT(format) |
		S_028C70_COMP_SWAP(swap) |
		S_028C70_BLEND_CLAMP(blend_clamp) |
		S_028C70_BLEND_BYPASS(blend_bypass) |
		S_028C70_SIMPLE_FLOAT(1) |
		S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM &&
				    ntype != V_028C70_NUMBER_SNORM &&
				    ntype != V_028C70_NUMBER_SRGB &&
				    format != V_028C70_COLOR_8_24 &&
				    format != V_028C70_COLOR_24_8) |
		S_028C70_NUMBER_TYPE(ntype) |
		S_028C70_ENDIAN(endian);
	if (radv_image_has_fmask(iview->image)) {
		cb->cb_color_info |= S_028C70_COMPRESSION(1);
		if (device->physical_device->rad_info.chip_class == GFX6) {
			unsigned fmask_bankh = util_logbase2(surf->u.legacy.fmask.bankh);
			cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(fmask_bankh);
		}

		if (radv_image_is_tc_compat_cmask(iview->image)) {
			/* Allow the texture block to read FMASK directly
			 * without decompressing it. This bit must be cleared
			 * when performing FMASK_DECOMPRESS or DCC_COMPRESS,
			 * otherwise the operation doesn't happen.
			 */
			cb->cb_color_info |= S_028C70_FMASK_COMPRESS_1FRAG_ONLY(1);

			/* Set CMASK into a tiling format that allows the
			 * texture block to read it.
			 */
			cb->cb_color_info |= S_028C70_CMASK_ADDR_TYPE(2);
		}
	}

	if (radv_image_has_cmask(iview->image) &&
	    !(device->instance->debug_flags & RADV_DEBUG_NO_FAST_CLEARS))
		cb->cb_color_info |= S_028C70_FAST_CLEAR(1);

	if (radv_dcc_enabled(iview->image, iview->base_mip))
		cb->cb_color_info |= S_028C70_DCC_ENABLE(1);

	cb->cb_dcc_control = radv_init_dcc_control_reg(device, iview);

	/* This must be set for fast clear to work without FMASK. */
	if (!radv_image_has_fmask(iview->image) &&
	    device->physical_device->rad_info.chip_class == GFX6) {
		unsigned bankh = util_logbase2(surf->u.legacy.bankh);
		cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
	}

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		const struct vk_format_description *format_desc = vk_format_description(iview->image->vk_format);

		unsigned mip0_depth = iview->image->type == VK_IMAGE_TYPE_3D ?
		  (iview->extent.depth - 1) : (iview->image->info.array_size - 1);
		unsigned width = iview->extent.width / (iview->plane_id ? format_desc->width_divisor : 1);
		unsigned height = iview->extent.height / (iview->plane_id ? format_desc->height_divisor : 1);

		if (device->physical_device->rad_info.chip_class >= GFX10) {
			cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX10(iview->base_mip);

			cb->cb_color_attrib3 |= S_028EE0_MIP0_DEPTH(mip0_depth) |
					        S_028EE0_RESOURCE_TYPE(surf->u.gfx9.resource_type) |
					        S_028EE0_RESOURCE_LEVEL(1);
		} else {
			cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX9(iview->base_mip);
			cb->cb_color_attrib |= S_028C74_MIP0_DEPTH(mip0_depth) |
					       S_028C74_RESOURCE_TYPE(surf->u.gfx9.resource_type);
		}

		cb->cb_color_attrib2 = S_028C68_MIP0_WIDTH(width - 1) |
			S_028C68_MIP0_HEIGHT(height - 1) |
			S_028C68_MAX_MIP(iview->image->info.levels - 1);
	}
}

static unsigned
radv_calc_decompress_on_z_planes(struct radv_device *device,
				 struct radv_image_view *iview)
{
	unsigned max_zplanes = 0;

	assert(radv_image_is_tc_compat_htile(iview->image));

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		/* Default value for 32-bit depth surfaces. */
		max_zplanes = 4;

		if (iview->vk_format == VK_FORMAT_D16_UNORM &&
		    iview->image->info.samples > 1)
			max_zplanes = 2;

		max_zplanes = max_zplanes + 1;
	} else {
		if (iview->vk_format == VK_FORMAT_D16_UNORM) {
			/* Do not enable Z plane compression for 16-bit depth
			 * surfaces because isn't supported on GFX8. Only
			 * 32-bit depth surfaces are supported by the hardware.
			 * This allows to maintain shader compatibility and to
			 * reduce the number of depth decompressions.
			 */
			max_zplanes = 1;
		} else {
			if (iview->image->info.samples <= 1)
				max_zplanes = 5;
			else if (iview->image->info.samples <= 4)
				max_zplanes = 3;
			else
				max_zplanes = 2;
		}
	}

	return max_zplanes;
}

void
radv_initialise_ds_surface(struct radv_device *device,
			   struct radv_ds_buffer_info *ds,
			   struct radv_image_view *iview)
{
	unsigned level = iview->base_mip;
	unsigned format, stencil_format;
	uint64_t va, s_offs, z_offs;
	bool stencil_only = false;
	const struct radv_image_plane *plane = &iview->image->planes[0];
	const struct radeon_surf *surf = &plane->surface;

	assert(vk_format_get_plane_count(iview->image->vk_format) == 1);

	memset(ds, 0, sizeof(*ds));
	switch (iview->image->vk_format) {
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
		ds->offset_scale = 2.0f;
		break;
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
		ds->offset_scale = 4.0f;
		break;
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) |
			S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
		ds->offset_scale = 1.0f;
		break;
	case VK_FORMAT_S8_UINT:
		stencil_only = true;
		break;
	default:
		break;
	}

	format = radv_translate_dbformat(iview->image->vk_format);
	stencil_format = surf->has_stencil ?
		V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

	uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
	ds->db_depth_view = S_028008_SLICE_START(iview->base_layer) |
		S_028008_SLICE_MAX(max_slice);
	if (device->physical_device->rad_info.chip_class >= GFX10) {
		ds->db_depth_view |= S_028008_SLICE_START_HI(iview->base_layer >> 11) |
				     S_028008_SLICE_MAX_HI(max_slice >> 11);
	}

	ds->db_htile_data_base = 0;
	ds->db_htile_surface = 0;

	va = radv_buffer_get_va(iview->bo) + iview->image->offset;
	s_offs = z_offs = va;

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		assert(surf->u.gfx9.surf_offset == 0);
		s_offs += surf->u.gfx9.stencil_offset;

		ds->db_z_info = S_028038_FORMAT(format) |
			S_028038_NUM_SAMPLES(util_logbase2(iview->image->info.samples)) |
			S_028038_SW_MODE(surf->u.gfx9.surf.swizzle_mode) |
			S_028038_MAXMIP(iview->image->info.levels - 1) |
			S_028038_ZRANGE_PRECISION(1);
		ds->db_stencil_info = S_02803C_FORMAT(stencil_format) |
			S_02803C_SW_MODE(surf->u.gfx9.stencil.swizzle_mode);

		if (device->physical_device->rad_info.chip_class == GFX9) {
			ds->db_z_info2 = S_028068_EPITCH(surf->u.gfx9.surf.epitch);
			ds->db_stencil_info2 = S_02806C_EPITCH(surf->u.gfx9.stencil.epitch);
		}

		ds->db_depth_view |= S_028008_MIPID(level);
		ds->db_depth_size = S_02801C_X_MAX(iview->image->info.width - 1) |
			S_02801C_Y_MAX(iview->image->info.height - 1);

		if (radv_htile_enabled(iview->image, level)) {
			ds->db_z_info |= S_028038_TILE_SURFACE_ENABLE(1);

			if (radv_image_is_tc_compat_htile(iview->image)) {
				unsigned max_zplanes =
					radv_calc_decompress_on_z_planes(device, iview);

				ds->db_z_info |= S_028038_DECOMPRESS_ON_N_ZPLANES(max_zplanes);

				if (device->physical_device->rad_info.chip_class >= GFX10) {
					ds->db_z_info |= S_028040_ITERATE_FLUSH(1);
					ds->db_stencil_info |= S_028044_ITERATE_FLUSH(1);
				} else {
					ds->db_z_info |= S_028038_ITERATE_FLUSH(1);
					ds->db_stencil_info |= S_02803C_ITERATE_FLUSH(1);
				}
			}

			if (!surf->has_stencil)
				/* Use all of the htile_buffer for depth if there's no stencil. */
				ds->db_stencil_info |= S_02803C_TILE_STENCIL_DISABLE(1);
			va = radv_buffer_get_va(iview->bo) + iview->image->offset +
				iview->image->htile_offset;
			ds->db_htile_data_base = va >> 8;
			ds->db_htile_surface = S_028ABC_FULL_CACHE(1) |
				S_028ABC_PIPE_ALIGNED(surf->u.gfx9.htile.pipe_aligned);

			if (device->physical_device->rad_info.chip_class == GFX9) {
				ds->db_htile_surface |= S_028ABC_RB_ALIGNED(surf->u.gfx9.htile.rb_aligned);
			}
		}
	} else {
		const struct legacy_surf_level *level_info = &surf->u.legacy.level[level];

		if (stencil_only)
			level_info = &surf->u.legacy.stencil_level[level];

		z_offs += surf->u.legacy.level[level].offset;
		s_offs += surf->u.legacy.stencil_level[level].offset;

		ds->db_depth_info = S_02803C_ADDR5_SWIZZLE_MASK(!radv_image_is_tc_compat_htile(iview->image));
		ds->db_z_info = S_028040_FORMAT(format) | S_028040_ZRANGE_PRECISION(1);
		ds->db_stencil_info = S_028044_FORMAT(stencil_format);

		if (iview->image->info.samples > 1)
			ds->db_z_info |= S_028040_NUM_SAMPLES(util_logbase2(iview->image->info.samples));

		if (device->physical_device->rad_info.chip_class >= GFX7) {
			struct radeon_info *info = &device->physical_device->rad_info;
			unsigned tiling_index = surf->u.legacy.tiling_index[level];
			unsigned stencil_index = surf->u.legacy.stencil_tiling_index[level];
			unsigned macro_index = surf->u.legacy.macro_tile_index;
			unsigned tile_mode = info->si_tile_mode_array[tiling_index];
			unsigned stencil_tile_mode = info->si_tile_mode_array[stencil_index];
			unsigned macro_mode = info->cik_macrotile_mode_array[macro_index];

			if (stencil_only)
				tile_mode = stencil_tile_mode;

			ds->db_depth_info |=
				S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
				S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
				S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
				S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
				S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
				S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
			ds->db_z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
			ds->db_stencil_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
		} else {
			unsigned tile_mode_index = si_tile_mode_index(&iview->image->planes[0], level, false);
			ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
			tile_mode_index = si_tile_mode_index(&iview->image->planes[0], level, true);
			ds->db_stencil_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
			if (stencil_only)
				ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
		}

		ds->db_depth_size = S_028058_PITCH_TILE_MAX((level_info->nblk_x / 8) - 1) |
			S_028058_HEIGHT_TILE_MAX((level_info->nblk_y / 8) - 1);
		ds->db_depth_slice = S_02805C_SLICE_TILE_MAX((level_info->nblk_x * level_info->nblk_y) / 64 - 1);

		if (radv_htile_enabled(iview->image, level)) {
			ds->db_z_info |= S_028040_TILE_SURFACE_ENABLE(1);

			if (!surf->has_stencil &&
			    !radv_image_is_tc_compat_htile(iview->image))
				/* Use all of the htile_buffer for depth if there's no stencil. */
				ds->db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(1);

			va = radv_buffer_get_va(iview->bo) + iview->image->offset +
				iview->image->htile_offset;
			ds->db_htile_data_base = va >> 8;
			ds->db_htile_surface = S_028ABC_FULL_CACHE(1);

			if (radv_image_is_tc_compat_htile(iview->image)) {
				unsigned max_zplanes =
					radv_calc_decompress_on_z_planes(device, iview);

				ds->db_htile_surface |= S_028ABC_TC_COMPATIBLE(1);
				ds->db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(max_zplanes);
			}
		}
	}

	ds->db_z_read_base = ds->db_z_write_base = z_offs >> 8;
	ds->db_stencil_read_base = ds->db_stencil_write_base = s_offs >> 8;
}

VkResult radv_CreateFramebuffer(
	VkDevice                                    _device,
	const VkFramebufferCreateInfo*              pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkFramebuffer*                              pFramebuffer)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_framebuffer *framebuffer;
	const VkFramebufferAttachmentsCreateInfoKHR *imageless_create_info =
		vk_find_struct_const(pCreateInfo->pNext,
			FRAMEBUFFER_ATTACHMENTS_CREATE_INFO_KHR);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

	size_t size = sizeof(*framebuffer);
	if (!imageless_create_info)
		size += sizeof(struct radv_image_view*) * pCreateInfo->attachmentCount;
	framebuffer = vk_alloc2(&device->alloc, pAllocator, size, 8,
				  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (framebuffer == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	framebuffer->attachment_count = pCreateInfo->attachmentCount;
	framebuffer->width = pCreateInfo->width;
	framebuffer->height = pCreateInfo->height;
	framebuffer->layers = pCreateInfo->layers;
	if (imageless_create_info) {
		for (unsigned i = 0; i < imageless_create_info->attachmentImageInfoCount; ++i) {
			const VkFramebufferAttachmentImageInfoKHR *attachment =
				imageless_create_info->pAttachmentImageInfos + i;
			framebuffer->width = MIN2(framebuffer->width, attachment->width);
			framebuffer->height = MIN2(framebuffer->height, attachment->height);
			framebuffer->layers = MIN2(framebuffer->layers, attachment->layerCount);
		}
	} else {
		for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
			VkImageView _iview = pCreateInfo->pAttachments[i];
			struct radv_image_view *iview = radv_image_view_from_handle(_iview);
			framebuffer->attachments[i] = iview;
			framebuffer->width = MIN2(framebuffer->width, iview->extent.width);
			framebuffer->height = MIN2(framebuffer->height, iview->extent.height);
			framebuffer->layers = MIN2(framebuffer->layers, radv_surface_max_layer_count(iview));
		}
	}

	*pFramebuffer = radv_framebuffer_to_handle(framebuffer);
	return VK_SUCCESS;
}

void radv_DestroyFramebuffer(
	VkDevice                                    _device,
	VkFramebuffer                               _fb,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_framebuffer, fb, _fb);

	if (!fb)
		return;
	vk_free2(&device->alloc, pAllocator, fb);
}

static unsigned radv_tex_wrap(VkSamplerAddressMode address_mode)
{
	switch (address_mode) {
	case VK_SAMPLER_ADDRESS_MODE_REPEAT:
		return V_008F30_SQ_TEX_WRAP;
	case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
		return V_008F30_SQ_TEX_MIRROR;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
		return V_008F30_SQ_TEX_CLAMP_BORDER;
	case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
	default:
		unreachable("illegal tex wrap mode");
		break;
	}
}

static unsigned
radv_tex_compare(VkCompareOp op)
{
	switch (op) {
	case VK_COMPARE_OP_NEVER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
	case VK_COMPARE_OP_LESS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
	case VK_COMPARE_OP_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
	case VK_COMPARE_OP_LESS_OR_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
	case VK_COMPARE_OP_GREATER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
	case VK_COMPARE_OP_NOT_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
	case VK_COMPARE_OP_GREATER_OR_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
	case VK_COMPARE_OP_ALWAYS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
	default:
		unreachable("illegal compare mode");
		break;
	}
}

static unsigned
radv_tex_filter(VkFilter filter, unsigned max_ansio)
{
	switch (filter) {
	case VK_FILTER_NEAREST:
		return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT :
			V_008F38_SQ_TEX_XY_FILTER_POINT);
	case VK_FILTER_LINEAR:
		return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR :
			V_008F38_SQ_TEX_XY_FILTER_BILINEAR);
	case VK_FILTER_CUBIC_IMG:
	default:
		fprintf(stderr, "illegal texture filter");
		return 0;
	}
}

static unsigned
radv_tex_mipfilter(VkSamplerMipmapMode mode)
{
	switch (mode) {
	case VK_SAMPLER_MIPMAP_MODE_NEAREST:
		return V_008F38_SQ_TEX_Z_FILTER_POINT;
	case VK_SAMPLER_MIPMAP_MODE_LINEAR:
		return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
	default:
		return V_008F38_SQ_TEX_Z_FILTER_NONE;
	}
}

static unsigned
radv_tex_bordercolor(VkBorderColor bcolor)
{
	switch (bcolor) {
	case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
	case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
		return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;
	case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
	case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
		return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK;
	case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
	case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
		return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;
	default:
		break;
	}
	return 0;
}

static unsigned
radv_tex_aniso_filter(unsigned filter)
{
	if (filter < 2)
		return 0;
	if (filter < 4)
		return 1;
	if (filter < 8)
		return 2;
	if (filter < 16)
		return 3;
	return 4;
}

static unsigned
radv_tex_filter_mode(VkSamplerReductionModeEXT mode)
{
	switch (mode) {
	case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT:
		return V_008F30_SQ_IMG_FILTER_MODE_BLEND;
	case VK_SAMPLER_REDUCTION_MODE_MIN_EXT:
		return V_008F30_SQ_IMG_FILTER_MODE_MIN;
	case VK_SAMPLER_REDUCTION_MODE_MAX_EXT:
		return V_008F30_SQ_IMG_FILTER_MODE_MAX;
	default:
		break;
	}
	return 0;
}

static uint32_t
radv_get_max_anisotropy(struct radv_device *device,
			const VkSamplerCreateInfo *pCreateInfo)
{
	if (device->force_aniso >= 0)
		return device->force_aniso;

	if (pCreateInfo->anisotropyEnable &&
	    pCreateInfo->maxAnisotropy > 1.0f)
		return (uint32_t)pCreateInfo->maxAnisotropy;

	return 0;
}

static void
radv_init_sampler(struct radv_device *device,
		  struct radv_sampler *sampler,
		  const VkSamplerCreateInfo *pCreateInfo)
{
	uint32_t max_aniso = radv_get_max_anisotropy(device, pCreateInfo);
	uint32_t max_aniso_ratio = radv_tex_aniso_filter(max_aniso);
	bool compat_mode = device->physical_device->rad_info.chip_class == GFX8 ||
			   device->physical_device->rad_info.chip_class == GFX9;
	unsigned filter_mode = V_008F30_SQ_IMG_FILTER_MODE_BLEND;

	const struct VkSamplerReductionModeCreateInfoEXT *sampler_reduction =
		vk_find_struct_const(pCreateInfo->pNext,
				     SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT);
	if (sampler_reduction)
		filter_mode = radv_tex_filter_mode(sampler_reduction->reductionMode);

	sampler->state[0] = (S_008F30_CLAMP_X(radv_tex_wrap(pCreateInfo->addressModeU)) |
			     S_008F30_CLAMP_Y(radv_tex_wrap(pCreateInfo->addressModeV)) |
			     S_008F30_CLAMP_Z(radv_tex_wrap(pCreateInfo->addressModeW)) |
			     S_008F30_MAX_ANISO_RATIO(max_aniso_ratio) |
			     S_008F30_DEPTH_COMPARE_FUNC(radv_tex_compare(pCreateInfo->compareOp)) |
			     S_008F30_FORCE_UNNORMALIZED(pCreateInfo->unnormalizedCoordinates ? 1 : 0) |
			     S_008F30_ANISO_THRESHOLD(max_aniso_ratio >> 1) |
			     S_008F30_ANISO_BIAS(max_aniso_ratio) |
			     S_008F30_DISABLE_CUBE_WRAP(0) |
			     S_008F30_COMPAT_MODE(compat_mode) |
			     S_008F30_FILTER_MODE(filter_mode));
	sampler->state[1] = (S_008F34_MIN_LOD(S_FIXED(CLAMP(pCreateInfo->minLod, 0, 15), 8)) |
			     S_008F34_MAX_LOD(S_FIXED(CLAMP(pCreateInfo->maxLod, 0, 15), 8)) |
			     S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
	sampler->state[2] = (S_008F38_LOD_BIAS(S_FIXED(CLAMP(pCreateInfo->mipLodBias, -16, 16), 8)) |
			     S_008F38_XY_MAG_FILTER(radv_tex_filter(pCreateInfo->magFilter, max_aniso)) |
			     S_008F38_XY_MIN_FILTER(radv_tex_filter(pCreateInfo->minFilter, max_aniso)) |
			     S_008F38_MIP_FILTER(radv_tex_mipfilter(pCreateInfo->mipmapMode)) |
			     S_008F38_MIP_POINT_PRECLAMP(0));
	sampler->state[3] = (S_008F3C_BORDER_COLOR_PTR(0) |
			     S_008F3C_BORDER_COLOR_TYPE(radv_tex_bordercolor(pCreateInfo->borderColor)));

	if (device->physical_device->rad_info.chip_class >= GFX10) {
		sampler->state[2] |= S_008F38_ANISO_OVERRIDE_GFX10(1);
	} else {
		sampler->state[2] |=
			S_008F38_DISABLE_LSB_CEIL(device->physical_device->rad_info.chip_class <= GFX8) |
			S_008F38_FILTER_PREC_FIX(1) |
			S_008F38_ANISO_OVERRIDE_GFX6(device->physical_device->rad_info.chip_class >= GFX8);
	}
}

VkResult radv_CreateSampler(
	VkDevice                                    _device,
	const VkSamplerCreateInfo*                  pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkSampler*                                  pSampler)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_sampler *sampler;

	const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
		vk_find_struct_const(pCreateInfo->pNext,
				     SAMPLER_YCBCR_CONVERSION_INFO);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

	sampler = vk_alloc2(&device->alloc, pAllocator, sizeof(*sampler), 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!sampler)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	radv_init_sampler(device, sampler, pCreateInfo);

	sampler->ycbcr_sampler = ycbcr_conversion ? radv_sampler_ycbcr_conversion_from_handle(ycbcr_conversion->conversion): NULL;
	*pSampler = radv_sampler_to_handle(sampler);

	return VK_SUCCESS;
}

void radv_DestroySampler(
	VkDevice                                    _device,
	VkSampler                                   _sampler,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_sampler, sampler, _sampler);

	if (!sampler)
		return;
	vk_free2(&device->alloc, pAllocator, sampler);
}

/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion);

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion)
{
	/* For the full details on loader interface versioning, see
	* <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
	* What follows is a condensed summary, to help you navigate the large and
	* confusing official doc.
	*
	*   - Loader interface v0 is incompatible with later versions. We don't
	*     support it.
	*
	*   - In loader interface v1:
	*       - The first ICD entrypoint called by the loader is
	*         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
	*         entrypoint.
	*       - The ICD must statically expose no other Vulkan symbol unless it is
	*         linked with -Bsymbolic.
	*       - Each dispatchable Vulkan handle created by the ICD must be
	*         a pointer to a struct whose first member is VK_LOADER_DATA. The
	*         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
	*       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
	*         vkDestroySurfaceKHR(). The ICD must be capable of working with
	*         such loader-managed surfaces.
	*
	*    - Loader interface v2 differs from v1 in:
	*       - The first ICD entrypoint called by the loader is
	*         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
	*         statically expose this entrypoint.
	*
	*    - Loader interface v3 differs from v2 in:
	*        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
	*          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
	*          because the loader no longer does so.
	*/
	*pSupportedVersion = MIN2(*pSupportedVersion, 4u);
	return VK_SUCCESS;
}

VkResult radv_GetMemoryFdKHR(VkDevice _device,
			     const VkMemoryGetFdInfoKHR *pGetFdInfo,
			     int *pFD)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, memory, pGetFdInfo->memory);

	assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

	/* At the moment, we support only the below handle types. */
	assert(pGetFdInfo->handleType ==
	       VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
	       pGetFdInfo->handleType ==
	       VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

	bool ret = radv_get_memory_fd(device, memory, pFD);
	if (ret == false)
		return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
	return VK_SUCCESS;
}

VkResult radv_GetMemoryFdPropertiesKHR(VkDevice _device,
				       VkExternalMemoryHandleTypeFlagBits handleType,
				       int fd,
				       VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pMemoryFdProperties->memoryTypeBits = (1 << RADV_MEM_TYPE_COUNT) - 1;
      return VK_SUCCESS;

   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

static VkResult radv_import_opaque_fd(struct radv_device *device,
                                      int fd,
                                      uint32_t *syncobj)
{
	uint32_t syncobj_handle = 0;
	int ret = device->ws->import_syncobj(device->ws, fd, &syncobj_handle);
	if (ret != 0)
		return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

	if (*syncobj)
		device->ws->destroy_syncobj(device->ws, *syncobj);

	*syncobj = syncobj_handle;
	close(fd);

	return VK_SUCCESS;
}

static VkResult radv_import_sync_fd(struct radv_device *device,
                                    int fd,
                                    uint32_t *syncobj)
{
	/* If we create a syncobj we do it locally so that if we have an error, we don't
	 * leave a syncobj in an undetermined state in the fence. */
	uint32_t syncobj_handle =  *syncobj;
	if (!syncobj_handle) {
		int ret = device->ws->create_syncobj(device->ws, &syncobj_handle);
		if (ret) {
			return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
		}
	}

	if (fd == -1) {
		device->ws->signal_syncobj(device->ws, syncobj_handle);
	} else {
		int ret = device->ws->import_syncobj_from_sync_file(device->ws, syncobj_handle, fd);
	if (ret != 0)
		return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
	}

	*syncobj = syncobj_handle;
	if (fd != -1)
		close(fd);

	return VK_SUCCESS;
}

VkResult radv_ImportSemaphoreFdKHR(VkDevice _device,
				   const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_semaphore, sem, pImportSemaphoreFdInfo->semaphore);
	VkResult result;
	struct radv_semaphore_part *dst = NULL;

	if (pImportSemaphoreFdInfo->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT) {
		dst = &sem->temporary;
	} else {
		dst = &sem->permanent;
	}

	uint32_t syncobj = dst->kind == RADV_SEMAPHORE_SYNCOBJ ? dst->syncobj : 0;

	switch(pImportSemaphoreFdInfo->handleType) {
		case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
			result = radv_import_opaque_fd(device, pImportSemaphoreFdInfo->fd, &syncobj);
			break;
		case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
			result = radv_import_sync_fd(device, pImportSemaphoreFdInfo->fd, &syncobj);
			break;
		default:
			unreachable("Unhandled semaphore handle type");
	}

	if (result == VK_SUCCESS) {
		dst->syncobj = syncobj;
		dst->kind = RADV_SEMAPHORE_SYNCOBJ;
	}

	return result;
}

VkResult radv_GetSemaphoreFdKHR(VkDevice _device,
				const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
				int *pFd)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_semaphore, sem, pGetFdInfo->semaphore);
	int ret;
	uint32_t syncobj_handle;

	if (sem->temporary.kind != RADV_SEMAPHORE_NONE) {
		assert(sem->temporary.kind == RADV_SEMAPHORE_SYNCOBJ);
		syncobj_handle = sem->temporary.syncobj;
	} else {
		assert(sem->permanent.kind == RADV_SEMAPHORE_SYNCOBJ);
		syncobj_handle = sem->permanent.syncobj;
	}

	switch(pGetFdInfo->handleType) {
	case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
		ret = device->ws->export_syncobj(device->ws, syncobj_handle, pFd);
		break;
	case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
		ret = device->ws->export_syncobj_to_sync_file(device->ws, syncobj_handle, pFd);
		if (!ret) {
			if (sem->temporary.kind != RADV_SEMAPHORE_NONE) {
				radv_destroy_semaphore_part(device, &sem->temporary);
			} else {
				device->ws->reset_syncobj(device->ws, syncobj_handle);
			}
		}
		break;
	default:
		unreachable("Unhandled semaphore handle type");
	}

	if (ret)
		return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
	return VK_SUCCESS;
}

void radv_GetPhysicalDeviceExternalSemaphoreProperties(
	VkPhysicalDevice                            physicalDevice,
	const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
	VkExternalSemaphoreProperties               *pExternalSemaphoreProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	VkSemaphoreTypeKHR type = radv_get_semaphore_type(pExternalSemaphoreInfo->pNext, NULL);
	
	if (type == VK_SEMAPHORE_TYPE_TIMELINE_KHR) {
		pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
		pExternalSemaphoreProperties->compatibleHandleTypes = 0;
		pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;

	/* Require has_syncobj_wait_for_submit for the syncobj signal ioctl introduced at virtually the same time */
	} else if (pdevice->rad_info.has_syncobj_wait_for_submit &&
	           (pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT || 
	            pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT)) {
		pExternalSemaphoreProperties->exportFromImportedHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
		pExternalSemaphoreProperties->compatibleHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
		pExternalSemaphoreProperties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
			VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
	} else if (pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) {
		pExternalSemaphoreProperties->exportFromImportedHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
		pExternalSemaphoreProperties->compatibleHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
		pExternalSemaphoreProperties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
			VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
	} else {
		pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
		pExternalSemaphoreProperties->compatibleHandleTypes = 0;
		pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
	}
}

VkResult radv_ImportFenceFdKHR(VkDevice _device,
				   const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, pImportFenceFdInfo->fence);
	uint32_t *syncobj_dst = NULL;


	if (pImportFenceFdInfo->flags & VK_FENCE_IMPORT_TEMPORARY_BIT) {
		syncobj_dst = &fence->temp_syncobj;
	} else {
		syncobj_dst = &fence->syncobj;
	}

	switch(pImportFenceFdInfo->handleType) {
		case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
			return radv_import_opaque_fd(device, pImportFenceFdInfo->fd, syncobj_dst);
		case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT:
			return radv_import_sync_fd(device, pImportFenceFdInfo->fd, syncobj_dst);
		default:
			unreachable("Unhandled fence handle type");
	}
}

VkResult radv_GetFenceFdKHR(VkDevice _device,
				const VkFenceGetFdInfoKHR *pGetFdInfo,
				int *pFd)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, pGetFdInfo->fence);
	int ret;
	uint32_t syncobj_handle;

	if (fence->temp_syncobj)
		syncobj_handle = fence->temp_syncobj;
	else
		syncobj_handle = fence->syncobj;

	switch(pGetFdInfo->handleType) {
	case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
		ret = device->ws->export_syncobj(device->ws, syncobj_handle, pFd);
		break;
	case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT:
		ret = device->ws->export_syncobj_to_sync_file(device->ws, syncobj_handle, pFd);
		if (!ret) {
			if (fence->temp_syncobj) {
				close (fence->temp_syncobj);
				fence->temp_syncobj = 0;
			} else {
				device->ws->reset_syncobj(device->ws, syncobj_handle);
			}
		}
		break;
	default:
		unreachable("Unhandled fence handle type");
	}

	if (ret)
		return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
	return VK_SUCCESS;
}

void radv_GetPhysicalDeviceExternalFenceProperties(
	VkPhysicalDevice                            physicalDevice,
	const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
	VkExternalFenceProperties               *pExternalFenceProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);

	if (pdevice->rad_info.has_syncobj_wait_for_submit &&
	    (pExternalFenceInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT || 
	     pExternalFenceInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT)) {
		pExternalFenceProperties->exportFromImportedHandleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
		pExternalFenceProperties->compatibleHandleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
		pExternalFenceProperties->externalFenceFeatures = VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT |
			VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
	} else {
		pExternalFenceProperties->exportFromImportedHandleTypes = 0;
		pExternalFenceProperties->compatibleHandleTypes = 0;
		pExternalFenceProperties->externalFenceFeatures = 0;
	}
}

VkResult
radv_CreateDebugReportCallbackEXT(VkInstance _instance,
                                 const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDebugReportCallbackEXT* pCallback)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	return vk_create_debug_report_callback(&instance->debug_report_callbacks,
	                                       pCreateInfo, pAllocator, &instance->alloc,
	                                       pCallback);
}

void
radv_DestroyDebugReportCallbackEXT(VkInstance _instance,
                                  VkDebugReportCallbackEXT _callback,
                                  const VkAllocationCallbacks* pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	vk_destroy_debug_report_callback(&instance->debug_report_callbacks,
	                                 _callback, pAllocator, &instance->alloc);
}

void
radv_DebugReportMessageEXT(VkInstance _instance,
                          VkDebugReportFlagsEXT flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t object,
                          size_t location,
                          int32_t messageCode,
                          const char* pLayerPrefix,
                          const char* pMessage)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	vk_debug_report(&instance->debug_report_callbacks, flags, objectType,
	                object, location, messageCode, pLayerPrefix, pMessage);
}

void
radv_GetDeviceGroupPeerMemoryFeatures(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures)
{
	assert(localDeviceIndex == remoteDeviceIndex);

	*pPeerMemoryFeatures = VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT |
	                       VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
	                       VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT |
	                       VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
}

static const VkTimeDomainEXT radv_time_domains[] = {
	VK_TIME_DOMAIN_DEVICE_EXT,
	VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
	VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT,
};

VkResult radv_GetPhysicalDeviceCalibrateableTimeDomainsEXT(
	VkPhysicalDevice                             physicalDevice,
	uint32_t                                     *pTimeDomainCount,
	VkTimeDomainEXT                              *pTimeDomains)
{
	int d;
	VK_OUTARRAY_MAKE(out, pTimeDomains, pTimeDomainCount);

	for (d = 0; d < ARRAY_SIZE(radv_time_domains); d++) {
		vk_outarray_append(&out, i) {
			*i = radv_time_domains[d];
		}
	}

	return vk_outarray_status(&out);
}

static uint64_t
radv_clock_gettime(clockid_t clock_id)
{
	struct timespec current;
	int ret;

	ret = clock_gettime(clock_id, &current);
	if (ret < 0 && clock_id == CLOCK_MONOTONIC_RAW)
		ret = clock_gettime(CLOCK_MONOTONIC, &current);
	if (ret < 0)
		return 0;

	return (uint64_t) current.tv_sec * 1000000000ULL + current.tv_nsec;
}

VkResult radv_GetCalibratedTimestampsEXT(
	VkDevice                                     _device,
	uint32_t                                     timestampCount,
	const VkCalibratedTimestampInfoEXT           *pTimestampInfos,
	uint64_t                                     *pTimestamps,
	uint64_t                                     *pMaxDeviation)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	uint32_t clock_crystal_freq = device->physical_device->rad_info.clock_crystal_freq;
	int d;
	uint64_t begin, end;
        uint64_t max_clock_period = 0;

	begin = radv_clock_gettime(CLOCK_MONOTONIC_RAW);

	for (d = 0; d < timestampCount; d++) {
		switch (pTimestampInfos[d].timeDomain) {
		case VK_TIME_DOMAIN_DEVICE_EXT:
			pTimestamps[d] = device->ws->query_value(device->ws,
								 RADEON_TIMESTAMP);
                        uint64_t device_period = DIV_ROUND_UP(1000000, clock_crystal_freq);
                        max_clock_period = MAX2(max_clock_period, device_period);
			break;
		case VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT:
			pTimestamps[d] = radv_clock_gettime(CLOCK_MONOTONIC);
                        max_clock_period = MAX2(max_clock_period, 1);
			break;

		case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT:
			pTimestamps[d] = begin;
			break;
		default:
			pTimestamps[d] = 0;
			break;
		}
	}

	end = radv_clock_gettime(CLOCK_MONOTONIC_RAW);

        /*
         * The maximum deviation is the sum of the interval over which we
         * perform the sampling and the maximum period of any sampled
         * clock. That's because the maximum skew between any two sampled
         * clock edges is when the sampled clock with the largest period is
         * sampled at the end of that period but right at the beginning of the
         * sampling interval and some other clock is sampled right at the
         * begining of its sampling period and right at the end of the
         * sampling interval. Let's assume the GPU has the longest clock
         * period and that the application is sampling GPU and monotonic:
         *
         *                               s                 e
         *			 w x y z 0 1 2 3 4 5 6 7 8 9 a b c d e f
         *	Raw              -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
         *
         *                               g
         *		  0         1         2         3
         *	GPU       -----_____-----_____-----_____-----_____
         *
         *                                                m
         *					    x y z 0 1 2 3 4 5 6 7 8 9 a b c
         *	Monotonic                           -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
         *
         *	Interval                     <----------------->
         *	Deviation           <-------------------------->
         *
         *		s  = read(raw)       2
         *		g  = read(GPU)       1
         *		m  = read(monotonic) 2
         *		e  = read(raw)       b
         *
         * We round the sample interval up by one tick to cover sampling error
         * in the interval clock
         */

        uint64_t sample_interval = end - begin + 1;

        *pMaxDeviation = sample_interval + max_clock_period;

	return VK_SUCCESS;
}

void radv_GetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties)
{
	if (samples & (VK_SAMPLE_COUNT_2_BIT |
		       VK_SAMPLE_COUNT_4_BIT |
		       VK_SAMPLE_COUNT_8_BIT)) {
		pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){ 2, 2 };
	} else {
		pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){ 0, 0 };
	}
}
