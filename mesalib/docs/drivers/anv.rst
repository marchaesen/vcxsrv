ANV
===

Debugging
---------

Here are a few environment variable debug environment variables
specific to Anv:

:envvar:`ANV_ENABLE_PIPELINE_CACHE`
   If defined to ``0`` or ``false``, this will disable pipeline
   caching, forcing Anv to reparse and recompile any VkShaderModule
   (SPIRV) it is given.
:envvar:`ANV_DISABLE_SECONDARY_CMD_BUFFER_CALLS`
   If defined to ``1`` or ``true``, this will prevent usage of self
   modifying command buffers to implement ``vkCmdExecuteCommands``. As
   a result of this, it will also disable ``VK_KHR_performance_query``.
:envvar:`ANV_ALWAYS_BINDLESS`
   If defined to ``1`` or ``true``, this forces all descriptor sets to
   use the internal `Bindless model`_.
:envvar:`ANV_QUEUE_THREAD_DISABLE`
   If defined to ``1`` or ``true``, this disables support for timeline
   semaphores.
:envvar:`ANV_USERSPACE_RELOCS`
   If defined to ``1`` or ``true``, this forces Anv to always do
   kernel relocations in command buffers. This should only have an
   effect on hardware that doesn't support soft-pinning (Ivybridge,
   Haswell, Cherryview).
:envvar:`ANV_PRIMITIVE_REPLICATION_MAX_VIEWS`
   Specifies up to how many view shaders can be lowered to handle
   VK_KHR_multiview. Beyond this number, multiview is implemented
   using instanced rendering. If unspecified, the value default to
   ``2``.


Experimental features
---------------------

:envvar:`ANV_EXPERIMENTAL_NV_MESH_SHADER`
   If defined to ``1`` or ``true``, this advertise support for
   VK_NV_mesh_shader extension for platforms that have hardware
   support for it.


.. _`Bindless model`:

Binding Model
-------------

Here is the Anv bindless binding model that was implemented for the
descriptor indexing feature of Vulkan 1.2 :

.. graphviz::

  digraph G {
    fontcolor="black";
    compound=true;

    subgraph cluster_1 {
      label = "Binding Table (HW)";

      bgcolor="cornflowerblue";

      node [ style=filled,shape="record",fillcolor="white",
             label="RT0"    ] n0;
      node [ label="RT1"    ] n1;
      node [ label="dynbuf0"] n2;
      node [ label="set0"   ] n3;
      node [ label="set1"   ] n4;
      node [ label="set2"   ] n5;

      n0 -> n1 -> n2 -> n3 -> n4 -> n5 [style=invis];
    }
    subgraph cluster_2 {
      label = "Descriptor Set 0";

      bgcolor="burlywood3";
      fixedsize = true;

      node [ style=filled,shape="record",fillcolor="white", fixedsize = true, width=4,
             label="binding 0 - STORAGE_IMAGE\n anv_storage_image_descriptor"          ] n8;
      node [ label="binding 1 - COMBINED_IMAGE_SAMPLER\n anv_sampled_image_descriptor" ] n9;
      node [ label="binding 2 - UNIFORM_BUFFER\n anv_address_range_descriptor"         ] n10;
      node [ label="binding 3 - UNIFORM_TEXEL_BUFFER\n anv_storage_image_descriptor"   ] n11;

      n8 -> n9 -> n10 -> n11 [style=invis];
    }
    subgraph cluster_5 {
      label = "Vulkan Objects"

      fontcolor="black";
      bgcolor="darkolivegreen4";

      subgraph cluster_6 {
        label = "VkImageView";

        bgcolor=darkolivegreen3;
        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="surface_state" ] n12;
      }
      subgraph cluster_7 {
        label = "VkSampler";

        bgcolor=darkolivegreen3;
        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="sample_state" ] n13;
      }
      subgraph cluster_8 {
        label = "VkImageView";
        bgcolor="darkolivegreen3";

        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="surface_state" ] n14;
      }
      subgraph cluster_9 {
        label = "VkBuffer";
        bgcolor=darkolivegreen3;

        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="address" ] n15;
      }
      subgraph cluster_10 {
        label = "VkBufferView";

        bgcolor=darkolivegreen3;
        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="surface_state" ] n16;
      }

      n12 -> n13 -> n14 -> n15 -> n16 [style=invis];
    }

    subgraph cluster_11 {
      subgraph cluster_12 {
        label = "CommandBuffer state stream";

        bgcolor="gold3";
        node [ style=filled,shape="box",fillcolor="white", fixedsize = true, width=2,
               label="surface_state" ] n17;
        node [ label="surface_state" ] n18;
        node [ label="surface_state" ] n19;

        n17 -> n18 -> n19 [style=invis];
      }
    }

    n3  -> n8 [lhead=cluster_2];

    n8  -> n12;
    n9  -> n13;
    n9  -> n14;
    n10 -> n15;
    n11 -> n16;

    n0 -> n17;
    n1 -> n18;
    n2 -> n19;
  }



