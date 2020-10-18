# Unofficial GCN/RDNA ISA reference errata

## `v_sad_u32`

The Vega ISA reference writes its behaviour as:

```
D.u = abs(S0.i - S1.i) + S2.u.
```

This is incorrect. The actual behaviour is what is written in the GCN3 reference
guide:

```
ABS_DIFF (A,B) = (A>B) ? (A-B) : (B-A)
D.u = ABS_DIFF (S0.u,S1.u) + S2.u
```

The instruction doesn't subtract the S0 and S1 and use the absolute value (the
_signed_ distance), it uses the _unsigned_ distance between the operands. So
`v_sad_u32(-5, 0, 0)` would return `4294967291` (`-5` interpreted as unsigned),
not `5`.

## `s_bfe_*`

Both the RDNA, Vega and GCN3 ISA references write that these instructions don't write
SCC. They do.

## `v_bcnt_u32_b32`

The Vega ISA reference writes its behaviour as:

```
D.u = 0;
for i in 0 ... 31 do
D.u += (S0.u[i] == 1 ? 1 : 0);
endfor.
```

This is incorrect. The actual behaviour (and number of operands) is what
is written in the GCN3 reference guide:

```
D.u = CountOneBits(S0.u) + S1.u.
```

## SMEM stores

The Vega ISA references doesn't say this (or doesn't make it clear), but
the offset for SMEM stores must be in m0 if IMM == 0.

The RDNA ISA doesn't mention SMEM stores at all, but they seem to be supported
by the chip and are present in LLVM. AMD devs however highly recommend avoiding
these instructions.

## SMEM atomics

RDNA ISA: same as the SMEM stores, the ISA pretends they don't exist, but they
are there in LLVM.

## VMEM stores

All reference guides say (under "Vector Memory Instruction Data Dependencies"):

> When a VM instruction is issued, the address is immediately read out of VGPRs
> and sent to the texture cache. Any texture or buffer resources and samplers
> are also sent immediately. However, write-data is not immediately sent to the
> texture cache.

Reading that, one might think that waitcnts need to be added when writing to
the registers used for a VMEM store's data. Experimentation has shown that this
does not seem to be the case on GFX8 and GFX9 (GFX6 and GFX7 are untested). It
also seems unlikely, since NOPs are apparently needed in a subset of these
situations.

## MIMG opcodes on GFX8/GCN3

The `image_atomic_{swap,cmpswap,add,sub}` opcodes in the GCN3 ISA reference
guide are incorrect. The Vega ISA reference guide has the correct ones.

## VINTRP encoding

VEGA ISA doc says the encoding should be `110010` but `110101` works.

## VOP1 instructions encoded as VOP3

RDNA ISA doc says that `0x140` should be added to the opcode, but that doesn't
work. What works is adding `0x180`, which LLVM also does.

## FLAT, Scratch, Global instructions

The NV bit was removed in RDNA, but some parts of the doc still mention it.

RDNA ISA doc 13.8.1 says that SADDR should be set to 0x7f when ADDR is used, but
9.3.1 says it should be set to NULL. We assume 9.3.1 is correct and set it to
SGPR_NULL.

## Legacy instructions

Some instructions have a `_LEGACY` variant which implements "DX9 rules", in which
the zero "wins" in multiplications, ie. `0.0*x` is always `0.0`. The VEGA ISA
mentions `V_MAC_LEGACY_F32` but this instruction is not really there on VEGA.

## RDNA L0, L1 cache and DLC, GLC bits

The old L1 cache was renamed to L0, and a new L1 cache was added to RDNA. The
L1 cache is 1 cache per shader array. Some instruction encodings have DLC and
GLC bits that interact with the cache.

* DLC ("device level coherent") bit: controls the L1 cache
* GLC ("globally coherent") bit: controls the L0 cache

