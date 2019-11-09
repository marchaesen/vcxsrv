/*
 * Copyright © 2018 Valve Corporation
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
 *
 */

#ifndef ACO_IR_H
#define ACO_IR_H

#include <vector>
#include <set>
#include <bitset>
#include <memory>

#include "nir.h"
#include "ac_binary.h"
#include "amd_family.h"
#include "aco_opcodes.h"
#include "aco_util.h"

struct radv_nir_compiler_options;
struct radv_shader_info;

namespace aco {

extern uint64_t debug_flags;

enum {
   DEBUG_VALIDATE = 0x1,
   DEBUG_VALIDATE_RA = 0x2,
   DEBUG_PERFWARN = 0x4,
};

/**
 * Representation of the instruction's microcode encoding format
 * Note: Some Vector ALU Formats can be combined, such that:
 * - VOP2* | VOP3A represents a VOP2 instruction in VOP3A encoding
 * - VOP2* | DPP represents a VOP2 instruction with data parallel primitive.
 * - VOP2* | SDWA represents a VOP2 instruction with sub-dword addressing.
 *
 * (*) The same is applicable for VOP1 and VOPC instructions.
 */
enum class Format : std::uint16_t {
   /* Pseudo Instruction Format */
   PSEUDO = 0,
   /* Scalar ALU & Control Formats */
   SOP1 = 1,
   SOP2 = 2,
   SOPK = 3,
   SOPP = 4,
   SOPC = 5,
   /* Scalar Memory Format */
   SMEM = 6,
   /* LDS/GDS Format */
   DS = 8,
   /* Vector Memory Buffer Formats */
   MTBUF = 9,
   MUBUF = 10,
   /* Vector Memory Image Format */
   MIMG = 11,
   /* Export Format */
   EXP = 12,
   /* Flat Formats */
   FLAT = 13,
   GLOBAL = 14,
   SCRATCH = 15,

   PSEUDO_BRANCH = 16,
   PSEUDO_BARRIER = 17,
   PSEUDO_REDUCTION = 18,

   /* Vector ALU Formats */
   VOP1 = 1 << 8,
   VOP2 = 1 << 9,
   VOPC = 1 << 10,
   VOP3 = 1 << 11,
   VOP3A = 1 << 11,
   VOP3B = 1 << 11,
   VOP3P = 1 << 12,
   /* Vector Parameter Interpolation Format */
   VINTRP = 1 << 13,
   DPP = 1 << 14,
   SDWA = 1 << 15,
};

enum barrier_interaction {
   barrier_none = 0,
   barrier_buffer = 0x1,
   barrier_image = 0x2,
   barrier_atomic = 0x4,
   barrier_shared = 0x8,
   barrier_count = 4,
};

constexpr Format asVOP3(Format format) {
   return (Format) ((uint32_t) Format::VOP3 | (uint32_t) format);
};

enum class RegType {
   none = 0,
   sgpr,
   vgpr,
   linear_vgpr,
};

struct RegClass {

   enum RC : uint8_t {
      s1 = 1,
      s2 = 2,
      s3 = 3,
      s4 = 4,
      s6 = 6,
      s8 = 8,
      s16 = 16,
      v1 = s1 | (1 << 5),
      v2 = s2 | (1 << 5),
      v3 = s3 | (1 << 5),
      v4 = s4 | (1 << 5),
      v5 = 5  | (1 << 5),
      v6 = 6  | (1 << 5),
      v7 = 7  | (1 << 5),
      v8 = 8  | (1 << 5),
      /* these are used for WWM and spills to vgpr */
      v1_linear = v1 | (1 << 6),
      v2_linear = v2 | (1 << 6),
   };

   RegClass() = default;
   constexpr RegClass(RC rc)
      : rc(rc) {}
   constexpr RegClass(RegType type, unsigned size)
      : rc((RC) ((type == RegType::vgpr ? 1 << 5 : 0) | size)) {}

   constexpr operator RC() const { return rc; }
   explicit operator bool() = delete;

   constexpr RegType type() const { return rc <= RC::s16 ? RegType::sgpr : RegType::vgpr; }
   constexpr unsigned size() const { return (unsigned) rc & 0x1F; }
   constexpr bool is_linear() const { return rc <= RC::s16 || rc & (1 << 6); }
   constexpr RegClass as_linear() const { return RegClass((RC) (rc | (1 << 6))); }

private:
   RC rc;
};

/* transitional helper expressions */
static constexpr RegClass s1{RegClass::s1};
static constexpr RegClass s2{RegClass::s2};
static constexpr RegClass s3{RegClass::s3};
static constexpr RegClass s4{RegClass::s4};
static constexpr RegClass s8{RegClass::s8};
static constexpr RegClass s16{RegClass::s16};
static constexpr RegClass v1{RegClass::v1};
static constexpr RegClass v2{RegClass::v2};
static constexpr RegClass v3{RegClass::v3};
static constexpr RegClass v4{RegClass::v4};
static constexpr RegClass v5{RegClass::v5};
static constexpr RegClass v6{RegClass::v6};
static constexpr RegClass v7{RegClass::v7};
static constexpr RegClass v8{RegClass::v8};

/**
 * Temp Class
 * Each temporary virtual register has a
 * register class (i.e. size and type)
 * and SSA id.
 */
struct Temp {
   Temp() = default;
   constexpr Temp(uint32_t id, RegClass cls) noexcept
      : id_(id), reg_class(cls) {}