The HW binding table is generated when the draw or dispatch commands
are emitted. Here are the types of entries one can find in the binding
table :

- The currently bound descriptor sets, one entry per descriptor set
  (our limit is 8).

- For dynamic buffers, one entry per dynamic buffer.

- For draw commands, render target entries if needed.

The entries of the HW binding table for descriptor sets are
RENDER_SURFACE_STATE similar to what you would have for a normal
uniform buffer. The shader will emit reads this buffer first to get
the information it needs to access a surface/sampler/etc... and then
emits the appropriate message using the information gathered from the
descriptor set buffer.

Each binding type entry gets an associated structure in memory
(``anv_storage_image_descriptor``, ``anv_sampled_image_descriptor``,
``anv_address_range_descriptor``, ``anv_storage_image_descriptor``).
This is the information read by the shader.


.. _`Descriptor Set Memory Layout`:

Descriptor Set Memory Layout
----------------------------

Here is a representation of how the descriptor set bindings, with each
elements in each binding is mapped to a the descriptor set memory :

.. graphviz::

  digraph structs {
    node [shape=record];
    rankdir=LR;

    struct1 [label="Descriptor Set | \
                    <b0> binding 0\n STORAGE_IMAGE \n (array_length=3) | \
                    <b1> binding 1\n COMBINED_IMAGE_SAMPLER \n (array_length=2) | \
                    <b2> binding 2\n UNIFORM_BUFFER \n (array_length=1) | \
                    <b3> binding 3\n UNIFORM_TEXEL_BUFFER \n (array_length=1)"];
    struct2 [label="Descriptor Set Memory | \
                    <b0e0> anv_storage_image_descriptor|\
                    <b0e1> anv_storage_image_descriptor|\
                    <b0e2> anv_storage_image_descriptor|\
                    <b1e0> anv_sampled_image_descriptor|\
                    <b1e1> anv_sampled_image_descriptor|\
                    <b2e0> anv_address_range_descriptor|\
                    <b3e0> anv_storage_image_descriptor"];

    struct1:b0 -> struct2:b0e0;
    struct1:b0 -> struct2:b0e1;
    struct1:b0 -> struct2:b0e2;
    struct1:b1 -> struct2:b1e0;
    struct1:b1 -> struct2:b1e1;
    struct1:b2 -> struct2:b2e0;
    struct1:b3 -> struct2:b3e0;
  }

Each Binding in the descriptor set is allocated an array of
``anv_*_descriptor`` data structure. The type of ``anv_*_descriptor``
used for a binding is selected based on the ``VkDescriptorType`` of
the bindings.

The value of ``anv_descriptor_set_binding_layout::descriptor_offset``
is a byte offset from the descriptor set memory to the associated
binding. ``anv_descriptor_set_binding_layout::array_size`` is the
number of ``anv_*_descriptor`` elements in the descriptor set memory
from that offset for the binding.
