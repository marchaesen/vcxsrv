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
 *
 * Authors: Tom Stellard <thomas.stellard@amd.com>
 *
 * Based on radeon_elf_util.c.
 */

#include "ac_binary.h"

#include "util/u_math.h"
#include "util/u_memory.h"

#include <gelf.h>
#include <libelf.h>
#include <stdio.h>

#include <sid.h>

#define SPILLED_SGPRS                                     0x4
#define SPILLED_VGPRS                                     0x8

static void parse_symbol_table(Elf_Data *symbol_table_data,
				const GElf_Shdr *symbol_table_header,
				struct ac_shader_binary *binary)
{
	GElf_Sym symbol;
	unsigned i = 0;
	unsigned symbol_count =
		symbol_table_header->sh_size / symbol_table_header->sh_entsize;

	/* We are over allocating this list, because symbol_count gives the
	 * total number of symbols, and we will only be filling the list
	 * with offsets of global symbols.  The memory savings from
	 * allocating the correct size of this list will be small, and
	 * I don't think it is worth the cost of pre-computing the number
	 * of global symbols.
	 */
	binary->global_symbol_offsets = CALLOC(symbol_count, sizeof(uint64_t));

	while (gelf_getsym(symbol_table_data, i++, &symbol)) {
		unsigned i;
		if (GELF_ST_BIND(symbol.st_info) != STB_GLOBAL ||
		    symbol.st_shndx == 0 /* Undefined symbol */) {
			continue;
		}

		binary->global_symbol_offsets[binary->global_symbol_count] =
					symbol.st_value;

		/* Sort the list using bubble sort.  This list will usually
		 * be small. */
		for (i = binary->global_symbol_count; i > 0; --i) {
			uint64_t lhs = binary->global_symbol_offsets[i - 1];
			uint64_t rhs = binary->global_symbol_offsets[i];
			if (lhs < rhs) {
				break;
			}
			binary->global_symbol_offsets[i] = lhs;
			binary->global_symbol_offsets[i - 1] = rhs;
		}
		++binary->global_symbol_count;
	}
}

static void parse_relocs(Elf *elf, Elf_Data *relocs, Elf_Data *symbols,
			unsigned symbol_sh_link,
			struct ac_shader_binary *binary)
{
	unsigned i;

	if (!relocs || !symbols || !binary->reloc_count) {
		return;
	}
	binary->relocs = CALLOC(binary->reloc_count,
			sizeof(struct ac_shader_reloc));
	for (i = 0; i < binary->reloc_count; i++) {
		GElf_Sym symbol;
		GElf_Rel rel;
		char *symbol_name;
		struct ac_shader_reloc *reloc = &binary->relocs[i];

		gelf_getrel(relocs, i, &rel);
		gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &symbol);
		symbol_name = elf_strptr(elf, symbol_sh_link, symbol.st_name);

		reloc->offset = rel.r_offset;
		strncpy(reloc->name, symbol_name, sizeof(reloc->name)-1);
		reloc->name[sizeof(reloc->name)-1] = 0;
	}
}

void ac_elf_read(const char *elf_data, unsigned elf_size,
		 struct ac_shader_binary *binary)
{
	char *elf_buffer;
	Elf *elf;
	Elf_Scn *section = NULL;
	Elf_Data *symbols = NULL, *relocs = NULL;
	size_t section_str_index;
	unsigned symbol_sh_link = 0;

	/* One of the libelf implementations
	 * (http://www.mr511.de/software/english.htm) requires calling
	 * elf_version() before elf_memory().
	 */
	elf_version(EV_CURRENT);
	elf_buffer = MALLOC(elf_size);
	memcpy(elf_buffer, elf_data, elf_size);

	elf = elf_memory(elf_buffer, elf_size);

	elf_getshdrstrndx(elf, &section_str_index);

	while ((section = elf_nextscn(elf, section))) {
		const char *name;
		Elf_Data *section_data = NULL;
		GElf_Shdr section_header;
		if (gelf_getshdr(section, &section_header) != &section_header) {
			fprintf(stderr, "Failed to read ELF section header\n");
			return;
		}
		name = elf_strptr(elf, section_str_index, section_header.sh_name);
		if (!strcmp(name, ".text")) {
			section_data = elf_getdata(section, section_data);
			binary->code_size = section_data->d_size;
			binary->code = MALLOC(binary->code_size * sizeof(unsigned char));
			memcpy(binary->code, section_data->d_buf, binary->code_size);
		} else if (!strcmp(name, ".AMDGPU.config")) {
			section_data = elf_getdata(section, section_data);
			binary->config_size = section_data->d_size;
			binary->config = MALLOC(binary->config_size * sizeof(unsigned char));
			memcpy(binary->config, section_data->d_buf, binary->config_size);
		} else if (!strcmp(name, ".AMDGPU.disasm")) {
			/* Always read disassembly if it's available. */
			section_data = elf_getdata(section, section_data);
			binary->disasm_string = strndup(section_data->d_buf,
							section_data->d_size);
		} else if (!strncmp(name, ".rodata", 7)) {
			section_data = elf_getdata(section, section_data);
			binary->rodata_size = section_data->d_size;
			binary->rodata = MALLOC(binary->rodata_size * sizeof(unsigned char));
			memcpy(binary->rodata, section_data->d_buf, binary->rodata_size);
		} else if (!strncmp(name, ".symtab", 7)) {
			symbols = elf_getdata(section, section_data);
			symbol_sh_link = section_header.sh_link;
			parse_symbol_table(symbols, &section_header, binary);
		} else if (!strcmp(name, ".rel.text")) {
			relocs = elf_getdata(section, section_data);
			binary->reloc_count = section_header.sh_size /
					section_header.sh_entsize;
		}
	}

