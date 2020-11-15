IR3 NOTES
=========

Some notes about ir3, the compiler and machine-specific IR for the shader ISA introduced with adreno a3xx.  The same shader ISA is present, with some small differences, in adreno a4xx.

Compared to the previous generation a2xx ISA (ir2), the a3xx ISA is a "simple" scalar instruction set.  However, the compiler is responsible, in most cases, to schedule the instructions.  The hardware does not try to hide the shader core pipeline stages.  For a common example, a common (cat2) ALU instruction takes four cycles, so a subsequent cat2 instruction which uses the result must have three intervening instructions (or nops).  When operating on vec4's, typically the corresponding scalar instructions for operating on the remaining three components could typically fit.  Although that results in a lot of edge cases where things fall over, like:

::

  ADD TEMP[0], TEMP[1], TEMP[2]
  MUL TEMP[0], TEMP[1], TEMP[0].wzyx

Here, the second instruction needs the output of the first group of scalar instructions in the wrong order, resulting in not enough instruction spots between the ``add r0.w, r1.w, r2.w`` and ``mul r0.x, r1.x, r0.w``.  Which is why the original (old) compiler which merely translated nearly literally from TGSI to ir3, had a strong tendency to fall over.

So the current compiler instead, in the frontend, generates a directed-acyclic-graph of instructions and basic blocks, which go through various additional passes to eventually schedule and do register assignment.

For additional documentation about the hardware, see wiki: `a3xx ISA
<https://github.com/freedreno/freedreno/wiki/A3xx-shader-instruction-set-architecture>`_.

External Structure
------------------

``ir3_shader``
    A single vertex/fragment/etc shader from gallium perspective (i.e.
    maps to a single TGSI shader), and manages a set of shader variants
    which are generated on demand based on the shader key.

``ir3_shader_key``
    The configuration key that identifies a shader variant.  Ie. based
    on other GL state (two-sided-color, render-to-alpha, etc) or render
    stages (binning-pass vertex shader) different shader variants are
    generated.

``ir3_shader_variant``
    The actual hw shader generated based on input TGSI and shader key.

``ir3_compiler``
    Compiler frontend which generates ir3 and runs the various backend
    stages to schedule and do register assignment.

The IR
------

The ir3 IR maps quite directly to the hardware, in that instruction opcodes map directly to hardware opcodes, and that dst/src register(s) map directly to the hardware dst/src register(s).  But there are a few extensions, in the form of meta_ instructions.  And additionally, for normal (non-const, etc) src registers, the ``IR3_REG_SSA`` flag is set and ``reg->instr`` points to the source instruction which produced that value.  So, for example, the following TGSI shader:

::

  VERT
  DCL IN[0]
  DCL IN[1]
  DCL OUT[0], POSITION
  DCL TEMP[0], LOCAL
    1: DP3 TEMP[0].x, IN[0].xyzz, IN[1].xyzz
    2: MOV OUT[0], TEMP[0].xxxx
    3: END

eventually generates:

.. graphviz::

  digraph G {
  rankdir=RL;
  nodesep=0.25;
  ranksep=1.5;
  subgraph clusterdce198 {
  label="vert";
  inputdce198 [shape=record,label="inputs|<in0> i0.x|<in1> i0.y|<in2> i0.z|<in4> i1.x|<in5> i1.y|<in6> i1.z"];
  instrdcf348 [shape=record,style=filled,fillcolor=lightgrey,label="{mov.f32f32|<dst0>|<src0> }"];
  instrdcedd0 [shape=record,style=filled,fillcolor=lightgrey,label="{mad.f32|<dst0>|<src0> |<src1> |<src2> }"];
  inputdce198:<in2>:w -> instrdcedd0:<src0>
  inputdce198:<in6>:w -> instrdcedd0:<src1>
  instrdcec30 [shape=record,style=filled,fillcolor=lightgrey,label="{mad.f32|<dst0>|<src0> |<src1> |<src2> }"];
  inputdce198:<in1>:w -> instrdcec30:<src0>
  inputdce198:<in5>:w -> instrdcec30:<src1>
  instrdceb60 [shape=record,style=filled,fillcolor=lightgrey,label="{mul.f|<dst0>|<src0> |<src1> }"];
  inputdce198:<in0>:w -> instrdceb60:<src0>
  inputdce198:<in4>:w -> instrdceb60:<src1>
  instrdceb60:<dst0> -> instrdcec30:<src2>
  instrdcec30:<dst0> -> instrdcedd0:<src2>
  instrdcedd0:<dst0> -> instrdcf348:<src0>
  instrdcf400 [shape=record,style=filled,fillcolor=lightgrey,label="{mov.f32f32|<dst0>|<src0> }"];
  instrdcedd0:<dst0> -> instrdcf400:<src0>
  instrdcf4b8 [shape=record,style=filled,fillcolor=lightgrey,label="{mov.f32f32|<dst0>|<src0> }"];
  instrdcedd0:<dst0> -> instrdcf4b8:<src0>
  outputdce198 [shape=record,label="outputs|<out0> o0.x|<out1> o0.y|<out2> o0.z|<out3> o0.w"];
  instrdcf348:<dst0> -> outputdce198:<out0>:e
  instrdcf400:<dst0> -> outputdce198:<out1>:e
  instrdcf4b8:<dst0> -> outputdce198:<out2>:e
  instrdcedd0:<dst0> -> outputdce198:<out3>:e
  }
  }

