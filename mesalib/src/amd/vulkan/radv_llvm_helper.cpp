/*
 * Copyright © 2018 Red Hat.
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
#include "ac_llvm_util.h"
#include "ac_llvm_build.h"
#include "radv_shader_helper.h"

#include <list>
class radv_llvm_per_thread_info {
public:
	radv_llvm_per_thread_info(enum radeon_family arg_family,
				enum ac_target_machine_options arg_tm_options,
				unsigned arg_wave_size)
		: family(arg_family), tm_options(arg_tm_options),
		  wave_size(arg_wave_size), passes(NULL), passes_wave32(NULL) {}

	~radv_llvm_per_thread_info()
	{
		ac_destroy_llvm_compiler(&llvm_info);
	}

	bool init(void)
	{
		if (!ac_init_llvm_compiler(&llvm_info,
					  family,
					  tm_options))
			return false;

		passes = ac_create_llvm_passes(llvm_info.tm);
		if (!passes)
			return false;

		if (llvm_info.tm_wave32) {
			passes_wave32 = ac_create_llvm_passes(llvm_info.tm_wave32);
			if (!passes_wave32)
				return false;
		}

		return true;
	}

	bool compile_to_memory_buffer(LLVMModuleRef module,
				      char **pelf_buffer, size_t *pelf_size)
	{
		struct ac_compiler_passes *p = wave_size == 32 ? passes_wave32 : passes;
		return ac_compile_module_to_elf(p, module, pelf_buffer, pelf_size);
	}

	bool is_same(enum radeon_family arg_family,
		     enum ac_target_machine_options arg_tm_options,
		     unsigned arg_wave_size) {
		if (arg_family == family &&
		    arg_tm_options == tm_options &&
		    arg_wave_size == wave_size)
			return true;
		return false;
	}
	struct ac_llvm_compiler llvm_info;
private:
	enum radeon_family family;
	enum ac_target_machine_options tm_options;
	unsigned wave_size;
	struct ac_compiler_passes *passes;
	struct ac_compiler_passes *passes_wave32;
};

/* we have to store a linked list per thread due to the possiblity of multiple gpus being required */
static thread_local std::list<radv_llvm_per_thread_info> radv_llvm_per_thread_list;

bool radv_compile_to_elf(struct ac_llvm_compiler *info,
			LLVMModuleRef module,
			char **pelf_buffer, size_t *pelf_size)
{
	radv_llvm_per_thread_info *thread_info = nullptr;

	for (auto &I : radv_llvm_per_thread_list) {
		if (I.llvm_info.tm == info->tm) {
			thread_info = &I;
			break;
		}
	}

	if (!thread_info) {
		struct ac_compiler_passes *passes = ac_create_llvm_passes(info->tm);
		bool ret = ac_compile_module_to_elf(passes, module, pelf_buffer, pelf_size);
		ac_destroy_llvm_passes(passes);
		return ret;
	}

	return thread_info->compile_to_memory_buffer(module, pelf_buffer, pelf_size);
}

bool radv_init_llvm_compiler(struct ac_llvm_compiler *info,
			     bool thread_compiler,
			     enum radeon_family family,
			     enum ac_target_machine_options tm_options,
			     unsigned wave_size)
{
	if (thread_compiler) {
		for (auto &I : radv_llvm_per_thread_list) {
			if (I.is_same(family, tm_options, wave_size)) {
				*info = I.llvm_info;
				return true;
			}
		}

		radv_llvm_per_thread_list.emplace_back(family, tm_options, wave_size);
		radv_llvm_per_thread_info &tinfo = radv_llvm_per_thread_list.back();

		if (!tinfo.init()) {
			radv_llvm_per_thread_list.pop_back();
			return false;
		}

		*info = tinfo.llvm_info;
		return true;
	}

	if (!ac_init_llvm_compiler(info,
				   family,
				   tm_options))
		return false;
	return true;
}

void radv_destroy_llvm_compiler(struct ac_llvm_compiler *info,
				bool thread_compiler)
{
	if (!thread_compiler)
		ac_destroy_llvm_compiler(info);
}