	parse_relocs(elf, relocs, symbols, symbol_sh_link, binary);

	if (elf){
		elf_end(elf);
	}
	FREE(elf_buffer);

	/* Cache the config size per symbol */
	if (binary->global_symbol_count) {
		binary->config_size_per_symbol =
			binary->config_size / binary->global_symbol_count;
	} else {
		binary->global_symbol_count = 1;
		binary->config_size_per_symbol = binary->config_size;
	}
}

static
const unsigned char *ac_shader_binary_config_start(
	const struct ac_shader_binary *binary,
	uint64_t symbol_offset)
{
	unsigned i;
	for (i = 0; i < binary->global_symbol_count; ++i) {
		if (binary->global_symbol_offsets[i] == symbol_offset) {
			unsigned offset = i * binary->config_size_per_symbol;
			return binary->config + offset;
		}
	}
	return binary->config;
}


static const char *scratch_rsrc_dword0_symbol =
	"SCRATCH_RSRC_DWORD0";

static const char *scratch_rsrc_dword1_symbol =
	"SCRATCH_RSRC_DWORD1";

void ac_shader_binary_read_config(struct ac_shader_binary *binary,
				  struct ac_shader_config *conf,
				  unsigned symbol_offset)
{
	unsigned i;
	const unsigned char *config =
		ac_shader_binary_config_start(binary, symbol_offset);
	bool really_needs_scratch = false;

	/* LLVM adds SGPR spills to the scratch size.
	 * Find out if we really need the scratch buffer.
	 */
	for (i = 0; i < binary->reloc_count; i++) {
		const struct ac_shader_reloc *reloc = &binary->relocs[i];

		if (!strcmp(scratch_rsrc_dword0_symbol, reloc->name) ||
		    !strcmp(scratch_rsrc_dword1_symbol, reloc->name)) {
			really_needs_scratch = true;
			break;
		}
	}

	for (i = 0; i < binary->config_size_per_symbol; i+= 8) {
		unsigned reg = util_le32_to_cpu(*(uint32_t*)(config + i));
		unsigned value = util_le32_to_cpu(*(uint32_t*)(config + i + 4));
		switch (reg) {
		case R_00B028_SPI_SHADER_PGM_RSRC1_PS:
		case R_00B128_SPI_SHADER_PGM_RSRC1_VS:
		case R_00B228_SPI_SHADER_PGM_RSRC1_GS:
		case R_00B848_COMPUTE_PGM_RSRC1:
			conf->num_sgprs = MAX2(conf->num_sgprs, (G_00B028_SGPRS(value) + 1) * 8);
			conf->num_vgprs = MAX2(conf->num_vgprs, (G_00B028_VGPRS(value) + 1) * 4);
			conf->float_mode =  G_00B028_FLOAT_MODE(value);
			break;
		case R_00B02C_SPI_SHADER_PGM_RSRC2_PS:
			conf->lds_size = MAX2(conf->lds_size, G_00B02C_EXTRA_LDS_SIZE(value));
			break;
		case R_00B84C_COMPUTE_PGM_RSRC2:
			conf->lds_size = MAX2(conf->lds_size, G_00B84C_LDS_SIZE(value));
			break;
		case R_0286CC_SPI_PS_INPUT_ENA:
			conf->spi_ps_input_ena = value;
			break;
		case R_0286D0_SPI_PS_INPUT_ADDR:
			conf->spi_ps_input_addr = value;
			break;
		case R_0286E8_SPI_TMPRING_SIZE:
		case R_00B860_COMPUTE_TMPRING_SIZE:
			/* WAVESIZE is in units of 256 dwords. */
			if (really_needs_scratch)
				conf->scratch_bytes_per_wave =
					G_00B860_WAVESIZE(value) * 256 * 4;
			break;
		case SPILLED_SGPRS:
			conf->spilled_sgprs = value;
			break;
		case SPILLED_VGPRS:
			conf->spilled_vgprs = value;
			break;
		default:
			{
				static bool printed;

				if (!printed) {
					fprintf(stderr, "Warning: LLVM emitted unknown "
						"config register: 0x%x\n", reg);
					printed = true;
				}
			}
			break;
		}

		if (!conf->spi_ps_input_addr)
			conf->spi_ps_input_addr = conf->spi_ps_input_ena;
	}
}
