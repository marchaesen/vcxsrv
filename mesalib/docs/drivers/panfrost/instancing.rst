Instancing
==========

The attribute descriptor lets the attribute unit compute the address of an
attribute given the vertex and instance ID. Unfortunately, the way this works is
rather complicated when instancing is enabled.

To explain this, first we need to explain how compute and vertex threads are
dispatched.  When a quad is dispatched, it receives a single, linear index.
However, we need to translate that index into a (vertex id, instance id) pair.
One option would be to do:

.. math::
   \text{vertex id} = \text{linear id} \% \text{num vertices}

   \text{instance id} = \text{linear id} / \text{num vertices}

but this involves a costly division and modulus by an arbitrary number.
Instead, we could pad ``num_vertices``. We dispatch
:math:`\text{padded_num_vertices} \cdot \text{num_instances}` threads instead
of :math:`\text{num_vertices} \cdot \text{num_instances}`, which results
in some "extra" threads with :math:`\text{vertex_id} \geq \text{num_vertices}`,
which we have to discard.  The more we pad ``num_vertices``, the more "wasted"
threads we dispatch, but the division is potentially easier.

One straightforward choice is to pad ``num_vertices`` to the next power
of two, which means that the division and modulus are just simple bit shifts
and masking. But the actual algorithm is a bit more complicated. The thread
dispatcher has special support for dividing by 3, 5, 7, and 9, in addition
to dividing by a power of two. As a result, ``padded_num_vertices`` can
be 1, 3, 5, 7, or 9 times a power of two. This results in less wasted threads,
since we need less padding.

``padded_num_vertices`` is picked by the hardware. The driver just specifies
the actual number of vertices. Note that ``padded_num_vertices`` is a multiple
of four (presumably because threads are dispatched in groups of 4). Also,
``padded_num_vertices`` is always at least one more than ``num_vertices``,
which seems like a quirk of the hardware. For larger ``num_vertices``, the
hardware uses the following algorithm: using the binary representation of
``num_vertices``, we look at the most significant set bit as well as the
following 3 bits. Let n be the number of bits after those 4 bits. Then we
set ``padded_num_vertices`` according to the following table:

==========  =======================
high bits   ``padded_num_vertices``
==========  =======================
1000		   :math:`9 \cdot 2^n`
1001		   :math:`5 \cdot 2^{n+1}`
101x		   :math:`3 \cdot 2^{n+2}`
110x		   :math:`7 \cdot 2^{n+1}`
111x		   :math:`2^{n+4}`
==========  =======================

For example, if :math:`\text{num_vertices} = 70` is passed to
:c:func:`glDraw()`, its binary representation is 1000110, so :math:`n = 3`
and the high bits are 1000, and therefore
:math:`\text{padded_num_vertices} = 9 \cdot 2^3 = 72`.

The attribute unit works in terms of the original ``linear_id``. if
:math:`\text{num_instances} = 1`, then they are the same, and everything
is simple. However, with instancing things get more complicated. There are
four possible modes, two of them we can group together:

1. Use the ``linear_id`` directly. Only used when there is no instancing.

2. Use the ``linear_id`` modulo a constant. This is used for per-vertex
attributes with instancing enabled by making the constant equal
``padded_num_vertices``. Because the modulus is always ``padded_num_vertices``,
this mode only supports a modulus that is a power of 2 times 1, 3, 5, 7,
or 9. The shift field specifies the power of two, while the ``extra_flags``
field specifies the odd number. If :math:`\text{shift} = n` and
:math:`\text{extra_flags} = m`, then the modulus is
:math:`(2m + 1) \cdot 2^n`. As an example, if
:math:`\text{num_vertices} = 70`, then as computed above,
:math:`\text{padded_num_vertices} = 9 \cdot 2^3`, so we should set
:math:`\text{extra_flags} = 4` and :math:`\text{shift} = 3`. Note that we
must exactly follow the hardware algorithm used to get ``padded_num_vertices``
in order to correctly implement per-vertex attributes.

3. Divide the ``linear_id`` by a constant. In order to correctly implement
instance divisors, we have to divide ``linear_id`` by ``padded_num_vertices``
times to user-specified divisor. So first we compute ``padded_num_vertices``,
again following the exact same algorithm that the hardware uses, then multiply
it by the GL-level divisor to get the hardware-level divisor. This case is
further divided into two more cases. If the hardware-level divisor is a
power of two, then we just need to shift. The shift amount is specified by
the shift field, so that the hardware-level divisor is just
:math:`2^\text{shift}`.

If it isn't a power of two, then we have to divide by an arbitrary integer.
For that, we use the well-known technique of multiplying by an approximation
of the inverse. The driver must compute the magic multiplier and shift
amount, and then the hardware does the multiplication and shift. The
hardware and driver also use the "round-down" optimization as described in
https://ridiculousfish.com/files/faster_unsigned_division_by_constants.pdf.
The hardware further assumes the multiplier is between :math:`2^{31}` and
:math:`2^{32}`, so the high bit is implicitly set to 1 even though it is set
to 0 by the driver -- presumably this simplifies the hardware multiplier a
little. The hardware first multiplies ``linear_id`` by the multiplier and
takes the high 32 bits, then applies the round-down correction if
:math:`\text{extra_flags} = 1`, then finally shifts right by the shift field.

There are some differences between ridiculousfish's algorithm and the Mali
hardware algorithm, which means that the reference code from ridiculousfish
doesn't always produce the right constants. Mali does not use the pre-shift
optimization, since that would make a hardware implementation slower (it
would have to always do the pre-shift, multiply, and post-shift operations).
It also forces the multiplier to be at least :math:`2^{31}`, which means
that the exponent is entirely fixed, so there is no trial-and-error.
Altogether, given the divisor d, the algorithm the driver must follow is:

1. Set :math:`\text{shift} = \lfloor \log_2(d) \rfloor`.
2. Compute :math:`m = \lceil 2^{shift + 32} / d \rceil` and :math:`e = 2^{shift + 32} % d`.
3. If :math:`e \leq 2^{shift}`, then we need to use the round-down algorithm.
   Set :math:`\text{magic_divisor} = m - 1` and :math:`\text{extra_flags} = 1`.
4. Otherwise, set :math:`\text{magic_divisor} = m` and
   :math:`\text{extra_flags} = 0`.
