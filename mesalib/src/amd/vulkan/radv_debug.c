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

#include <stdlib.h>
#include <stdio.h>
#include <sys/utsname.h>

#include "sid.h"
#include "gfx9d.h"
#include "ac_debug.h"
#include "radv_debug.h"
#include "radv_shader.h"

#define TRACE_BO_SIZE 4096

#define COLOR_RESET	"\033[0m"
#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[1;32m"
#define COLOR_YELLOW	"\033[1;33m"
#define COLOR_CYAN	"\033[1;36m"

/* Trace BO layout (offsets are 4 bytes):
 *
 * [0]: primary trace ID
 * [1]: secondary trace ID
 * [2-3]: 64-bit GFX pipeline pointer
 * [4-5]: 64-bit COMPUTE pipeline pointer
 * [6-7]: 64-bit descriptor set #0 pointer
 * ...
 * [68-69]: 64-bit descriptor set #31 pointer
 */

bool
radv_init_trace(struct radv_device *device)
{
	struct radeon_winsys *ws = device->ws;

	device->trace_bo = ws->buffer_create(ws, TRACE_BO_SIZE, 8,
					     RADEON_DOMAIN_VRAM,
					     RADEON_FLAG_CPU_ACCESS|
					     RADEON_FLAG_NO_INTERPROCESS_SHARING);
	if (!device->trace_bo)
		return false;

	device->trace_id_ptr = ws->buffer_map(device->trace_bo);
	if (!device->trace_id_ptr)
		return false;

	memset(device->trace_id_ptr, 0, TRACE_BO_SIZE);

	ac_vm_fault_occured(device->physical_device->rad_info.chip_class,
			    &device->dmesg_timestamp, NULL);

	return true;
}

static void
radv_dump_trace(struct radv_device *device, struct radeon_winsys_cs *cs)
{
	const char *filename = getenv("RADV_TRACE_FILE");
	FILE *f = fopen(filename, "w");

	if (!f) {
		fprintf(stderr, "Failed to write trace dump to %s\n", filename);
		return;
	}

	fprintf(f, "Trace ID: %x\n", *device->trace_id_ptr);
	device->ws->cs_dump(cs, f, (const int*)device->trace_id_ptr, 2);
	fclose(f);
}

static void
radv_dump_mmapped_reg(struct radv_device *device, FILE *f, unsigned offset)
{
	struct radeon_winsys *ws = device->ws;
	uint32_t value;

	if (ws->read_registers(ws, offset, 1, &value))
		ac_dump_reg(f, device->physical_device->rad_info.chip_class,
			    offset, value, ~0);
}

static void
radv_dump_debug_registers(struct radv_device *device, FILE *f)
{
	struct radeon_info *info = &device->physical_device->rad_info;

	if (info->drm_major == 2 && info->drm_minor < 42)
		return; /* no radeon support */

	fprintf(f, "Memory-mapped registers:\n");
	radv_dump_mmapped_reg(device, f, R_008010_GRBM_STATUS);

	/* No other registers can be read on DRM < 3.1.0. */
	if (info->drm_major < 3 || info->drm_minor < 1) {
		fprintf(f, "\n");
		return;
	}

	radv_dump_mmapped_reg(device, f, R_008008_GRBM_STATUS2);
	radv_dump_mmapped_reg(device, f, R_008014_GRBM_STATUS_SE0);
	radv_dump_mmapped_reg(device, f, R_008018_GRBM_STATUS_SE1);
	radv_dump_mmapped_reg(device, f, R_008038_GRBM_STATUS_SE2);
	radv_dump_mmapped_reg(device, f, R_00803C_GRBM_STATUS_SE3);
	radv_dump_mmapped_reg(device, f, R_00D034_SDMA0_STATUS_REG);
	radv_dump_mmapped_reg(device, f, R_00D834_SDMA1_STATUS_REG);
	if (info->chip_class <= VI) {
		radv_dump_mmapped_reg(device, f, R_000E50_SRBM_STATUS);
		radv_dump_mmapped_reg(device, f, R_000E4C_SRBM_STATUS2);
		radv_dump_mmapped_reg(device, f, R_000E54_SRBM_STATUS3);
	}
	radv_dump_mmapped_reg(device, f, R_008680_CP_STAT);
	radv_dump_mmapped_reg(device, f, R_008674_CP_STALLED_STAT1);
	radv_dump_mmapped_reg(device, f, R_008678_CP_STALLED_STAT2);
	radv_dump_mmapped_reg(device, f, R_008670_CP_STALLED_STAT3);
	radv_dump_mmapped_reg(device, f, R_008210_CP_CPC_STATUS);
	radv_dump_mmapped_reg(device, f, R_008214_CP_CPC_BUSY_STAT);
	radv_dump_mmapped_reg(device, f, R_008218_CP_CPC_STALLED_STAT1);
	radv_dump_mmapped_reg(device, f, R_00821C_CP_CPF_STATUS);
	radv_dump_mmapped_reg(device, f, R_008220_CP_CPF_BUSY_STAT);
	radv_dump_mmapped_reg(device, f, R_008224_CP_CPF_STALLED_STAT1);
	fprintf(f, "\n");
}