(after scheduling, etc, but before register assignment).

Internal Structure
~~~~~~~~~~~~~~~~~~

``ir3_block``
    Represents a basic block.

    TODO: currently blocks are nested, but I think I need to change that
    to a more conventional arrangement before implementing proper flow
    control.  Currently the only flow control handles is if/else which
    gets flattened out and results chosen with ``sel`` instructions.

``ir3_instruction``
    Represents a machine instruction or meta_ instruction.  Has pointers
    to dst register (``regs[0]``) and src register(s) (``regs[1..n]``),
    as needed.

``ir3_register``
    Represents a src or dst register, flags indicate const/relative/etc.
    If ``IR3_REG_SSA`` is set on a src register, the actual register
    number (name) has not been assigned yet, and instead the ``instr``
    field points to src instruction.

In addition there are various util macros/functions to simplify manipulation/traversal of the graph:

``foreach_src(srcreg, instr)``
    Iterate each instruction's source ``ir3_register``\s

``foreach_src_n(srcreg, n, instr)``
    Like ``foreach_src``, also setting ``n`` to the source number (starting
    with ``0``).

``foreach_ssa_src(srcinstr, instr)``
    Iterate each instruction's SSA source ``ir3_instruction``\s.  This skips
    non-SSA sources (consts, etc), but includes virtual sources (such as the
    address register if `relative addressing`_ is used).

``foreach_ssa_src_n(srcinstr, n, instr)``
    Like ``foreach_ssa_src``, also setting ``n`` to the source number.

For example:

.. code-block:: c

  foreach_ssa_src_n(src, i, instr) {
    unsigned d = delay_calc_srcn(ctx, src, instr, i);
    delay = MAX2(delay, d);
  }


TODO probably other helper/util stuff worth mentioning here

.. _meta:

Meta Instructions
~~~~~~~~~~~~~~~~~

