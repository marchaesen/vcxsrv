/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* The purpose of this file is to abstract the differences between the Windows
 * C ABI for D3D12 and the Linux ABI. Essentially, for class methods that return
 * structures, the MSVC C++ ABI specifies that they are always called with the return
 * structure allocated by the caller, and passed as a hidden second parameter,
 * after "this". But the C compiler doesn't apply that automatically to the C
 * equivalent definition of the method, and so that ABI needs to be explicitly
 * embedded in the C function signature. For Linux, no such ABI difference between
 * C and C++ exists, and so C callers should use the same signature as C++.
 */

#ifndef DZN_ABI_HELPER_H
#define DZN_ABI_HELPER_H

static inline D3D12_HEAP_PROPERTIES
dzn_ID3D12Device2_GetCustomHeapProperties(ID3D12Device2 *dev, UINT node_mask, D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES ret;
#ifdef _WIN32
    ID3D12Device2_GetCustomHeapProperties(dev, &ret, node_mask, type);
#elif D3D12_SDK_VERSION >= 606
    ret = ID3D12Device2_GetCustomHeapProperties(dev, node_mask, type);
#else
    ret = ((D3D12_HEAP_PROPERTIES (STDMETHODCALLTYPE *)(ID3D12Device2 *, UINT, D3D12_HEAP_TYPE))dev->lpVtbl->GetCustomHeapProperties)(dev, node_mask, type);
#endif
    return ret;
}

static inline D3D12_RESOURCE_ALLOCATION_INFO
dzn_ID3D12Device2_GetResourceAllocationInfo(ID3D12Device2 *dev, UINT visible_mask, UINT num_resource_descs, const D3D12_RESOURCE_DESC *resource_descs)
{
    D3D12_RESOURCE_ALLOCATION_INFO ret;
#ifdef _WIN32
    ID3D12Device2_GetResourceAllocationInfo(dev, &ret, visible_mask, num_resource_descs, resource_descs);
#elif D3D12_SDK_VERSION >= 606
    ret = ID3D12Device2_GetResourceAllocationInfo(dev, visible_mask, num_resource_descs, resource_descs);
#else
    ret = ((D3D12_RESOURCE_ALLOCATION_INFO (STDMETHODCALLTYPE *)(ID3D12Device2 *, UINT, UINT, const D3D12_RESOURCE_DESC *))
        dev->lpVtbl->GetResourceAllocationInfo)(dev, visible_mask, num_resource_descs, resource_descs);
#endif
    return ret;
}

static inline D3D12_RESOURCE_DESC
dzn_ID3D12Resource_GetDesc(ID3D12Resource *res)
{
    D3D12_RESOURCE_DESC ret;
#ifdef _WIN32
    ID3D12Resource_GetDesc(res, &ret);
#elif D3D12_SDK_VERSION >= 606
    ret = ID3D12Resource_GetDesc(res);
#else
    ret = ((D3D12_RESOURCE_DESC (STDMETHODCALLTYPE *)(ID3D12Resource *))res->lpVtbl->GetDesc)(res);
#endif
    return ret;
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE
dzn_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *heap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE ret;
#ifdef _WIN32
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &ret);
#elif D3D12_SDK_VERSION >= 606
    ret = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
#else
    ret = ((D3D12_CPU_DESCRIPTOR_HANDLE (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap *))heap->lpVtbl->GetCPUDescriptorHandleForHeapStart)(heap);
#endif
    return ret;
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE
dzn_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *heap)
{
    D3D12_GPU_DESCRIPTOR_HANDLE ret;
#ifdef _WIN32
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &ret);
#elif D3D12_SDK_VERSION >= 606
    ret = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
#else
    ret = ((D3D12_GPU_DESCRIPTOR_HANDLE (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap *))heap->lpVtbl->GetGPUDescriptorHandleForHeapStart)(heap);
#endif
    return ret;
}

#endif /*DZN_ABI_HELPER_H*/