static const char *
radv_get_descriptor_name(enum VkDescriptorType type)
{
	switch (type) {
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return "SAMPLER";
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return "COMBINED_IMAGE_SAMPLER";
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		return "SAMPLED_IMAGE";
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return "STORAGE_IMAGE";
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		return "UNIFORM_TEXEL_BUFFER";
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		return "STORAGE_TEXEL_BUFFER";
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		return "UNIFORM_BUFFER";
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		return "STORAGE_BUFFER";
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		return "UNIFORM_BUFFER_DYNAMIC";
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		return "STORAGE_BUFFER_DYNAMIC";
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		return "INPUT_ATTACHMENT";
	default:
		return "UNKNOWN";
	}
}

static void
radv_dump_buffer_descriptor(enum chip_class chip_class, const uint32_t *desc,
			    FILE *f)
{
	fprintf(f, COLOR_CYAN "    Buffer:" COLOR_RESET "\n");
	for (unsigned j = 0; j < 4; j++)
		ac_dump_reg(f, chip_class, R_008F00_SQ_BUF_RSRC_WORD0 + j * 4,
			    desc[j], 0xffffffff);
}

static void
radv_dump_image_descriptor(enum chip_class chip_class, const uint32_t *desc,
			   FILE *f)
{
	fprintf(f, COLOR_CYAN "    Image:" COLOR_RESET "\n");
	for (unsigned j = 0; j < 8; j++)
		ac_dump_reg(f, chip_class, R_008F10_SQ_IMG_RSRC_WORD0 + j * 4,
			    desc[j], 0xffffffff);

	fprintf(f, COLOR_CYAN "    FMASK:" COLOR_RESET "\n");
	for (unsigned j = 0; j < 8; j++)
		ac_dump_reg(f, chip_class, R_008F10_SQ_IMG_RSRC_WORD0 + j * 4,
			    desc[8 + j], 0xffffffff);
}

static void
radv_dump_sampler_descriptor(enum chip_class chip_class, const uint32_t *desc,
			     FILE *f)
{
	fprintf(f, COLOR_CYAN "    Sampler state:" COLOR_RESET "\n");
	for (unsigned j = 0; j < 4; j++) {
		ac_dump_reg(f, chip_class, R_008F30_SQ_IMG_SAMP_WORD0 + j * 4,
			    desc[j], 0xffffffff);
	}
}

static void
radv_dump_combined_image_sampler_descriptor(enum chip_class chip_class,
					    const uint32_t *desc, FILE *f)
{
	radv_dump_image_descriptor(chip_class, desc, f);
	radv_dump_sampler_descriptor(chip_class, desc + 16, f);
}

static void
radv_dump_descriptor_set(enum chip_class chip_class,
			 struct radv_descriptor_set *set, unsigned id, FILE *f)
{
	const struct radv_descriptor_set_layout *layout;
	int i;

	if (!set)
		return;
	layout = set->layout;

	fprintf(f, "** descriptor set (%d) **\n", id);
	fprintf(f, "va: 0x%"PRIx64"\n", set->va);
	fprintf(f, "size: %d\n", set->size);
	fprintf(f, "mapped_ptr:\n");

