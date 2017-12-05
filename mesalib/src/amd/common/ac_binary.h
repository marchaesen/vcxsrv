/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef AC_BINARY_H
#define AC_BINARY_H

#include <stdint.h>
#include <stdbool.h>

struct ac_shader_reloc {
	char name[32];
	uint64_t offset;
};

struct ac_shader_binary {
	unsigned code_size;
	unsigned config_size;
	/** The number of bytes of config information for each global symbol.
	 */
	unsigned config_size_per_symbol;
	unsigned rodata_size;
	unsigned global_symbol_count;
	unsigned reloc_count;

	/** Shader code */
	unsigned char *code;

	/** Config/Context register state that accompanies this shader.
	 * This is a stream of dword pairs.  First dword contains the
	 * register address, the second dword contains the value.*/
	unsigned char *config;


	/** Constant data accessed by the shader.  This will be uploaded
	 * into a constant buffer. */
	unsigned char *rodata;

	/** List of symbol offsets for the shader */
	uint64_t *global_symbol_offsets;

	struct ac_shader_reloc *relocs;

	/** Disassembled shader in a string. */
	char *disasm_string;
	char *llvm_ir_string;
};

struct ac_shader_config {
	unsigned num_sgprs;
	unsigned num_vgprs;
	unsigned spilled_sgprs;
	unsigned spilled_vgprs;
	unsigned lds_size;
	unsigned spi_ps_input_ena;
	unsigned spi_ps_input_addr;
	unsigned float_mode;
	unsigned scratch_bytes_per_wave;
};

/*
 * Parse the elf binary stored in \p elf_data and create a
 * ac_shader_binary object.
 */
bool ac_elf_read(const char *elf_data, unsigned elf_size,
		 struct ac_shader_binary *binary);

/**
 * @returns A pointer to the start of the configuration information for
 * the function starting at \p symbol_offset of the binary.
 */
const unsigned char *ac_shader_binary_config_start(
	const struct ac_shader_binary *binary,
	uint64_t symbol_offset);

void ac_shader_binary_read_config(struct ac_shader_binary *binary,
				  struct ac_shader_config *conf,
				  unsigned symbol_offset,
				  bool supports_spill);
void ac_shader_binary_clean(struct ac_shader_binary *b);

#endif /* AC_BINARY_H */