   constexpr uint32_t id() const noexcept { return id_; }
   constexpr RegClass regClass() const noexcept { return reg_class; }

   constexpr unsigned size() const noexcept { return reg_class.size(); }
   constexpr RegType type() const noexcept { return reg_class.type(); }
   constexpr bool is_linear() const noexcept { return reg_class.is_linear(); }

   constexpr bool operator <(Temp other) const noexcept { return id() < other.id(); }
   constexpr bool operator==(Temp other) const noexcept { return id() == other.id(); }
   constexpr bool operator!=(Temp other) const noexcept { return id() != other.id(); }

private:
   uint32_t id_:24;
   RegClass reg_class;
};

/**
 * PhysReg
 * Represents the physical register for each
 * Operand and Definition.
 */
struct PhysReg {
   constexpr PhysReg() = default;
   explicit constexpr PhysReg(unsigned r) : reg(r) {}
   constexpr operator unsigned() const { return reg; }

   uint16_t reg = 0;
};

/* helper expressions for special registers */
static constexpr PhysReg m0{124};
static constexpr PhysReg vcc{106};
static constexpr PhysReg sgpr_null{125}; /* GFX10+ */
static constexpr PhysReg exec{126};
static constexpr PhysReg exec_lo{126};
static constexpr PhysReg exec_hi{127};
static constexpr PhysReg scc{253};

/**
 * Operand Class
 * Initially, each Operand refers to either
 * a temporary virtual register
 * or to a constant value
 * Temporary registers get mapped to physical register during RA
 * Constant values are inlined into the instruction sequence.
 */
class Operand final
{
public:
   constexpr Operand()
      : reg_(PhysReg{128}), isTemp_(false), isFixed_(true), isConstant_(false),
        isKill_(false), isUndef_(true), isFirstKill_(false), is64BitConst_(false) {}

   explicit Operand(Temp r) noexcept
   {
      data_.temp = r;
      if (r.id()) {
         isTemp_ = true;
      } else {
         isUndef_ = true;
         setFixed(PhysReg{128});
      }
   };
   explicit Operand(uint32_t v) noexcept
   {
      data_.i = v;
      isConstant_ = true;
      if (v <= 64)
         setFixed(PhysReg{128 + v});
      else if (v >= 0xFFFFFFF0) /* [-16 .. -1] */
         setFixed(PhysReg{192 - v});
      else if (v == 0x3f000000) /* 0.5 */
         setFixed(PhysReg{240});
      else if (v == 0xbf000000) /* -0.5 */
         setFixed(PhysReg{241});
      else if (v == 0x3f800000) /* 1.0 */
         setFixed(PhysReg{242});
      else if (v == 0xbf800000) /* -1.0 */
         setFixed(PhysReg{243});
      else if (v == 0x40000000) /* 2.0 */
         setFixed(PhysReg{244});
      else if (v == 0xc0000000) /* -2.0 */
         setFixed(PhysReg{245});
      else if (v == 0x40800000) /* 4.0 */
         setFixed(PhysReg{246});
      else if (v == 0xc0800000) /* -4.0 */
         setFixed(PhysReg{247});
      else if (v == 0x3e22f983) /* 1/(2*PI) */
         setFixed(PhysReg{248});
      else /* Literal Constant */
         setFixed(PhysReg{255});
   };
   explicit Operand(uint64_t v) noexcept
   {
      isConstant_ = true;
      is64BitConst_ = true;
      if (v <= 64)
         setFixed(PhysReg{128 + (uint32_t) v});
      else if (v >= 0xFFFFFFFFFFFFFFF0) /* [-16 .. -1] */
         setFixed(PhysReg{192 - (uint32_t) v});
      else if (v == 0x3FE0000000000000) /* 0.5 */
         setFixed(PhysReg{240});
      else if (v == 0xBFE0000000000000) /* -0.5 */
         setFixed(PhysReg{241});
      else if (v == 0x3FF0000000000000) /* 1.0 */
         setFixed(PhysReg{242});
      else if (v == 0xBFF0000000000000) /* -1.0 */
         setFixed(PhysReg{243});
      else if (v == 0x4000000000000000) /* 2.0 */
         setFixed(PhysReg{244});
      else if (v == 0xC000000000000000) /* -2.0 */
         setFixed(PhysReg{245});
      else if (v == 0x4010000000000000) /* 4.0 */
         setFixed(PhysReg{246});
      else if (v == 0xC010000000000000) /* -4.0 */
         setFixed(PhysReg{247});
      else if (v == 0x3fc45f306dc9c882) /* 1/(2*PI) */
         setFixed(PhysReg{248});
      else { /* Literal Constant: we don't know if it is a long or double.*/
         isConstant_ = 0;
         assert(false && "attempt to create a 64-bit literal constant");
      }
   };
   explicit Operand(RegClass type) noexcept
   {
      isUndef_ = true;
      data_.temp = Temp(0, type);
      setFixed(PhysReg{128});
   };
   explicit Operand(PhysReg reg, RegClass type) noexcept
   {
      data_.temp = Temp(0, type);
      setFixed(reg);
   }