	for (i = 0; i < set->size / 4; i++) {
		fprintf(f, "\t[0x%x] = 0x%08x\n", i, set->mapped_ptr[i]);
	}
	fprintf(f, "\n");

	fprintf(f, "\t*** layout ***\n");
	fprintf(f, "\tbinding_count: %d\n", layout->binding_count);
	fprintf(f, "\tsize: %d\n", layout->size);
	fprintf(f, "\tshader_stages: %x\n", layout->shader_stages);
	fprintf(f, "\tdynamic_shader_stages: %x\n",
		layout->dynamic_shader_stages);
	fprintf(f, "\tbuffer_count: %d\n", layout->buffer_count);
	fprintf(f, "\tdynamic_offset_count: %d\n",
		layout->dynamic_offset_count);
	fprintf(f, "\n");

	for (i = 0; i < set->layout->binding_count; i++) {
		uint32_t *desc =
			set->mapped_ptr + layout->binding[i].offset / 4;

		fprintf(f, "\t\t**** binding layout (%d) ****\n", i);
		fprintf(f, "\t\ttype: %s\n",
			radv_get_descriptor_name(layout->binding[i].type));
		fprintf(f, "\t\tarray_size: %d\n",
			layout->binding[i].array_size);
		fprintf(f, "\t\toffset: %d\n",
			layout->binding[i].offset);
		fprintf(f, "\t\tbuffer_offset: %d\n",
			layout->binding[i].buffer_offset);
		fprintf(f, "\t\tdynamic_offset_offset: %d\n",
			layout->binding[i].dynamic_offset_offset);
		fprintf(f, "\t\tdynamic_offset_count: %d\n",
			layout->binding[i].dynamic_offset_count);
		fprintf(f, "\t\tsize: %d\n",
			layout->binding[i].size);
		fprintf(f, "\t\timmutable_samplers_offset: %d\n",
			layout->binding[i].immutable_samplers_offset);
		fprintf(f, "\t\timmutable_samplers_equal: %d\n",
			layout->binding[i].immutable_samplers_equal);
		fprintf(f, "\n");

		switch (layout->binding[i].type) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			radv_dump_buffer_descriptor(chip_class, desc, f);
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			radv_dump_image_descriptor(chip_class, desc, f);
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			radv_dump_combined_image_sampler_descriptor(chip_class, desc, f);
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			radv_dump_sampler_descriptor(chip_class, desc, f);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			/* todo */
			break;
		default:
			assert(!"unknown descriptor type");
			break;
		}
		fprintf(f, "\n");
	}
	fprintf(f, "\n\n");
}

static void
radv_dump_descriptors(struct radv_pipeline *pipeline, FILE *f)
{
	struct radv_device *device = pipeline->device;
	enum chip_class chip_class = device->physical_device->rad_info.chip_class;
	uint64_t *ptr = (uint64_t *)device->trace_id_ptr;
	int i;

	fprintf(f, "List of descriptors:\n");
	for (i = 0; i < MAX_SETS; i++) {
		struct radv_descriptor_set *set =
			(struct radv_descriptor_set *)ptr[i + 3];

		radv_dump_descriptor_set(chip_class, set, i, f);
	}
}

struct radv_shader_inst {
	char text[160];  /* one disasm line */
	unsigned offset; /* instruction offset */
	unsigned size;   /* instruction size = 4 or 8 */
};

/* Split a disassembly string into lines and add them to the array pointed
 * to by "instructions". */
static void si_add_split_disasm(const char *disasm,
				uint64_t start_addr,
				unsigned *num,
				struct radv_shader_inst *instructions)
{
	struct radv_shader_inst *last_inst = *num ? &instructions[*num - 1] : NULL;
	char *next;

