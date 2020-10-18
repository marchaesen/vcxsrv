=====================
Adreno Five Microcode
=====================

.. contents::

.. _afuc-introduction:

Introduction
============

Adreno GPUs prior to 6xx use two micro-controllers to parse the command-stream,
setup the hardware for draws (or compute jobs), and do various GPU
housekeeping.  They are relatively simple (basically glorified
register writers) and basically all their state is in a collection
of registers.  Ie. there is no stack, and no memory assigned to
them; any global state like which bank of context registers is to
be used in the next draw is stored in a register.

The setup is similar to radeon, in fact Adreno 2xx thru 4xx used
basically the same instruction set as r600.  There is a "PFP"
(Prefetch Parser) and "ME" (Micro Engine, also confusingly referred
to as "PM4").  These make up the "CP" ("Command Parser").  The
PFP runs ahead of the ME, with some PM4 packets handled entirely
in the PFP.  Between the PFP and ME is a FIFO ("MEQ").  In the
generations prior to Adreno 5xx, the PFP and ME had different
instruction sets.

Starting with Adreno 5xx, a new microcontroller with a unified
instruction set was introduced, although the overall architecture
and purpose of the two microcontrollers remains the same.

For lack of a better name, this new instruction set is called
"Adreno Five MicroCode" or "afuc".  (No idea what Qualcomm calls
it internally.

With Adreno 6xx, the separate PF and ME are replaced with a single
SQE microcontroller using the same instruction set as 5xx.

.. _afuc-overview:

Instruction Set Overview
========================

32bit instruction set with basic arithmatic ops that can take
either two source registers or one src and a 16b immediate.

32 registers, although some are special purpose:

- ``$00`` - always reads zero, otherwise seems to be the PC
- ``$01`` - current PM4 packet header
- ``$1c`` - alias ``$rem``, remaining data in packet
- ``$1d`` - alias ``$addr``
- ``$1f`` - alias ``$data``

Branch instructions have a delay slot so the following instruction
is always executed regardless of whether branch is taken or not.


.. _afuc-alu:

ALU Instructions
================

The following instructions are available:

- ``add``   - add
- ``addhi`` - add + carry (for upper 32b of 64b value)
- ``sub``   - subtract
- ``subhi`` - subtract + carry (for upper 32b of 64b value)
- ``and``   - bitwise AND
- ``or``    - bitwise OR
- ``xor``   - bitwise XOR
- ``not``   - bitwise NOT (no src1)
- ``shl``   - shift-left
- ``ushr``  - unsigned shift-right
- ``ishr``  - signed shift-right
- ``rot``   - rotate-left (like shift-left with wrap-around)
- ``mul8``  - multiply low 8b of two src
- ``min``   - minimum
- ``max``   - maximum
- ``comp``  - compare two values

The ALU instructions can take either two src registers, or a src
plus 16b immediate as 2nd src, ex::

  add $dst, $src, 0x1234   ; src2 is immed
  add $dst, $src1, $src2   ; src2 is reg

The ``not`` instruction only takes a single source::

  not $dst, $src
  not $dst, 0x1234

.. _afuc-alu-cmp:

The ``cmp`` instruction returns:

- ``0x00`` if src1 > src2
- ``0x2b`` if src1 == src2
- ``0x1e`` if src1 < src2

See explanation in :ref:`afuc-branch`


.. _afuc-branch:

Branch Instructions
===================

The following branch/jump instructions are available:

- ``brne`` - branch if not equal (or bit not set)
- ``breq`` - branch if equal (or bit set)
- ``jump`` - unconditional jump

Both ``brne`` and ``breq`` have two forms, comparing the src register
against either a small immediate (up to 5 bits) or a specific bit::

  breq $src, b3, #somelabel  ; branch if src & (1 << 3)
  breq $src, 0x3, #somelabel ; branch if src == 3

The branch instructions are encoded with a 16b relative offset.
Since ``$00`` always reads back zero, it can be used to construct
an unconditional relative jump.

The :ref:`cmp <afuc-alu-cmp>` instruction can be paired with the
bit-test variants of ``brne``/``breq`` to implement gt/ge/lt/le,
due to the bit pattern it returns, for example::

  cmp $04, $02, $03
  breq $04, b1, #somelabel

will branch if ``$02`` is less than or equal to ``$03``.


.. _afuc-call:

Call/Return
===========

Simple subroutines can be implemented with ``call``/``ret``.  The
jump instruction encodes a fixed offset.

  TODO not sure how many levels deep function calls can be nested.
  There isn't really a stack.  Definitely seems to be multiple
  levels of fxn call, see in PFP: CP_CONTEXT_SWITCH_YIELD -> f13 ->
  f22.


.. _afuc-control:

Config Instructions
===================

These seem to read/write config state in other parts of CP.  In at
least some cases I expect these map to CP registers (but possibly
not directly??)

- ``cread $dst, [$off + addr], flags``
- ``cwrite $src, [$off + addr], flags``

In cases where no offset is needed, ``$00`` is frequently used as
the offset.

For example, the following sequences sets::

  ; load CP_INDIRECT_BUFFER parameters from cmdstream:
  mov $02, $data   ; low 32b of IB target address
  mov $03, $data   ; high 32b of IB target
  mov $04, $data   ; IB size in dwords

  ; sanity check # of dwords:
  breq $04, 0x0, #l23 (#69, 04a2)

  ; this seems something to do with figuring out whether
  ; we are going from RB->IB1 or IB1->IB2 (ie. so the
  ; below cwrite instructions update either
  ; CP_IB1_BASE_LO/HI/BUFSIZE or CP_IB2_BASE_LO/HI/BUFSIZE
  and $05, $18, 0x0003
  shl $05, $05, 0x0002

  ; update CP_IBn_BASE_LO/HI/BUFSIZE:
  cwrite $02, [$05 + 0x0b0], 0x8
  cwrite $03, [$05 + 0x0b1], 0x8
  cwrite $04, [$05 + 0x0b2], 0x8



.. _afuc-reg-access:

Register Access
===============

The special registers ``$addr`` and ``$data`` can be used to write GPU
registers, for example, to write::

  mov $addr, CP_SCRATCH_REG[0x2] ; set register to write
  mov $data, $03                 ; CP_SCRATCH_REG[0x2]
  mov $data, $04                 ; CP_SCRATCH_REG[0x3]
  ...

subsequent writes to ``$data`` will increment the address of the register
to write, so a sequence of consecutive registers can be written

To read::

  mov $addr, CP_SCRATCH_REG[0x2]
  mov $03, $addr
  mov $04, $addr

Many registers that are updated frequently have two banks, so they can be
updated without stalling for previous draw to finish.  These banks are
arranged so bit 11 is zero for bank 0 and 1 for bank 1.  The ME fw (at
least the version I'm looking at) stores this in ``$17``, so to update
these registers from ME::

  or $addr, $17, VFD_INDEX_OFFSET
  mov $data, $03
  ...

Note that PFP doesn't seem to use this approach, instead it does something
like::

  mov $0c, CP_SCRATCH_REG[0x7]
  mov $02, 0x789a   ; value
  cwrite $0c, [$00 + 0x010], 0x8
  cwrite $02, [$00 + 0x011], 0x8

Like with the ``$addr``/``$data`` approach, the destination register address
increments on each write.

.. _afuc-mem:

Memory Access
=============

There are no load/store instructions, as such.  The microcontrollers
have only indirect memory access via GPU registers.  There are two
mechanism possible.

Read/Write via CP_NRT Registers
-------------------------------

This seems to be only used by ME.  If PFP were also using it, they would
race with each other.  It seems to be primarily used for small reads.

- ``CP_ME_NRT_ADDR_LO``/``_HI`` - write to set the address to read or write
- ``CP_ME_NRT_DATA`` - write to trigger write to address in ``CP_ME_NRT_ADDR``

The address register increments with successive reads or writes.

Memory Write example::

  ; store 64b value in $04+$05 to 64b address in $02+$03
  mov $addr, CP_ME_NRT_ADDR_LO
  mov $data, $02
  mov $data, $03
  mov $addr, CP_ME_NRT_DATA
  mov $data, $04
  mov $data, $05

Memory Read example::

  ; load 64b value from address in $02+$03 into $04+$05
  mov $addr, CP_ME_NRT_ADDR_LO
  mov $data, $02
  mov $data, $03
  mov $04, $addr
  mov $05, $addr


Read via Control Instructions
-----------------------------

This is used by PFP whenever it needs to read memory.  Also seems to be
used by ME for streaming reads (larger amounts of data).  The DMA access
seems to be done by ROQ.

  TODO might also be possible for write access

  TODO some of the control commands might be synchronizing access
  between PFP and ME??

An example from ``CP_DRAW_INDIRECT`` packet handler::

  mov $07, 0x0004  ; # of dwords to read from draw-indirect buffer
  ; load address of indirect buffer from cmdstream:
  cwrite $data, [$00 + 0x0b8], 0x8
  cwrite $data, [$00 + 0x0b9], 0x8
  ; set # of dwords to read:
  cwrite $07, [$00 + 0x0ba], 0x8
  ...
  ; read parameters from draw-indirect buffer:
  mov $09, $addr
  mov $07, $addr
  cread $12, [$00 + 0x040], 0x8
  ; the start parameter gets written into MEQ, which ME writes
  ; to VFD_INDEX_OFFSET register:
  mov $data, $addr


A6XX NOTES
==========

The ``$14`` register holds global flags set by:

  CP_SKIP_IB2_ENABLE_LOCAL - b8
  CP_SKIP_IB2_ENABLE_GLOBAL - b9
  CP_SET_MARKER
    MODE=GMEM - sets b15
    MODE=BLIT2D - clears b15, b12, b7
  CP_SET_MODE - b29+b30
  CP_SET_VISIBILITY_OVERRIDE - b11, b21, b30?
  CP_SET_DRAW_STATE - checks b29+b30

  CP_COND_REG_EXEC - checks b10, which should be predicate flag?