   constexpr bool isTemp() const noexcept
   {
      return isTemp_;
   }

   constexpr void setTemp(Temp t) noexcept {
      assert(!isConstant_);
      isTemp_ = true;
      data_.temp = t;
   }

   constexpr Temp getTemp() const noexcept
   {
      return data_.temp;
   }

   constexpr uint32_t tempId() const noexcept
   {
      return data_.temp.id();
   }

   constexpr bool hasRegClass() const noexcept
   {
      return isTemp() || isUndefined();
   }

   constexpr RegClass regClass() const noexcept
   {
      return data_.temp.regClass();
   }

   constexpr unsigned size() const noexcept
   {
      if (isConstant())
         return is64BitConst_ ? 2 : 1;
      else
         return data_.temp.size();
   }

   constexpr bool isFixed() const noexcept
   {
      return isFixed_;
   }

   constexpr PhysReg physReg() const noexcept
   {
      return reg_;
   }

   constexpr void setFixed(PhysReg reg) noexcept
   {
      isFixed_ = reg != unsigned(-1);
      reg_ = reg;
   }

   constexpr bool isConstant() const noexcept
   {
      return isConstant_;
   }

   constexpr bool isLiteral() const noexcept
   {
      return isConstant() && reg_ == 255;
   }

   constexpr bool isUndefined() const noexcept
   {
      return isUndef_;
   }

   constexpr uint32_t constantValue() const noexcept
   {
      return data_.i;
   }

   constexpr bool constantEquals(uint32_t cmp) const noexcept
   {
      return isConstant() && constantValue() == cmp;
   }

   constexpr void setKill(bool flag) noexcept
   {
      isKill_ = flag;
      if (!flag)
         setFirstKill(false);
   }

   constexpr bool isKill() const noexcept
   {
      return isKill_ || isFirstKill();
   }

   constexpr void setFirstKill(bool flag) noexcept
   {
      isFirstKill_ = flag;
      if (flag)
         setKill(flag);
   }

   /* When there are multiple operands killing the same temporary,
    * isFirstKill() is only returns true for the first one. */
   constexpr bool isFirstKill() const noexcept
   {
      return isFirstKill_;
   }

private:
   union {
      uint32_t i;
      float f;
      Temp temp = Temp(0, s1);
   } data_;
   PhysReg reg_;
   union {
      struct {
         uint8_t isTemp_:1;
         uint8_t isFixed_:1;
         uint8_t isConstant_:1;
         uint8_t isKill_:1;
         uint8_t isUndef_:1;
         uint8_t isFirstKill_:1;
         uint8_t is64BitConst_:1;
      };
      /* can't initialize bit-fields in c++11, so work around using a union */
      uint8_t control_ = 0;
   };
};

/**
 * Definition Class
 * Definitions are the results of Instructions
 * and refer to temporary virtual registers
 * which are later mapped to physical registers
 */
class Definition final
{
public:
   constexpr Definition() : temp(Temp(0, s1)), reg_(0), isFixed_(0), hasHint_(0), isKill_(0) {}
   Definition(uint32_t index, RegClass type) noexcept
      : temp(index, type) {}
   explicit Definition(Temp tmp) noexcept
      : temp(tmp) {}
   Definition(PhysReg reg, RegClass type) noexcept
      : temp(Temp(0, type))
   {
      setFixed(reg);
   }
   Definition(uint32_t tmpId, PhysReg reg, RegClass type) noexcept
      : temp(Temp(tmpId, type))
   {
      setFixed(reg);
   }

   constexpr bool isTemp() const noexcept
   {
      return tempId() > 0;
   }

   constexpr Temp getTemp() const noexcept
   {
      return temp;
   }

   constexpr uint32_t tempId() const noexcept
   {
      return temp.id();
   }

   constexpr void setTemp(Temp t) noexcept {
      temp = t;
   }

   constexpr RegClass regClass() const noexcept
   {
      return temp.regClass();
   }

   constexpr unsigned size() const noexcept
   {
      return temp.size();
   }

   constexpr bool isFixed() const noexcept
   {
      return isFixed_;
   }