	while ((next = strchr(disasm, '\n'))) {
		struct radv_shader_inst *inst = &instructions[*num];
		unsigned len = next - disasm;

		assert(len < ARRAY_SIZE(inst->text));
		memcpy(inst->text, disasm, len);
		inst->text[len] = 0;
		inst->offset = last_inst ? last_inst->offset + last_inst->size : 0;

		const char *semicolon = strchr(disasm, ';');
		assert(semicolon);
		/* More than 16 chars after ";" means the instruction is 8 bytes long. */
		inst->size = next - semicolon > 16 ? 8 : 4;

		snprintf(inst->text + len, ARRAY_SIZE(inst->text) - len,
			" [PC=0x%"PRIx64", off=%u, size=%u]",
			start_addr + inst->offset, inst->offset, inst->size);

		last_inst = inst;
		(*num)++;
		disasm = next + 1;
	}
}

static void
radv_dump_annotated_shader(struct radv_pipeline *pipeline,
			   struct radv_shader_variant *shader,
			   gl_shader_stage stage,
			   struct ac_wave_info *waves, unsigned num_waves,
			   FILE *f)
{
	uint64_t start_addr, end_addr;
	unsigned i;

	if (!shader)
		return;

	start_addr = radv_buffer_get_va(shader->bo) + shader->bo_offset;
	end_addr = start_addr + shader->code_size;

	/* See if any wave executes the shader. */
	for (i = 0; i < num_waves; i++) {
		if (start_addr <= waves[i].pc && waves[i].pc <= end_addr)
			break;
	}

	if (i == num_waves)
		return; /* the shader is not being executed */

	/* Remember the first found wave. The waves are sorted according to PC. */
	waves = &waves[i];
	num_waves -= i;

	/* Get the list of instructions.
	 * Buffer size / 4 is the upper bound of the instruction count.
	 */
	unsigned num_inst = 0;
	struct radv_shader_inst *instructions =
		calloc(shader->code_size / 4, sizeof(struct radv_shader_inst));

	si_add_split_disasm(shader->disasm_string,
			    start_addr, &num_inst, instructions);

	fprintf(f, COLOR_YELLOW "%s - annotated disassembly:" COLOR_RESET "\n",
		radv_get_shader_name(shader, stage));

	/* Print instructions with annotations. */
	for (i = 0; i < num_inst; i++) {
		struct radv_shader_inst *inst = &instructions[i];

		fprintf(f, "%s\n", inst->text);

		/* Print which waves execute the instruction right now. */
		while (num_waves && start_addr + inst->offset == waves->pc) {
			fprintf(f,
				"          " COLOR_GREEN "^ SE%u SH%u CU%u "
				"SIMD%u WAVE%u  EXEC=%016"PRIx64 "  ",
				waves->se, waves->sh, waves->cu, waves->simd,
				waves->wave, waves->exec);

			if (inst->size == 4) {
				fprintf(f, "INST32=%08X" COLOR_RESET "\n",
					waves->inst_dw0);
			} else {
				fprintf(f, "INST64=%08X %08X" COLOR_RESET "\n",
					waves->inst_dw0, waves->inst_dw1);
			}

			waves->matched = true;
			waves = &waves[1];
			num_waves--;
		}
	}

	fprintf(f, "\n\n");
	free(instructions);
}

static void
radv_dump_annotated_shaders(struct radv_pipeline *pipeline,
			    struct radv_shader_variant *compute_shader,
			    FILE *f)
{
	struct ac_wave_info waves[AC_MAX_WAVES_PER_CHIP];
	unsigned num_waves = ac_get_wave_info(waves);
	unsigned mask;

	fprintf(f, COLOR_CYAN "The number of active waves = %u" COLOR_RESET
		"\n\n", num_waves);

	/* Dump annotated active graphics shaders. */
	mask = pipeline->active_stages;
	while (mask) {
		int stage = u_bit_scan(&mask);

		radv_dump_annotated_shader(pipeline, pipeline->shaders[stage],
					   stage, waves, num_waves, f);
	}

	radv_dump_annotated_shader(pipeline, compute_shader,
				   MESA_SHADER_COMPUTE, waves, num_waves, f);

	/* Print waves executing shaders that are not currently bound. */
	unsigned i;
	bool found = false;
	for (i = 0; i < num_waves; i++) {
		if (waves[i].matched)
			continue;

		if (!found) {
			fprintf(f, COLOR_CYAN
				"Waves not executing currently-bound shaders:"
				COLOR_RESET "\n");
			found = true;
		}
		fprintf(f, "    SE%u SH%u CU%u SIMD%u WAVE%u  EXEC=%016"PRIx64
			"  INST=%08X %08X  PC=%"PRIx64"\n",
			waves[i].se, waves[i].sh, waves[i].cu, waves[i].simd,
			waves[i].wave, waves[i].exec, waves[i].inst_dw0,
			waves[i].inst_dw1, waves[i].pc);
	}
	if (found)
		fprintf(f, "\n\n");
}

