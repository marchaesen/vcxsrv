/*
 * Copyright © 2021 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <cstdio>

#include "dxil_validation.h"

#if DETECT_OS_WINDOWS

#include <windows.h>
#include <wrl/client.h>

#include "dxcapi.h"

using Microsoft::WRL::ComPtr;

class DxilBlob : public IDxcBlob {
 public:
   DxilBlob(dxil_spirv_object *data) : m_data(data) {}

   LPVOID STDMETHODCALLTYPE
   GetBufferPointer() override
   {
      return m_data->binary.buffer;
   }

   SIZE_T STDMETHODCALLTYPE
   GetBufferSize() override
   {
      return m_data->binary.size;
   }

   HRESULT STDMETHODCALLTYPE
   QueryInterface(REFIID, void **) override
   {
      return E_NOINTERFACE;
   }

   ULONG STDMETHODCALLTYPE
   AddRef() override
   {
      return 1;
   }

   ULONG STDMETHODCALLTYPE
   Release() override
   {
      return 0;
   }

   dxil_spirv_object *m_data;
};

bool
validate_dxil(dxil_spirv_object *dxil_obj)
{
   HMODULE dxil_dll = LoadLibraryA("dxil.dll");
   if (!dxil_dll) {
      fprintf(stderr, "Unable to load dxil.dll\n");
      return false;
   }
   DxcCreateInstanceProc dxc_create_instance =
      reinterpret_cast<DxcCreateInstanceProc>(
         GetProcAddress(dxil_dll, "DxcCreateInstance"));

   bool res = false;
   DxilBlob blob(dxil_obj);
   // Creating a block so that ComPtrs free before we call FreeLibrary
   {
      ComPtr<IDxcValidator> validator;
      if (FAILED(dxc_create_instance(CLSID_DxcValidator,
                                     IID_PPV_ARGS(&validator)))) {
         fprintf(stderr, "Failed to create DxcValidator instance \n");
         FreeLibrary(dxil_dll);
         return false;
      }

      ComPtr<IDxcOperationResult> result;
      validator->Validate(&blob, DxcValidatorFlags_InPlaceEdit, &result);
      HRESULT status;
      result->GetStatus(&status);
      if (FAILED(status)) {
         ComPtr<IDxcBlobEncoding> error;
         result->GetErrorBuffer(&error);
         BOOL known = false;
         uint32_t cp = 0;
         error->GetEncoding(&known, &cp);
         fprintf(stderr, "DXIL: ");
         if (cp == CP_UTF8 || cp == CP_ACP) {
            fprintf(stderr, "%s\n",
                    static_cast<char *>(error->GetBufferPointer()));
         } else {
            fwprintf(stderr, L"%ls\n",
                     static_cast<wchar_t *>(error->GetBufferPointer()));
         }
      } else {
         res = true;
      }
   }

   FreeLibrary(dxil_dll);
   return res;
}

#else

bool
validate_dxil(dxil_spirv_object *dxil_obj)
{
   fprintf(stderr, "DXIL validation only available in Windows.\n");
   return false;
}

#endif