   constexpr PhysReg physReg() const noexcept
   {
      return reg_;
   }

   constexpr void setFixed(PhysReg reg) noexcept
   {
      isFixed_ = 1;
      reg_ = reg;
   }

   constexpr void setHint(PhysReg reg) noexcept
   {
      hasHint_ = 1;
      reg_ = reg;
   }

   constexpr bool hasHint() const noexcept
   {
      return hasHint_;
   }

   constexpr void setKill(bool flag) noexcept
   {
      isKill_ = flag;
   }

   constexpr bool isKill() const noexcept
   {
      return isKill_;
   }

private:
   Temp temp = Temp(0, s1);
   PhysReg reg_;
   union {
      struct {
         uint8_t isFixed_:1;
         uint8_t hasHint_:1;
         uint8_t isKill_:1;
      };
      /* can't initialize bit-fields in c++11, so work around using a union */
      uint8_t control_ = 0;
   };
};

class Block;

struct Instruction {
   aco_opcode opcode;
   Format format;
   uint32_t pass_flags;

   aco::span<Operand> operands;
   aco::span<Definition> definitions;

   constexpr bool isVALU() const noexcept
   {
      return ((uint16_t) format & (uint16_t) Format::VOP1) == (uint16_t) Format::VOP1
          || ((uint16_t) format & (uint16_t) Format::VOP2) == (uint16_t) Format::VOP2
          || ((uint16_t) format & (uint16_t) Format::VOPC) == (uint16_t) Format::VOPC
          || ((uint16_t) format & (uint16_t) Format::VOP3A) == (uint16_t) Format::VOP3A
          || ((uint16_t) format & (uint16_t) Format::VOP3B) == (uint16_t) Format::VOP3B
          || ((uint16_t) format & (uint16_t) Format::VOP3P) == (uint16_t) Format::VOP3P;
   }

   constexpr bool isSALU() const noexcept
   {
      return format == Format::SOP1 ||
             format == Format::SOP2 ||
             format == Format::SOPC ||
             format == Format::SOPK ||
             format == Format::SOPP;
   }

   constexpr bool isVMEM() const noexcept
   {
      return format == Format::MTBUF ||
             format == Format::MUBUF ||
             format == Format::MIMG;
   }

   constexpr bool isDPP() const noexcept
   {
      return (uint16_t) format & (uint16_t) Format::DPP;
   }

   constexpr bool isVOP3() const noexcept
   {
      return ((uint16_t) format & (uint16_t) Format::VOP3A) ||
             ((uint16_t) format & (uint16_t) Format::VOP3B) ||
             format == Format::VOP3P;
   }

   constexpr bool isSDWA() const noexcept
   {
      return (uint16_t) format & (uint16_t) Format::SDWA;
   }

   constexpr bool isFlatOrGlobal() const noexcept
   {
      return format == Format::FLAT || format == Format::GLOBAL;
   }