**input**
    Used for shader inputs (registers configured in the command-stream
    to hold particular input values, written by the shader core before
    start of execution.  Also used for connecting up values within a
    basic block to an output of a previous block.

**output**
    Used to hold outputs of a basic block.

**flow**
    TODO

**phi**
    TODO

**fanin**
    Groups registers which need to be assigned to consecutive scalar
    registers, for example `sam` (texture fetch) src instructions (see
    `register groups`_) or array element dereference
    (see `relative addressing`_).

**fanout**
    The counterpart to **fanin**, when an instruction such as `sam`
    writes multiple components, splits the result into individual
    scalar components to be consumed by other instructions.


.. _`flow control`:

Flow Control
~~~~~~~~~~~~

TODO


.. _`register groups`:

Register Groups
~~~~~~~~~~~~~~~

Certain instructions, such as texture sample instructions, consume multiple consecutive scalar registers via a single src register encoded in the instruction, and/or write multiple consecutive scalar registers.  In the simplest example:

::

  sam (f32)(xyz)r2.x, r0.z, s#0, t#0

for a 2d texture, would read ``r0.zw`` to get the coordinate, and write ``r2.xyz``.

Before register assignment, to group the two components of the texture src together:

.. graphviz::

  digraph G {
    { rank=same;
      fanin;
    };
    { rank=same;
      coord_x;
      coord_y;
    };
    sam -> fanin [label="regs[1]"];
    fanin -> coord_x [label="regs[1]"];
    fanin -> coord_y [label="regs[2]"];
    coord_x -> coord_y [label="right",style=dotted];
    coord_y -> coord_x [label="left",style=dotted];
    coord_x [label="coord.x"];
    coord_y [label="coord.y"];
  }

The frontend sets up the SSA ptrs from ``sam`` source register to the ``fanin`` meta instruction, which in turn points to the instructions producing the ``coord.x`` and ``coord.y`` values.  And the grouping_ pass sets up the ``left`` and ``right`` neighbor pointers to the ``fanin``\'s sources, used later by the `register assignment`_ pass to assign blocks of scalar registers.

And likewise, for the consecutive scalar registers for the destination:

.. graphviz::

  digraph {
    { rank=same;
      A;
      B;
      C;
    };
    { rank=same;
      fanout_0;
      fanout_1;
      fanout_2;
    };
    A -> fanout_0;
    B -> fanout_1;
    C -> fanout_2;
    fanout_0 [label="fanout\noff=0"];
    fanout_0 -> sam;
    fanout_1 [label="fanout\noff=1"];
    fanout_1 -> sam;
    fanout_2 [label="fanout\noff=2"];
    fanout_2 -> sam;
    fanout_0 -> fanout_1 [label="right",style=dotted];
    fanout_1 -> fanout_0 [label="left",style=dotted];
    fanout_1 -> fanout_2 [label="right",style=dotted];
    fanout_2 -> fanout_1 [label="left",style=dotted];
    sam;
  }

.. _`relative addressing`:

Relative Addressing
~~~~~~~~~~~~~~~~~~~

Most instructions support addressing indirectly (relative to address register) into const or gpr register file in some or all of their src/dst registers.  In this case the register accessed is taken from ``r<a0.x + n>`` or ``c<a0.x + n>``, i.e. address register (``a0.x``) value plus ``n``, where ``n`` is encoded in the instruction (rather than the absolute register number).

    Note that cat5 (texture sample) instructions are the notable exception, not
    supporting relative addressing of src or dst.

Relative addressing of the const file (for example, a uniform array) is relatively simple.  We don't do register assignment of the const file, so all that is required is to schedule things properly.  Ie. the instruction that writes the address register must be scheduled first, and we cannot have two different address register values live at one time.

But relative addressing of gpr file (which can be as src or dst) has additional restrictions on register assignment (i.e. the array elements must be assigned to consecutive scalar registers).  And in the case of relative dst, subsequent instructions now depend on both the relative write, as well as the previous instruction which wrote that register, since we do not know at compile time which actual register was written.

Each instruction has an optional ``address`` pointer, to capture the dependency on the address register value when relative addressing is used for any of the src/dst register(s).  This behaves as an additional virtual src register, i.e. ``foreach_ssa_src()`` will also iterate the address register (last).

    Note that ``nop``\'s for timing constraints, type specifiers (i.e.
    ``add.f`` vs ``add.u``), etc, omitted for brevity in examples

::

  mova a0.x, hr1.y
  sub r1.y, r2.x, r3.x
  add r0.x, r1.y, c<a0.x + 2>

results in:

.. graphviz::

  digraph {
    rankdir=LR;
    sub;
    const [label="const file"];
    add;
    mova;
    add -> mova;
    add -> sub;
    add -> const [label="off=2"];
  }

The scheduling pass has some smarts to schedule things such that only a single ``a0.x`` value is used at any one time.

To implement variable arrays, values are stored in consecutive scalar registers.  This has some overlap with `register groups`_, in that ``fanin`` and ``fanout`` are used to help group things for the `register assignment`_ pass.

To use a variable array as a src register, a slight variation of what is done for const array src.  The instruction src is a `fanin` instruction that groups all the array members:

::

  mova a0.x, hr1.y
  sub r1.y, r2.x, r3.x
  add r0.x, r1.y, r<a0.x + 2>

results in:

.. graphviz::

  digraph {
    a0 [label="r0.z"];
    a1 [label="r0.w"];
    a2 [label="r1.x"];
    a3 [label="r1.y"];
    sub;
    fanin;
    mova;
    add;
    add -> sub;
    add -> fanin [label="off=2"];
    add -> mova;
    fanin -> a0;
    fanin -> a1;
    fanin -> a2;
    fanin -> a3;
  }

