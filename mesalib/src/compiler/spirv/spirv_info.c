/*
 * Copyright © 2016 Intel Corporation
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

#include "spirv_info.h"
#include "util/macros.h"

#define CAPABILITY(cap) [SpvCapability##cap] = #cap
static const char * const capability_to_string[] = {
   CAPABILITY(Matrix),
   CAPABILITY(Shader),
   CAPABILITY(Geometry),
   CAPABILITY(Tessellation),
   CAPABILITY(Addresses),
   CAPABILITY(Linkage),
   CAPABILITY(Kernel),
   CAPABILITY(Vector16),
   CAPABILITY(Float16Buffer),
   CAPABILITY(Float16),
   CAPABILITY(Float64),
   CAPABILITY(Int64),
   CAPABILITY(Int64Atomics),
   CAPABILITY(ImageBasic),
   CAPABILITY(ImageReadWrite),
   CAPABILITY(ImageMipmap),
   CAPABILITY(Pipes),
   CAPABILITY(Groups),
   CAPABILITY(DeviceEnqueue),
   CAPABILITY(LiteralSampler),
   CAPABILITY(AtomicStorage),
   CAPABILITY(Int16),
   CAPABILITY(TessellationPointSize),
   CAPABILITY(GeometryPointSize),
   CAPABILITY(ImageGatherExtended),
   CAPABILITY(StorageImageMultisample),
   CAPABILITY(UniformBufferArrayDynamicIndexing),
   CAPABILITY(SampledImageArrayDynamicIndexing),
   CAPABILITY(StorageBufferArrayDynamicIndexing),
   CAPABILITY(StorageImageArrayDynamicIndexing),
   CAPABILITY(ClipDistance),
   CAPABILITY(CullDistance),
   CAPABILITY(ImageCubeArray),
   CAPABILITY(SampleRateShading),
   CAPABILITY(ImageRect),
   CAPABILITY(SampledRect),
   CAPABILITY(GenericPointer),
   CAPABILITY(Int8),
   CAPABILITY(InputAttachment),
   CAPABILITY(SparseResidency),
   CAPABILITY(MinLod),
   CAPABILITY(Sampled1D),
   CAPABILITY(Image1D),
   CAPABILITY(SampledCubeArray),
   CAPABILITY(SampledBuffer),
   CAPABILITY(ImageBuffer),
   CAPABILITY(ImageMSArray),
   CAPABILITY(StorageImageExtendedFormats),
   CAPABILITY(ImageQuery),
   CAPABILITY(DerivativeControl),
   CAPABILITY(InterpolationFunction),
   CAPABILITY(TransformFeedback),
   CAPABILITY(GeometryStreams),
   CAPABILITY(StorageImageReadWithoutFormat),
   CAPABILITY(StorageImageWriteWithoutFormat),
   CAPABILITY(MultiViewport),
};

const char *
spirv_capability_to_string(SpvCapability cap)
{
   if (cap < ARRAY_SIZE(capability_to_string))
      return capability_to_string[cap];
   else
      return "unknown";
}

#define DECORATION(dec) [SpvDecoration##dec] = #dec
static const char * const decoration_to_string[] = {
   DECORATION(RelaxedPrecision),
   DECORATION(SpecId),
   DECORATION(Block),
   DECORATION(BufferBlock),
   DECORATION(RowMajor),
   DECORATION(ColMajor),
   DECORATION(ArrayStride),
   DECORATION(MatrixStride),
   DECORATION(GLSLShared),
   DECORATION(GLSLPacked),
   DECORATION(CPacked),
   DECORATION(BuiltIn),
   DECORATION(NoPerspective),
   DECORATION(Flat),
   DECORATION(Patch),
   DECORATION(Centroid),
   DECORATION(Sample),
   DECORATION(Invariant),
   DECORATION(Restrict),
   DECORATION(Aliased),
   DECORATION(Volatile),
   DECORATION(Constant),
   DECORATION(Coherent),
   DECORATION(NonWritable),
   DECORATION(NonReadable),
   DECORATION(Uniform),
   DECORATION(SaturatedConversion),
   DECORATION(Stream),
   DECORATION(Location),
   DECORATION(Component),
   DECORATION(Index),
   DECORATION(Binding),
   DECORATION(DescriptorSet),
   DECORATION(Offset),
   DECORATION(XfbBuffer),
   DECORATION(XfbStride),
   DECORATION(FuncParamAttr),
   DECORATION(FPRoundingMode),
   DECORATION(FPFastMathMode),
   DECORATION(LinkageAttributes),
   DECORATION(NoContraction),
   DECORATION(InputAttachmentIndex),
   DECORATION(Alignment),
};

const char *
spirv_decoration_to_string(SpvDecoration dec)
{
   if (dec < ARRAY_SIZE(decoration_to_string))
      return decoration_to_string[dec];
   else
      return "unknown";
}