   constexpr bool usesModifiers() const noexcept;
};

struct SOPK_instruction : public Instruction {
   uint16_t imm;
};

struct SOPP_instruction : public Instruction {
   uint32_t imm;
   int block;
};

struct SOPC_instruction : public Instruction {
};

struct SOP1_instruction : public Instruction {
};

struct SOP2_instruction : public Instruction {
};

/**
 * Scalar Memory Format:
 * For s_(buffer_)load_dword*:
 * Operand(0): SBASE - SGPR-pair which provides base address
 * Operand(1): Offset - immediate (un)signed offset or SGPR
 * Operand(2) / Definition(0): SDATA - SGPR for read / write result
 * Operand(n-1): SOffset - SGPR offset (Vega only)
 *
 * Having no operands is also valid for instructions such as s_dcache_inv.
 *
 */
struct SMEM_instruction : public Instruction {
   bool glc; /* VI+: globally coherent */
   bool dlc; /* NAVI: device level coherent */
   bool nv; /* VEGA only: Non-volatile */
   bool can_reorder;
   bool disable_wqm;
   barrier_interaction barrier;
};

struct VOP1_instruction : public Instruction {
};

struct VOP2_instruction : public Instruction {
};

struct VOPC_instruction : public Instruction {
};

struct VOP3A_instruction : public Instruction {
   bool abs[3];
   bool opsel[4];
   bool clamp;
   unsigned omod;
   bool neg[3];
};

/**
 * Data Parallel Primitives Format:
 * This format can be used for VOP1, VOP2 or VOPC instructions.
 * The swizzle applies to the src0 operand.
 *
 */
struct DPP_instruction : public Instruction {
   uint16_t dpp_ctrl;
   uint8_t row_mask;
   uint8_t bank_mask;
   bool abs[2];
   bool neg[2];
   bool bound_ctrl;
};

struct Interp_instruction : public Instruction {
   unsigned attribute;
   unsigned component;
};

/**
 * Local and Global Data Sharing instructions
 * Operand(0): ADDR - VGPR which supplies the address.
 * Operand(1): DATA0 - First data VGPR.
 * Operand(2): DATA1 - Second data VGPR.
 * Operand(n-1): M0 - LDS size.
 * Definition(0): VDST - Destination VGPR when results returned to VGPRs.
 *
 */
struct DS_instruction : public Instruction {
   int16_t offset0;
   int8_t offset1;
   bool gds;
};

/**
 * Vector Memory Untyped-buffer Instructions
 * Operand(0): VADDR - Address source. Can carry an index and/or offset
 * Operand(1): SRSRC - Specifies which SGPR supplies T# (resource constant)
 * Operand(2): SOFFSET - SGPR to supply unsigned byte offset. (SGPR, M0, or inline constant)
 * Operand(3) / Definition(0): VDATA - Vector GPR for write result / read data
 *
 */
struct MUBUF_instruction : public Instruction {
   unsigned offset; /* Unsigned byte offset - 12 bit */
   bool offen; /* Supply an offset from VGPR (VADDR) */
   bool idxen; /* Supply an index from VGPR (VADDR) */
   bool glc; /* globally coherent */
   bool dlc; /* NAVI: device level coherent */
   bool slc; /* system level coherent */
   bool tfe; /* texture fail enable */
   bool lds; /* Return read-data to LDS instead of VGPRs */
   bool disable_wqm; /* Require an exec mask without helper invocations */
   bool can_reorder;
   barrier_interaction barrier;
};

/**
 * Vector Memory Typed-buffer Instructions
 * Operand(0): VADDR - Address source. Can carry an index and/or offset
 * Operand(1): SRSRC - Specifies which SGPR supplies T# (resource constant)
 * Operand(2): SOFFSET - SGPR to supply unsigned byte offset. (SGPR, M0, or inline constant)
 * Operand(3) / Definition(0): VDATA - Vector GPR for write result / read data
 *
 */
struct MTBUF_instruction : public Instruction {
   uint8_t dfmt : 4; /* Data Format of data in memory buffer */
   uint8_t nfmt : 3; /* Numeric format of data in memory */
   unsigned offset; /* Unsigned byte offset - 12 bit */
   bool offen; /* Supply an offset from VGPR (VADDR) */
   bool idxen; /* Supply an index from VGPR (VADDR) */
   bool glc; /* globally coherent */
   bool dlc; /* NAVI: device level coherent */
   bool slc; /* system level coherent */
   bool tfe; /* texture fail enable */
   bool disable_wqm; /* Require an exec mask without helper invocations */
   bool can_reorder;
   barrier_interaction barrier;
};

/**
 * Vector Memory Image Instructions
 * Operand(0): VADDR - Address source. Can carry an offset or an index.
 * Operand(1): SRSRC - Scalar GPR that specifies the resource constant.
 * Operand(2): SSAMP - Scalar GPR that specifies sampler constant.
 * Operand(3) / Definition(0): VDATA - Vector GPR for read / write result.
 *
 */
struct MIMG_instruction : public Instruction {
   unsigned dmask; /* Data VGPR enable mask */
   unsigned dim; /* NAVI: dimensionality */
   bool unrm; /* Force address to be un-normalized */
   bool dlc; /* NAVI: device level coherent */
   bool glc; /* globally coherent */
   bool slc; /* system level coherent */
   bool tfe; /* texture fail enable */
   bool da; /* declare an array */
   bool lwe; /* Force data to be un-normalized */
   bool r128; /* NAVI: Texture resource size */
   bool a16; /* VEGA, NAVI: Address components are 16-bits */
   bool d16; /* Convert 32-bit data to 16-bit data */
   bool disable_wqm; /* Require an exec mask without helper invocations */
   bool can_reorder;
   barrier_interaction barrier;
};

/**
 * Flat/Scratch/Global Instructions
 * Operand(0): ADDR
 * Operand(1): SADDR
 * Operand(2) / Definition(0): DATA/VDST
 *
 */
struct FLAT_instruction : public Instruction {
   uint16_t offset; /* Vega only */
   bool slc; /* system level coherent */
   bool glc; /* globally coherent */
   bool dlc; /* NAVI: device level coherent */
   bool lds;
   bool nv;
};

struct Export_instruction : public Instruction {
   unsigned enabled_mask;
   unsigned dest;
   bool compressed;
   bool done;
   bool valid_mask;
};

struct Pseudo_instruction : public Instruction {
   bool tmp_in_scc;
   PhysReg scratch_sgpr; /* might not be valid if it's not needed */
};

struct Pseudo_branch_instruction : public Instruction {
   /* target[0] is the block index of the branch target.
    * For conditional branches, target[1] contains the fall-through alternative.
    * A value of 0 means the target has not been initialized (BB0 cannot be a branch target).
    */
   uint32_t target[2];
};

struct Pseudo_barrier_instruction : public Instruction {
};

enum ReduceOp {
   iadd32, iadd64,
   imul32, imul64,
   fadd32, fadd64,
   fmul32, fmul64,
   imin32, imin64,
   imax32, imax64,
   umin32, umin64,
   umax32, umax64,
   fmin32, fmin64,
   fmax32, fmax64,
   iand32, iand64,
   ior32, ior64,
   ixor32, ixor64,
   gfx10_wave64_bpermute
};

/**
 * Subgroup Reduction Instructions, everything except for the data to be
 * reduced and the result as inserted by setup_reduce_temp().
 * Operand(0): data to be reduced
 * Operand(1): reduce temporary
 * Operand(2): vector temporary
 * Definition(0): result
 * Definition(1): scalar temporary
 * Definition(2): scalar identity temporary (not used to store identity on GFX10)
 * Definition(3): scc clobber
 * Definition(4): vcc clobber
 *
 */
struct Pseudo_reduction_instruction : public Instruction {
   ReduceOp reduce_op;
   unsigned cluster_size; // must be 0 for scans
};

struct instr_deleter_functor {
   void operator()(void* p) {
      free(p);
   }
};

template<typename T>
using aco_ptr = std::unique_ptr<T, instr_deleter_functor>;

template<typename T>
T* create_instruction(aco_opcode opcode, Format format, uint32_t num_operands, uint32_t num_definitions)
{
   std::size_t size = sizeof(T) + num_operands * sizeof(Operand) + num_definitions * sizeof(Definition);
   char *data = (char*) calloc(1, size);
   T* inst = (T*) data;

   inst->opcode = opcode;
   inst->format = format;

   inst->operands = aco::span<Operand>((Operand*)(data + sizeof(T)), num_operands);
   inst->definitions = aco::span<Definition>((Definition*)inst->operands.end(), num_definitions);

   return inst;
}

constexpr bool Instruction::usesModifiers() const noexcept
{
   if (isDPP() || isSDWA())
      return true;
   if (!isVOP3())
      return false;
   const VOP3A_instruction *vop3 = static_cast<const VOP3A_instruction*>(this);
   for (unsigned i = 0; i < operands.size(); i++) {
      if (vop3->abs[i] || vop3->opsel[i] || vop3->neg[i])
         return true;
   }
   return vop3->opsel[3] || vop3->clamp || vop3->omod;
}

constexpr bool is_phi(Instruction* instr)
{
   return instr->opcode == aco_opcode::p_phi || instr->opcode == aco_opcode::p_linear_phi;
}

static inline bool is_phi(aco_ptr<Instruction>& instr)
{
   return is_phi(instr.get());
}

constexpr barrier_interaction get_barrier_interaction(Instruction* instr)
{
   switch (instr->format) {
   case Format::SMEM:
      return static_cast<SMEM_instruction*>(instr)->barrier;
   case Format::MUBUF:
      return static_cast<MUBUF_instruction*>(instr)->barrier;
   case Format::MIMG:
      return static_cast<MIMG_instruction*>(instr)->barrier;
   case Format::FLAT:
   case Format::GLOBAL:
      return barrier_buffer;
   case Format::DS:
      return barrier_shared;
   default:
      return barrier_none;
   }
}

enum block_kind {
   /* uniform indicates that leaving this block,
    * all actives lanes stay active */
   block_kind_uniform = 1 << 0,
   block_kind_top_level = 1 << 1,
   block_kind_loop_preheader = 1 << 2,
   block_kind_loop_header = 1 << 3,
   block_kind_loop_exit = 1 << 4,
   block_kind_continue = 1 << 5,
   block_kind_break = 1 << 6,
   block_kind_continue_or_break = 1 << 7,
   block_kind_discard = 1 << 8,
   block_kind_branch = 1 << 9,
   block_kind_merge = 1 << 10,
   block_kind_invert = 1 << 11,
   block_kind_uses_discard_if = 1 << 12,
   block_kind_needs_lowering = 1 << 13,
   block_kind_uses_demote = 1 << 14,
};


struct RegisterDemand {
   constexpr RegisterDemand() = default;
   constexpr RegisterDemand(const int16_t v, const int16_t s) noexcept
      : vgpr{v}, sgpr{s} {}
   int16_t vgpr = 0;
   int16_t sgpr = 0;

