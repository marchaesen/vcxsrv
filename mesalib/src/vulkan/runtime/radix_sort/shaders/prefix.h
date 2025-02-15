// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PREFIX_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PREFIX_H_

//
// Requires several defines
//
#ifndef RS_PREFIX_LIMITS
#error "Error: \"prefix_limits.h\" not loaded"
#endif

#ifndef RS_PREFIX_ARGS
#error "Error: RS_PREFIX_ARGS undefined"
#endif

#ifndef RS_PREFIX_LOAD
#error "Error: RS_PREFIX_LOAD undefined"
#endif

#ifndef RS_PREFIX_STORE
#error "Error: RS_PREFIX_STORE undefined"
#endif

//
// Optional switches:
//
//   * Disable holding original inclusively scanned histogram values in registers.
//
//     #define RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
//

//
// Compute exclusive prefix of uint32_t[256]
//
void
rs_prefix(RS_PREFIX_ARGS)
{
  if (RS_WORKGROUP_SUBGROUPS == 1)
  {
    //
    // Workgroup is a single subgroup so no shared memory is required.
    //

    //
    // Exclusive scan-add the histogram
    //
    const uint32_t               h0     = RS_PREFIX_LOAD(0);
    const uint32_t               h0_inc = subgroupInclusiveAdd(h0);
    RS_SUBGROUP_UNIFORM uint32_t h_last = subgroupBroadcast(h0_inc, RS_SUBGROUP_SIZE - 1);

    RS_PREFIX_STORE(0) = h0_inc - h0;  // exclusive

    //
    // Each iteration is dependent on the previous so no unrolling.  The
    // compiler is free to hoist the loads upward though.
    //
    for (RS_SUBGROUP_UNIFORM uint32_t ii = RS_SUBGROUP_SIZE;  //
         ii < RS_RADIX_SIZE;
         ii += RS_SUBGROUP_SIZE)
      {
        const uint32_t h     = RS_PREFIX_LOAD(ii);
        const uint32_t h_inc = subgroupInclusiveAdd(h) + h_last;
        h_last               = subgroupBroadcast(h_inc, RS_SUBGROUP_SIZE - 1);

        RS_PREFIX_STORE(ii) = h_inc - h;  // exclusive
      }
  }
  else
  {
    //
    // Workgroup is multiple subgroups and uses shared memory to store
    // the scan's intermediate results.
    //
    // Assumes a power-of-two subgroup, workgroup and radix size.
    //
    // Downsweep: Repeatedly scan reductions until they fit in a single
    //            subgroup.
    //
    // Upsweep:   Then uniformly apply reductions to each subgroup.
    //
    //
    //   Subgroup Size |  4 |  8 | 16 | 32 | 64 | 128 |
    //   --------------+----+----+----+----+----+-----+
    //   Sweep 0       | 64 | 32 | 16 |  8 |  4 |   2 | sweep_0[]
    //   Sweep 1       | 16 |  4 |  - |  - |  - |   - | sweep_1[]
    //   Sweep 2       |  4 |  - |  - |  - |  - |   - | sweep_2[]
    //   --------------+----+----+----+----+----+-----+
    //   Total dwords  | 84 | 36 | 16 |  8 |  4 |   2 |
    //   --------------+----+----+----+----+----+-----+
    //
#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
    uint32_t h_exc[RS_H_COMPONENTS];
#endif

    //
    // Downsweep 0
    //
    for (uint32_t ii = 0; ii < RS_H_COMPONENTS; ii++)
    {
      const uint32_t h = RS_PREFIX_LOAD(ii * RS_WORKGROUP_SIZE);

      const uint32_t h_inc = subgroupInclusiveAdd(h);

      const uint32_t smem_idx = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;

      RS_PREFIX_SWEEP0(smem_idx) = subgroupBroadcast(h_inc, RS_SUBGROUP_SIZE - 1);

      //
#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
      h_exc[ii] = h_inc - h;
#else
      RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) = h_inc - h;
#endif
    }

    barrier();

    //
    // Skip generalizing these sweeps for all possible subgroups -- just
    // write them directly.
    //
    if (RS_SUBGROUP_SIZE == 128)
    {
      // There are only two elements in SWEEP0 per subgroup. The scan is
      // trivial so we fold it into the upsweep.
    }
    else if (RS_SUBGROUP_SIZE >= 16)
    {
      //////////////////////////////////////////////////////////////////////
      //
      // Scan 0
      //
      if (RS_SWEEP_0_SIZE != RS_WORKGROUP_SIZE && // workgroup has inactive components
          gl_LocalInvocationID.x < RS_SWEEP_0_SIZE)
        {
          const uint32_t h0_red = RS_PREFIX_SWEEP0(gl_LocalInvocationID.x);
          const uint32_t h0_inc = subgroupInclusiveAdd(h0_red);

          RS_PREFIX_SWEEP0(gl_LocalInvocationID.x) = h0_inc - h0_red;
        }

      barrier();
    }
    else if (RS_SUBGROUP_SIZE == 8)
    {
      if (RS_SWEEP_0_SIZE < RS_WORKGROUP_SIZE)
      {
        //////////////////////////////////////////////////////////////////////
        //
        // Scan 0 and Downsweep 1
        //
        if (gl_LocalInvocationID.x < RS_SWEEP_0_SIZE)  // 32 invocations
          {
            const uint32_t h0_red = RS_PREFIX_SWEEP0(gl_LocalInvocationID.x);
            const uint32_t h0_inc = subgroupInclusiveAdd(h0_red);

            RS_PREFIX_SWEEP0(gl_LocalInvocationID.x) = h0_inc - h0_red;
            RS_PREFIX_SWEEP1(gl_SubgroupID) = subgroupBroadcast(h0_inc, RS_SUBGROUP_SIZE - 1);
          }
      }
      else
      {
        //////////////////////////////////////////////////////////////////////
        //
        // Scan 0 and Downsweep 1
        //
        for (uint32_t ii = 0; ii < RS_S0_PASSES; ii++)  // 32 invocations
        {
          const uint32_t idx0 = (ii * RS_WORKGROUP_SIZE) + gl_LocalInvocationID.x;
          const uint32_t idx1 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;

          const uint32_t h0_red = RS_PREFIX_SWEEP0(idx0);
          const uint32_t h0_inc = subgroupInclusiveAdd(h0_red);

          RS_PREFIX_SWEEP0(idx0) = h0_inc - h0_red;
          RS_PREFIX_SWEEP1(idx1) = subgroupBroadcast(h0_inc, RS_SUBGROUP_SIZE - 1);
        }
      }

      barrier();

      //
      // Scan 1
      //
      if (gl_LocalInvocationID.x < RS_SWEEP_1_SIZE)  // 4 invocations
        {
          const uint32_t h1_red = RS_PREFIX_SWEEP1(gl_LocalInvocationID.x);
          const uint32_t h1_inc = subgroupInclusiveAdd(h1_red);

          RS_PREFIX_SWEEP1(gl_LocalInvocationID.x) = h1_inc - h1_red;
        }

      barrier();
    }
    else if (RS_SUBGROUP_SIZE == 4)
    {
      //////////////////////////////////////////////////////////////////////
      //
      // Scan 0 and Downsweep 1
      //
      if (RS_SWEEP_0_SIZE < RS_WORKGROUP_SIZE)
      {
        if (gl_LocalInvocationID.x < RS_SWEEP_0_SIZE)  // 64 invocations
          {
            const uint32_t h0_red = RS_PREFIX_SWEEP0(gl_LocalInvocationID.x);
            const uint32_t h0_inc = subgroupInclusiveAdd(h0_red);

            RS_PREFIX_SWEEP0(gl_LocalInvocationID.x) = h0_inc - h0_red;
            RS_PREFIX_SWEEP1(gl_SubgroupID)          = subgroupBroadcast(h0_inc, RS_SUBGROUP_SIZE - 1);
          }
      }
      else
      {
        for (uint32_t ii = 0; ii < RS_S0_PASSES; ii++)  // 64 invocations
        {
          const uint32_t idx0 = (ii * RS_WORKGROUP_SIZE) + gl_LocalInvocationID.x;
          const uint32_t idx1 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;

          const uint32_t h0_red = RS_PREFIX_SWEEP0(idx0);
          const uint32_t h0_inc = subgroupInclusiveAdd(h0_red);

          RS_PREFIX_SWEEP0(idx0) = h0_inc - h0_red;
          RS_PREFIX_SWEEP1(idx1) = subgroupBroadcast(h0_inc, RS_SUBGROUP_SIZE - 1);
        }
      }

      barrier();

      //
      // Scan 1 and Downsweep 2
      //
      if (RS_SWEEP_1_SIZE < RS_WORKGROUP_SIZE)
      {
        if (gl_LocalInvocationID.x < RS_SWEEP_1_SIZE)  // 16 invocations
          {
            const uint32_t h1_red = RS_PREFIX_SWEEP1(gl_LocalInvocationID.x);
            const uint32_t h1_inc = subgroupInclusiveAdd(h1_red);

            RS_PREFIX_SWEEP1(gl_LocalInvocationID.x) = h1_inc - h1_red;
            RS_PREFIX_SWEEP2(gl_SubgroupID)          = subgroupBroadcast(h1_inc, RS_SUBGROUP_SIZE - 1);
          }
      }
      else 
      {
        for (uint32_t ii = 0; ii < RS_S1_PASSES; ii++)  // 16 invocations
        {
          const uint32_t idx1 = (ii * RS_WORKGROUP_SIZE) + gl_LocalInvocationID.x;
          const uint32_t idx2 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;

          const uint32_t h1_red = RS_PREFIX_SWEEP1(idx1);
          const uint32_t h1_inc = subgroupInclusiveAdd(h1_red);

          RS_PREFIX_SWEEP1(idx1) = h1_inc - h1_red;
          RS_PREFIX_SWEEP2(idx2) = subgroupBroadcast(h1_inc, RS_SUBGROUP_SIZE - 1);
        }
      }

      barrier();

      //
      // Scan 2
      //
      // 4 invocations
      //
      if (gl_LocalInvocationID.x < RS_SWEEP_2_SIZE)
        {
          const uint32_t h2_red = RS_PREFIX_SWEEP2(gl_LocalInvocationID.x);
          const uint32_t h2_inc = subgroupInclusiveAdd(h2_red);

          RS_PREFIX_SWEEP2(gl_LocalInvocationID.x) = h2_inc - h2_red;
        }

      barrier();
    }

    //////////////////////////////////////////////////////////////////////
    //
    // Final upsweep 0
    //
    if (RS_SUBGROUP_SIZE == 128)
    {
      // There must be more than one subgroup per workgroup, but the maximum
      // workgroup size is 256 so there must be exactly two subgroups per
      // workgroup and RS_H_COMPONENTS must be 1.
#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
      RS_PREFIX_STORE(0) = h_exc[0] + (gl_SubgroupID > 0 ? RS_PREFIX_SWEEP0(0) : 0);
#else
      const uint32_t h_exc = RS_PREFIX_LOAD(0);

      RS_PREFIX_STORE(0) = h_exc + (gl_SubgroupID > 0 ? RS_PREFIX_SWEEP0(0) : 0);
#endif
    }
    else if (RS_SUBGROUP_SIZE >= 16)
    {
      for (uint32_t ii = 0; ii < RS_H_COMPONENTS; ii++)
      {
        const uint32_t idx0 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;

        // clang format issue
#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) = h_exc[ii] + RS_PREFIX_SWEEP0(idx0);
#else
        const uint32_t h_exc = RS_PREFIX_LOAD(ii * RS_WORKGROUP_SIZE);

        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) = h_exc + RS_PREFIX_SWEEP0(idx0);
#endif
      }
    }
    else if (RS_SUBGROUP_SIZE == 8)
    {
      for (uint32_t ii = 0; ii < RS_H_COMPONENTS; ii++)
      {
        const uint32_t idx0 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;
        const uint32_t idx1 = idx0 / RS_SUBGROUP_SIZE;

#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) =
          h_exc[ii] + RS_PREFIX_SWEEP0(idx0) + RS_PREFIX_SWEEP1(idx1);
#else
        const uint32_t h_exc = RS_PREFIX_LOAD(ii * RS_WORKGROUP_SIZE);

        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) =
          h_exc + RS_PREFIX_SWEEP0(idx0) + RS_PREFIX_SWEEP1(idx1);
#endif
      }
    }
    else if (RS_SUBGROUP_SIZE == 4)
    {
      for (uint32_t ii = 0; ii < RS_H_COMPONENTS; ii++)
      {
        const uint32_t idx0 = (ii * RS_WORKGROUP_SUBGROUPS) + gl_SubgroupID;
        const uint32_t idx1 = idx0 / RS_SUBGROUP_SIZE;
        const uint32_t idx2 = idx1 / RS_SUBGROUP_SIZE;

#ifndef RS_PREFIX_DISABLE_COMPONENTS_IN_REGISTERS
        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) =
          h_exc[ii] + (RS_PREFIX_SWEEP0(idx0) + RS_PREFIX_SWEEP1(idx1) + RS_PREFIX_SWEEP2(idx2));
#else
        const uint32_t h_exc = RS_PREFIX_LOAD(ii * RS_WORKGROUP_SIZE);

        RS_PREFIX_STORE(ii * RS_WORKGROUP_SIZE) =
          h_exc + (RS_PREFIX_SWEEP0(idx0) + RS_PREFIX_SWEEP1(idx1) + RS_PREFIX_SWEEP2(idx2));
#endif
      }
    }
  }
}

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PREFIX_H_