The recommendation from AMD devs is to always set these two bits at the same time,
as it doesn't make too much sense to set them independently, aside from some
circumstances (eg. we needn't set DLC when only one shader array is used).

Stores and atomics always bypass the L1 cache, so they don't support the DLC bit,
and it shouldn't be set in these cases. Setting the DLC for these cases can result
in graphical glitches or hangs.

## RDNA `s_dcache_wb`

The `s_dcache_wb` is not mentioned in the RDNA ISA doc, but it is needed in order
to achieve correct behavior in some SSBO CTS tests.

## RDNA subvector mode

The documentation of `s_subvector_loop_begin` and `s_subvector_mode_end` is not clear
on what sort of addressing should be used, but it says that it
"is equivalent to an `S_CBRANCH` with extra math", so the subvector loop handling
in ACO is done according to the `s_cbranch` doc.

# Hardware Bugs

## SMEM corrupts VCCZ on SI/CI

[See this LLVM source.](https://github.com/llvm/llvm-project/blob/acb089e12ae48b82c0b05c42326196a030df9b82/llvm/lib/Target/AMDGPU/SIInsertWaits.cpp#L580-L616)

After issuing a SMEM instructions, we need to wait for the SMEM instructions to
finish and then write to vcc (for example, `s_mov_b64 vcc, vcc`) to correct vccz

Currently, we don't do this.

## GCN / GFX6 hazards

### VINTRP followed by a read with `v_readfirstlane` or `v_readlane`

It's required to insert 1 wait state if the dst VGPR of any  `v_interp_*` is
followed by a read with `v_readfirstlane` or `v_readlane` to fix GPU hangs on GFX6.
Note that `v_writelane_*` is apparently not affected. This hazard isn't
documented anywhere but AMD confirmed it.

## RDNA / GFX10 hazards

### SMEM store followed by a load with the same address

We found that an `s_buffer_load` will produce incorrect results if it is preceded
by an `s_buffer_store` with the same address. Inserting an `s_nop` between them
does not mitigate the issue, so an `s_waitcnt lgkmcnt(0)` must be inserted.
This is not mentioned by LLVM among the other GFX10 bugs, but LLVM doesn't use
SMEM stores, so it's not surprising that they didn't notice it.

### VMEMtoScalarWriteHazard

Triggered by:
VMEM/FLAT/GLOBAL/SCRATCH/DS instruction reads an SGPR (or EXEC, or M0).
Then, a SALU/SMEM instruction writes the same SGPR.

Mitigated by:
A VALU instruction or an `s_waitcnt vmcnt(0)` between the two instructions.

### SMEMtoVectorWriteHazard

Triggered by:
An SMEM instruction reads an SGPR. Then, a VALU instruction writes that same SGPR.

Mitigated by:
Any non-SOPP SALU instruction (except `s_setvskip`, `s_version`, and any non-lgkmcnt `s_waitcnt`).

### Offset3fBug

Any branch that is located at offset 0x3f will be buggy. Just insert some NOPs to make sure no branch
is located at this offset.

### InstFwdPrefetchBug

According to LLVM, the `s_inst_prefetch` instruction can cause a hang.
There are no further details.

### LdsMisalignedBug

When there is a misaligned multi-dword FLAT load/store instruction in WGP mode,
it needs to be split into multiple single-dword FLAT instructions.

ACO doesn't use FLAT load/store on GFX10, so is unaffected.

### FlatSegmentOffsetBug

The 12-bit immediate OFFSET field of FLAT instructions must always be 0.
GLOBAL and SCRATCH are unaffected.

ACO doesn't use FLAT load/store on GFX10, so is unaffected.

### VcmpxPermlaneHazard

Triggered by:
Any permlane instruction that follows any VOPC instruction.
Confirmed by AMD devs that despite the name, this doesn't only affect v_cmpx.

Mitigated by: any VALU instruction except `v_nop`.

### VcmpxExecWARHazard

Triggered by:
Any non-VALU instruction reads the EXEC mask. Then, any VALU instruction writes the EXEC mask.

Mitigated by:
A VALU instruction that writes an SGPR (or has a valid SDST operand), or `s_waitcnt_depctr 0xfffe`.
Note: `s_waitcnt_depctr` is an internal instruction, so there is no further information
about what it does or what its operand means.

### LdsBranchVmemWARHazard

Triggered by:
VMEM/GLOBAL/SCRATCH instruction, then a branch, then a DS instruction,
or vice versa: DS instruction, then a branch, then a VMEM/GLOBAL/SCRATCH instruction.

Mitigated by:
Only `s_waitcnt_vscnt null, 0`. Needed even if the first instruction is a load.