   constexpr friend bool operator==(const RegisterDemand a, const RegisterDemand b) noexcept {
      return a.vgpr == b.vgpr && a.sgpr == b.sgpr;
   }

   constexpr bool exceeds(const RegisterDemand other) const noexcept {
      return vgpr > other.vgpr || sgpr > other.sgpr;
   }

   constexpr RegisterDemand operator+(const Temp t) const noexcept {
      if (t.type() == RegType::sgpr)
         return RegisterDemand( vgpr, sgpr + t.size() );
      else
         return RegisterDemand( vgpr + t.size(), sgpr );
   }

   constexpr RegisterDemand operator+(const RegisterDemand other) const noexcept {
      return RegisterDemand(vgpr + other.vgpr, sgpr + other.sgpr);
   }

   constexpr RegisterDemand operator-(const RegisterDemand other) const noexcept {
      return RegisterDemand(vgpr - other.vgpr, sgpr - other.sgpr);
   }

   constexpr RegisterDemand& operator+=(const RegisterDemand other) noexcept {
      vgpr += other.vgpr;
      sgpr += other.sgpr;
      return *this;
   }

   constexpr RegisterDemand& operator-=(const RegisterDemand other) noexcept {
      vgpr -= other.vgpr;
      sgpr -= other.sgpr;
      return *this;
   }

