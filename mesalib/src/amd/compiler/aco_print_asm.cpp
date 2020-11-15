#include <array>
#include <iomanip>
#include "aco_ir.h"
#include "llvm-c/Disassembler.h"
#include "ac_llvm_util.h"

#include <llvm/ADT/StringRef.h>
#if LLVM_VERSION_MAJOR >= 11
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#endif

namespace aco {
namespace {

/* LLVM disassembler only supports GFX8+, try to disassemble with CLRXdisasm
 * for GFX6-GFX7 if found on the system, this is better than nothing.
*/
bool print_asm_gfx6_gfx7(Program *program, std::vector<uint32_t>& binary,
                         FILE *output)
{
   char path[] = "/tmp/fileXXXXXX";
   char line[2048], command[128];
   const char *gpu_type;
   FILE *p;
   int fd;

   /* Dump the binary into a temporary file. */
   fd = mkstemp(path);
   if (fd < 0)
      return true;

   for (uint32_t w : binary)
   {
      if (write(fd, &w, sizeof(w)) == -1)
         goto fail;
   }

   /* Determine the GPU type for CLRXdisasm. Use the family for GFX6 chips
    * because it doesn't allow to use gfx600 directly.
    */
   switch (program->chip_class) {
   case GFX6:
      switch (program->family) {
      case CHIP_TAHITI:
         gpu_type = "tahiti";
         break;
      case CHIP_PITCAIRN:
         gpu_type = "pitcairn";
         break;
      case CHIP_VERDE:
         gpu_type = "capeverde";
         break;
      case CHIP_OLAND:
         gpu_type = "oland";
         break;
      case CHIP_HAINAN:
         gpu_type = "hainan";
         break;
      default:
         unreachable("Invalid GFX6 family!");
      }
      break;
   case GFX7:
      gpu_type = "gfx700";
      break;
   default:
      unreachable("Invalid chip class!");
   }

   sprintf(command, "clrxdisasm --gpuType=%s -r %s", gpu_type, path);

   p = popen(command, "r");
   if (p) {
      if (!fgets(line, sizeof(line), p)) {
         fprintf(output, "clrxdisasm not found\n");
         pclose(p);
         goto fail;
      }

      do {
         fputs(line, output);
      } while (fgets(line, sizeof(line), p));

      pclose(p);
   }

   return false;

fail:
   close(fd);
   unlink(path);
   return true;
}

std::pair<bool, size_t> disasm_instr(chip_class chip, LLVMDisasmContextRef disasm,
                                     uint32_t *binary, unsigned exec_size, size_t pos,
                                     char *outline, unsigned outline_size)
{
   /* mask out src2 on v_writelane_b32 */
   if (((chip == GFX8 || chip == GFX9) && (binary[pos] & 0xffff8000) == 0xd28a0000) ||
       (chip >= GFX10 && (binary[pos] & 0xffff8000) == 0xd7610000)) {
      binary[pos+1] = binary[pos+1] & 0xF803FFFF;
   }

   size_t l = LLVMDisasmInstruction(disasm, (uint8_t *) &binary[pos],
                                    (exec_size - pos) * sizeof(uint32_t), pos * 4,
                                    outline, outline_size);

   if (chip >= GFX10 && l == 8 &&
       ((binary[pos] & 0xffff0000) == 0xd7610000) &&
       ((binary[pos + 1] & 0x1ff) == 0xff)) {
      /* v_writelane with literal uses 3 dwords but llvm consumes only 2 */
      l += 4;
   }

   bool invalid = false;
   size_t size;
   if (!l &&
       ((chip >= GFX9 && (binary[pos] & 0xffff8000) == 0xd1348000) || /* v_add_u32_e64 + clamp */
        (chip >= GFX10 && (binary[pos] & 0xffff8000) == 0xd7038000) || /* v_add_u16_e64 + clamp */
        (chip <= GFX9 && (binary[pos] & 0xffff8000) == 0xd1268000) || /* v_add_u16_e64 + clamp */
        (chip >= GFX10 && (binary[pos] & 0xffff8000) == 0xd76d8000) || /* v_add3_u32 + clamp */
        (chip == GFX9 && (binary[pos] & 0xffff8000) == 0xd1ff8000)) /* v_add3_u32 + clamp */) {
      strcpy(outline, "\tinteger addition + clamp");
      bool has_literal = chip >= GFX10 &&
                         (((binary[pos+1] & 0x1ff) == 0xff) || (((binary[pos+1] >> 9) & 0x1ff) == 0xff));
      size = 2 + has_literal;
   } else if (chip >= GFX10 && l == 4 && ((binary[pos] & 0xfe0001ff) == 0x020000f9)) {
      strcpy(outline, "\tv_cndmask_b32 + sdwa");
      size = 2;
   } else if (!l) {
      strcpy(outline, "(invalid instruction)");
      size = 1;
      invalid = true;
   } else {
      assert(l % 4 == 0);
      size = l / 4;
   }

   return std::make_pair(invalid, size);
}
} /* end namespace */

bool print_asm(Program *program, std::vector<uint32_t>& binary,
               unsigned exec_size, FILE *output)
{
   if (program->chip_class <= GFX7) {
      /* Do not abort if clrxdisasm isn't found. */
      print_asm_gfx6_gfx7(program, binary, output);
      return false;
   }

   std::vector<bool> referenced_blocks(program->blocks.size());
   referenced_blocks[0] = true;
   for (Block& block : program->blocks) {
      for (unsigned succ : block.linear_succs)
         referenced_blocks[succ] = true;
   }

   #if LLVM_VERSION_MAJOR >= 11
   std::vector<llvm::SymbolInfoTy> symbols;
   #else
   std::vector<std::tuple<uint64_t, llvm::StringRef, uint8_t>> symbols;
   #endif
   std::vector<std::array<char,16>> block_names;
   block_names.reserve(program->blocks.size());
   for (Block& block : program->blocks) {
      if (!referenced_blocks[block.index])
         continue;
      std::array<char, 16> name;
      sprintf(name.data(), "BB%u", block.index);
      block_names.push_back(name);
      symbols.emplace_back(block.offset * 4, llvm::StringRef(block_names[block_names.size() - 1].data()), 0);
   }

   const char *features = "";
   if (program->chip_class >= GFX10 && program->wave_size == 64) {
      features = "+wavefrontsize64";
   }

   LLVMDisasmContextRef disasm = LLVMCreateDisasmCPUFeatures("amdgcn-mesa-mesa3d",
                                                             ac_get_llvm_processor_name(program->family),
                                                             features,
                                                             &symbols, 0, NULL, NULL);

   size_t pos = 0;
   bool invalid = false;
   unsigned next_block = 0;

   unsigned prev_size = 0;
   unsigned prev_pos = 0;
   unsigned repeat_count = 0;
   while (pos < exec_size) {
      bool new_block = next_block < program->blocks.size() && pos == program->blocks[next_block].offset;
      if (pos + prev_size <= exec_size && prev_pos != pos && !new_block &&
          memcmp(&binary[prev_pos], &binary[pos], prev_size * 4) == 0) {
         repeat_count++;
         pos += prev_size;
         continue;
      } else {
         if (repeat_count)
            fprintf(output, "\t(then repeated %u times)\n", repeat_count);
         repeat_count = 0;
      }

      while (next_block < program->blocks.size() && pos == program->blocks[next_block].offset) {
         if (referenced_blocks[next_block])
            fprintf(output, "BB%u:\n", next_block);
         next_block++;
      }

      char outline[1024];
      std::pair<bool, size_t> res = disasm_instr(
         program->chip_class, disasm, binary.data(), exec_size, pos, outline, sizeof(outline));
      invalid |= res.first;

      fprintf(output, "%-60s ;", outline);

      for (unsigned i = 0; i < res.second; i++)
         fprintf(output, " %.8x", binary[pos + i]);
      fputc('\n', output);

      prev_size = res.second;
      prev_pos = pos;
      pos += res.second;
   }
   assert(next_block == program->blocks.size());

   LLVMDisasmDispose(disasm);

   if (program->constant_data.size()) {
      fputs("\n/* constant data */\n", output);
      for (unsigned i = 0; i < program->constant_data.size(); i += 32) {
         fprintf(output, "[%.6u]", i);
         unsigned line_size = std::min<size_t>(program->constant_data.size() - i, 32);
         for (unsigned j = 0; j < line_size; j += 4) {
            unsigned size = std::min<size_t>(program->constant_data.size() - (i + j), 4);
            uint32_t v = 0;
            memcpy(&v, &program->constant_data[i + j], size);
            fprintf(output, " %.8x", v);
         }
         fputc('\n', output);
      }
   }

   return invalid;
}

}