static void
radv_dump_shader(struct radv_pipeline *pipeline,
		 struct radv_shader_variant *shader, gl_shader_stage stage,
		 FILE *f)
{
	if (!shader)
		return;

	fprintf(f, "%s:\n\n", radv_get_shader_name(shader, stage));

	if (shader->spirv) {
		fprintf(f, "SPIRV:\n");
		radv_print_spirv(shader->spirv, shader->spirv_size, f);
	}

	if (shader->nir) {
		fprintf(f, "NIR:\n");
		nir_print_shader(shader->nir, f);
	}

	fprintf(f, "LLVM IR:\n%s\n", shader->llvm_ir_string);
	fprintf(f, "DISASM:\n%s\n", shader->disasm_string);

	radv_shader_dump_stats(pipeline->device, shader, stage, f);
}

static void
radv_dump_shaders(struct radv_pipeline *pipeline,
		  struct radv_shader_variant *compute_shader, FILE *f)
{
	unsigned mask;

	/* Dump active graphics shaders. */
	mask = pipeline->active_stages;
	while (mask) {
		int stage = u_bit_scan(&mask);

		radv_dump_shader(pipeline, pipeline->shaders[stage], stage, f);
	}

	radv_dump_shader(pipeline, compute_shader, MESA_SHADER_COMPUTE, f);
}

static void
radv_dump_graphics_state(struct radv_pipeline *graphics_pipeline,
			 struct radv_pipeline *compute_pipeline, FILE *f)
{
	struct radv_shader_variant *compute_shader =
		compute_pipeline ? compute_pipeline->shaders[MESA_SHADER_COMPUTE] : NULL;

	if (!graphics_pipeline)
		return;

	radv_dump_shaders(graphics_pipeline, compute_shader, f);
	radv_dump_annotated_shaders(graphics_pipeline, compute_shader, f);
	radv_dump_descriptors(graphics_pipeline, f);
}

static void
radv_dump_compute_state(struct radv_pipeline *compute_pipeline, FILE *f)
{
	if (!compute_pipeline)
		return;

	radv_dump_shaders(compute_pipeline,
			  compute_pipeline->shaders[MESA_SHADER_COMPUTE], f);
	radv_dump_annotated_shaders(compute_pipeline,
				    compute_pipeline->shaders[MESA_SHADER_COMPUTE],
				    f);
	radv_dump_descriptors(compute_pipeline, f);
}

static struct radv_pipeline *
radv_get_saved_graphics_pipeline(struct radv_device *device)
{
	uint64_t *ptr = (uint64_t *)device->trace_id_ptr;

	return (struct radv_pipeline *)ptr[1];
}

static struct radv_pipeline *
radv_get_saved_compute_pipeline(struct radv_device *device)
{
	uint64_t *ptr = (uint64_t *)device->trace_id_ptr;

	return (struct radv_pipeline *)ptr[2];
}

static void
radv_dump_dmesg(FILE *f)
{
	char line[2000];
	FILE *p;

	p = popen("dmesg | tail -n60", "r");
	if (!p)
		return;

	fprintf(f, "\nLast 60 lines of dmesg:\n\n");
	while (fgets(line, sizeof(line), p))
		fputs(line, f);
	fprintf(f, "\n");

	pclose(p);
}

void
radv_dump_enabled_options(struct radv_device *device, FILE *f)
{
	uint64_t mask;

	if (device->instance->debug_flags) {
		fprintf(f, "Enabled debug options: ");

		mask = device->instance->debug_flags;
		while (mask) {
			int i = u_bit_scan64(&mask);
			fprintf(f, "%s, ", radv_get_debug_option_name(i));
		}
		fprintf(f, "\n");
	}

	if (device->instance->perftest_flags) {
		fprintf(f, "Enabled perftest options: ");

		mask = device->instance->perftest_flags;
		while (mask) {
			int i = u_bit_scan64(&mask);
			fprintf(f, "%s, ", radv_get_perftest_option_name(i));
		}
		fprintf(f, "\n");
	}
}