   constexpr RegisterDemand& operator+=(const Temp t) noexcept {
      if (t.type() == RegType::sgpr)
         sgpr += t.size();
      else
         vgpr += t.size();
      return *this;
   }

   constexpr RegisterDemand& operator-=(const Temp t) noexcept {
      if (t.type() == RegType::sgpr)
         sgpr -= t.size();
      else
         vgpr -= t.size();
      return *this;
   }

   constexpr void update(const RegisterDemand other) noexcept {
      vgpr = std::max(vgpr, other.vgpr);
      sgpr = std::max(sgpr, other.sgpr);
   }

};

/* CFG */
struct Block {
   unsigned index;
   unsigned offset = 0;
   std::vector<aco_ptr<Instruction>> instructions;
   std::vector<unsigned> logical_preds;
   std::vector<unsigned> linear_preds;
   std::vector<unsigned> logical_succs;
   std::vector<unsigned> linear_succs;
   RegisterDemand register_demand = RegisterDemand();
   uint16_t loop_nest_depth = 0;
   uint16_t kind = 0;
   int logical_idom = -1;
   int linear_idom = -1;
   Temp live_out_exec = Temp();

   /* this information is needed for predecessors to blocks with phis when
    * moving out of ssa */
   bool scc_live_out = false;
   PhysReg scratch_sgpr = PhysReg(); /* only needs to be valid if scc_live_out != false */

   Block(unsigned idx) : index(idx) {}
   Block() : index(0) {}
};

using Stage = uint16_t;

/* software stages */
static constexpr Stage sw_vs = 1 << 0;
static constexpr Stage sw_gs = 1 << 1;
static constexpr Stage sw_tcs = 1 << 2;
static constexpr Stage sw_tes = 1 << 3;
static constexpr Stage sw_fs = 1 << 4;
static constexpr Stage sw_cs = 1 << 5;
static constexpr Stage sw_mask = 0x3f;

/* hardware stages (can't be OR'd, just a mask for convenience when testing multiple) */
static constexpr Stage hw_vs = 1 << 6;
static constexpr Stage hw_es = 1 << 7; /* not on GFX9. combined into GS on GFX9 (and GFX10/legacy). */
static constexpr Stage hw_gs = 1 << 8;
static constexpr Stage hw_ls = 1 << 9; /* not on GFX9. combined into HS on GFX9 (and GFX10/legacy). */
static constexpr Stage hw_hs = 1 << 10;
static constexpr Stage hw_fs = 1 << 11;
static constexpr Stage hw_cs = 1 << 12;
static constexpr Stage hw_mask = 0x7f << 6;

/* possible settings of Program::stage */
static constexpr Stage vertex_vs = sw_vs | hw_vs;
static constexpr Stage fragment_fs = sw_fs | hw_fs;
static constexpr Stage compute_cs = sw_cs | hw_cs;
static constexpr Stage tess_eval_vs = sw_tes | hw_vs;
/* GFX10/NGG */
static constexpr Stage ngg_vertex_gs = sw_vs | hw_gs;
static constexpr Stage ngg_vertex_geometry_gs = sw_vs | sw_gs | hw_gs;
static constexpr Stage ngg_tess_eval_geometry_gs = sw_tes | sw_gs | hw_gs;
static constexpr Stage ngg_vertex_tess_control_hs = sw_vs | sw_tcs | hw_hs;
/* GFX9 (and GFX10 if NGG isn't used) */
static constexpr Stage vertex_geometry_gs = sw_vs | sw_gs | hw_gs;
static constexpr Stage vertex_tess_control_hs = sw_vs | sw_tcs | hw_hs;
static constexpr Stage tess_eval_geometry_gs = sw_tes | sw_gs | hw_gs;
/* pre-GFX9 */
static constexpr Stage vertex_ls = sw_vs | hw_ls; /* vertex before tesselation control */
static constexpr Stage vertex_es = sw_vs | hw_es; /* vertex before geometry */
static constexpr Stage tess_control_hs = sw_tcs | hw_hs;
static constexpr Stage tess_eval_es = sw_tes | hw_gs; /* tesselation evaluation before geometry */
static constexpr Stage geometry_gs = sw_gs | hw_gs;

class Program final {
public:
   std::vector<Block> blocks;
   RegisterDemand max_reg_demand = RegisterDemand();
   uint16_t num_waves = 0;
   uint16_t max_waves = 0; /* maximum number of waves, regardless of register usage */
   ac_shader_config* config;
   struct radv_shader_info *info;
   enum chip_class chip_class;
   enum radeon_family family;
   unsigned wave_size;
   Stage stage; /* Stage */
   bool needs_exact = false; /* there exists an instruction with disable_wqm = true */
   bool needs_wqm = false; /* there exists a p_wqm instruction */
   bool wb_smem_l1_on_end = false;