TODO better describe how actual deref offset is derived, i.e. based on array base register.

To do an indirect write to a variable array, a ``fanout`` is used.  Say the array was assigned to registers ``r0.z`` through ``r1.y`` (hence the constant offset of 2):

    Note that only cat1 (mov) can do indirect write.

::

  mova a0.x, hr1.y
  min r2.x, r2.x, c0.x
  mov r<a0.x + 2>, r2.x
  mul r0.x, r0.z, c0.z


In this case, the ``mov`` instruction does not write all elements of the array (compared to usage of ``fanout`` for ``sam`` instructions in grouping_).  But the ``mov`` instruction does need an additional dependency (via ``fanin``) on instructions that last wrote the array element members, to ensure that they get scheduled before the ``mov`` in scheduling_ stage (which also serves to group the array elements for the `register assignment`_ stage).

.. graphviz::

  digraph {
    a0 [label="r0.z"];
    a1 [label="r0.w"];
    a2 [label="r1.x"];
    a3 [label="r1.y"];
    min;
    mova;
    mov;
    mul;
    fanout [label="fanout\noff=0"];
    mul -> fanout;
    fanout -> mov;
    fanin;
    fanin -> a0;
    fanin -> a1;
    fanin -> a2;
    fanin -> a3;
    mov -> min;
    mov -> mova;
    mov -> fanin;
  }

Note that there would in fact be ``fanout`` nodes generated for each array element (although only the reachable ones will be scheduled, etc).



Shader Passes
-------------

After the frontend has generated the use-def graph of instructions, they are run through various passes which include scheduling_ and `register assignment`_.  Because inserting ``mov`` instructions after scheduling would also require inserting additional ``nop`` instructions (since it is too late to reschedule to try and fill the bubbles), the earlier stages try to ensure that (at least given an infinite supply of registers) that `register assignment`_ after scheduling_ cannot fail.

    Note that we essentially have ~256 scalar registers in the
    architecture (although larger register usage will at some thresholds
    limit the number of threads which can run in parallel).  And at some
    point we will have to deal with spilling.

.. _flatten:

Flatten
~~~~~~~

In this stage, simple if/else blocks are flattened into a single block with ``phi`` nodes converted into ``sel`` instructions.  The a3xx ISA has very few predicated instructions, and we would prefer not to use branches for simple if/else.


.. _`copy propagation`:

Copy Propagation
~~~~~~~~~~~~~~~~

Currently the frontend inserts ``mov``\s in various cases, because certain categories of instructions have limitations about const regs as sources.  And the CP pass simply removes all simple ``mov``\s (i.e. src-type is same as dst-type, no abs/neg flags, etc).

The eventual plan is to invert that, with the front-end inserting no ``mov``\s and CP legalize things.


.. _grouping:

Grouping
~~~~~~~~

In the grouping pass, instructions which need to be grouped (for ``fanin``\s, etc) have their ``left`` / ``right`` neighbor pointers setup.  In cases where there is a conflict (i.e. one instruction cannot have two unique left or right neighbors), an additional ``mov`` instruction is inserted.  This ensures that there is some possible valid `register assignment`_ at the later stages.


.. _depth:

Depth
~~~~~

In the depth pass, a depth is calculated for each instruction node within it's basic block.  The depth is the sum of the required cycles (delay slots needed between two instructions plus one) of each instruction plus the max depth of any of it's source instructions.  (meta_ instructions don't add to the depth).  As an instruction's depth is calculated, it is inserted into a per block list sorted by deepest instruction.  Unreachable instructions and inputs are marked.

    TODO: we should probably calculate both hard and soft depths (?) to
    try to coax additional instructions to fit in places where we need
    to use sync bits, such as after a texture fetch or SFU.

.. _scheduling:

Scheduling
~~~~~~~~~~

After the grouping_ pass, there are no more instructions to insert or remove.  Start scheduling each basic block from the deepest node in the depth sorted list created by the depth_ pass, recursively trying to schedule each instruction after it's source instructions plus delay slots.  Insert ``nop``\s as required.

.. _`register assignment`:

Register Assignment
~~~~~~~~~~~~~~~~~~~

TODO