static void
radv_dump_device_name(struct radv_device *device, FILE *f)
{
	struct radeon_info *info = &device->physical_device->rad_info;
	char llvm_string[32] = {}, kernel_version[128] = {};
	struct utsname uname_data;
	const char *chip_name;

	chip_name = device->ws->get_chip_name(device->ws);

	if (uname(&uname_data) == 0)
		snprintf(kernel_version, sizeof(kernel_version),
			 " / %s", uname_data.release);

	if (HAVE_LLVM > 0) {
		snprintf(llvm_string, sizeof(llvm_string),
			 ", LLVM %i.%i.%i", (HAVE_LLVM >> 8) & 0xff,
			 HAVE_LLVM & 0xff, MESA_LLVM_VERSION_PATCH);
	}

	fprintf(f, "Device name: %s (%s DRM %i.%i.%i%s%s)\n\n",
		chip_name, device->physical_device->name,
		info->drm_major, info->drm_minor, info->drm_patchlevel,
		kernel_version, llvm_string);
}

static bool
radv_gpu_hang_occured(struct radv_queue *queue, enum ring_type ring)
{
	struct radeon_winsys *ws = queue->device->ws;

	if (!ws->ctx_wait_idle(queue->hw_ctx, ring, queue->queue_idx))
		return true;

	return false;
}

void
radv_check_gpu_hangs(struct radv_queue *queue, struct radeon_winsys_cs *cs)
{
	struct radv_pipeline *graphics_pipeline, *compute_pipeline;
	struct radv_device *device = queue->device;
	enum ring_type ring;
	uint64_t addr;

	ring = radv_queue_family_to_ring(queue->queue_family_index);

	bool hang_occurred = radv_gpu_hang_occured(queue, ring);
	bool vm_fault_occurred = false;
	if (queue->device->instance->debug_flags & RADV_DEBUG_VM_FAULTS)
		vm_fault_occurred = ac_vm_fault_occured(device->physical_device->rad_info.chip_class,
		                                        &device->dmesg_timestamp, &addr);
	if (!hang_occurred && !vm_fault_occurred)
		return;

	graphics_pipeline = radv_get_saved_graphics_pipeline(device);
	compute_pipeline = radv_get_saved_compute_pipeline(device);

	fprintf(stderr, "GPU hang report:\n\n");
	radv_dump_device_name(device, stderr);

	radv_dump_enabled_options(device, stderr);
	radv_dump_dmesg(stderr);

	if (vm_fault_occurred) {
		fprintf(stderr, "VM fault report.\n\n");
		fprintf(stderr, "Failing VM page: 0x%08"PRIx64"\n\n", addr);
	}

	radv_dump_debug_registers(device, stderr);

	switch (ring) {
	case RING_GFX:
		radv_dump_graphics_state(graphics_pipeline, compute_pipeline,
					 stderr);
		break;
	case RING_COMPUTE:
		radv_dump_compute_state(compute_pipeline, stderr);
		break;
	default:
		assert(0);
		break;
	}

	radv_dump_trace(queue->device, cs);
	abort();
}

void
radv_print_spirv(uint32_t *data, uint32_t size, FILE *fp)
{
	char path[] = "/tmp/fileXXXXXX";
	char line[2048], command[128];
	FILE *p;
	int fd;

	/* Dump the binary into a temporary file. */
	fd = mkstemp(path);
	if (fd < 0)
		return;

	if (write(fd, data, size) == -1)
		goto fail;

	sprintf(command, "spirv-dis %s", path);

	/* Disassemble using spirv-dis if installed. */
	p = popen(command, "r");
	if (p) {
		while (fgets(line, sizeof(line), p))
			fprintf(fp, "%s", line);
		pclose(p);
	}

fail:
	close(fd);
	unlink(path);
}