   std::vector<uint8_t> constant_data;
   Temp private_segment_buffer;
   Temp scratch_offset;

   uint16_t lds_alloc_granule;
   uint32_t lds_limit; /* in bytes */
   uint16_t vgpr_limit;
   uint16_t sgpr_limit;
   uint16_t physical_sgprs;
   uint16_t sgpr_alloc_granule; /* minus one. must be power of two */

   bool needs_vcc = false;
   bool needs_xnack_mask = false;
   bool needs_flat_scr = false;

   uint32_t allocateId()
   {
      assert(allocationID <= 16777215);
      return allocationID++;
   }

   uint32_t peekAllocationId()
   {
      return allocationID;
   }

   void setAllocationId(uint32_t id)
   {
      allocationID = id;
   }

   Block* create_and_insert_block() {
      blocks.emplace_back(blocks.size());
      return &blocks.back();
   }

   Block* insert_block(Block&& block) {
      block.index = blocks.size();
      blocks.emplace_back(std::move(block));
      return &blocks.back();
   }

private:
   uint32_t allocationID = 1;
};

struct live {
   /* live temps out per block */
   std::vector<std::set<Temp>> live_out;
   /* register demand (sgpr/vgpr) per instruction per block */
   std::vector<std::vector<RegisterDemand>> register_demand;
};

void select_program(Program *program,
                    unsigned shader_count,
                    struct nir_shader *const *shaders,
                    ac_shader_config* config,
                    struct radv_shader_info *info,
                    struct radv_nir_compiler_options *options);

void lower_wqm(Program* program, live& live_vars,
               const struct radv_nir_compiler_options *options);
void lower_bool_phis(Program* program);
void update_vgpr_sgpr_demand(Program* program, const RegisterDemand new_demand);
live live_var_analysis(Program* program, const struct radv_nir_compiler_options *options);
std::vector<uint16_t> dead_code_analysis(Program *program);
void dominator_tree(Program* program);
void insert_exec_mask(Program *program);
void value_numbering(Program* program);
void optimize(Program* program);
void setup_reduce_temp(Program* program);
void lower_to_cssa(Program* program, live& live_vars, const struct radv_nir_compiler_options *options);
void register_allocation(Program *program, std::vector<std::set<Temp>> live_out_per_block);
void ssa_elimination(Program* program);
void lower_to_hw_instr(Program* program);
void schedule_program(Program* program, live& live_vars);
void spill(Program* program, live& live_vars, const struct radv_nir_compiler_options *options);
void insert_wait_states(Program* program);
void insert_NOPs(Program* program);
unsigned emit_program(Program* program, std::vector<uint32_t>& code);
void print_asm(Program *program, std::vector<uint32_t>& binary,
               unsigned exec_size, std::ostream& out);
void validate(Program* program, FILE *output);
bool validate_ra(Program* program, const struct radv_nir_compiler_options *options, FILE *output);
#ifndef NDEBUG
void perfwarn(bool cond, const char *msg, Instruction *instr=NULL);
#else
#define perfwarn(program, cond, msg, ...)
#endif

void aco_print_instr(Instruction *instr, FILE *output);
void aco_print_program(Program *program, FILE *output);

/* number of sgprs that need to be allocated but might notbe addressable as s0-s105 */
uint16_t get_extra_sgprs(Program *program);

/* get number of sgprs allocated required to address a number of sgprs */
uint16_t get_sgpr_alloc(Program *program, uint16_t addressable_sgprs);

/* return number of addressable SGPRs for max_waves */
uint16_t get_addr_sgpr_from_waves(Program *program, uint16_t max_waves);

typedef struct {
   const int16_t opcode_gfx9[static_cast<int>(aco_opcode::num_opcodes)];
   const int16_t opcode_gfx10[static_cast<int>(aco_opcode::num_opcodes)];
   const std::bitset<static_cast<int>(aco_opcode::num_opcodes)> can_use_input_modifiers;
   const std::bitset<static_cast<int>(aco_opcode::num_opcodes)> can_use_output_modifiers;
   const char *name[static_cast<int>(aco_opcode::num_opcodes)];
   const aco::Format format[static_cast<int>(aco_opcode::num_opcodes)];
} Info;

extern const Info instr_info;

}

#endif /* ACO_IR_H */

