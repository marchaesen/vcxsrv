/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include <stdlib.h>
#include <stdio.h>

#include "util/u_pointer.h"
#include "gallivm/lp_bld.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_assert.h"
#include "gallivm/lp_bld_printf.h"

#include "lp_test.h"


struct printf_test_case {
   int foo;
};

void
write_tsv_header(FILE *fp)
{
   fprintf(fp,
           "result\t"
           "format\n");

   fflush(fp);
}



typedef void (*test_printf_t)(int i);


static LLVMValueRef
add_printf_test(struct gallivm_state *gallivm, int n, char *func_name)
{
   LLVMModuleRef module = gallivm->module;
   LLVMTypeRef args[1] = { LLVMIntTypeInContext(gallivm->context, 32) };
   snprintf(func_name, 64 * sizeof(char), "test_lookup_multiple_%d", n);
   LLVMValueRef func = LLVMAddFunction(module, func_name, LLVMFunctionType(LLVMVoidTypeInContext(gallivm->context), args, 1, 0));
   LLVMBuilderRef builder = gallivm->builder;
   LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(gallivm->context, func, "entry");

   LLVMSetFunctionCallConv(func, LLVMCCallConv);

   LLVMPositionBuilderAtEnd(builder, block);
   lp_build_printf(gallivm, "hello, world from ");
   lp_build_printf(gallivm, func_name);
   lp_build_printf(gallivm, "print 5 6: %d %d\n", LLVMConstInt(LLVMInt32TypeInContext(gallivm->context), 5, 0),
				LLVMConstInt(LLVMInt32TypeInContext(gallivm->context), 6, 0));

   /* Also test lp_build_assert().  This should not fail. */
   lp_build_assert(gallivm, LLVMConstInt(LLVMInt32TypeInContext(gallivm->context), 1, 0), "assert(1)");

   LLVMBuildRetVoid(builder);

   gallivm_verify_function(gallivm, func);

   return func;
}


static bool
test_lookup_multiple(unsigned verbose, FILE *fp,
            const struct printf_test_case *testcase)
{
#define NUM_FUNC 10
   struct gallivm_state *gallivm;
   LLVMValueRef *func =
      (LLVMValueRef *) malloc(NUM_FUNC * sizeof(LLVMValueRef));
   char func_name[NUM_FUNC][64];
   test_printf_t *test_lookup_multiple_func =
      (test_printf_t *)malloc(NUM_FUNC * sizeof(test_printf_t));
   bool success = true;
   int i;

   lp_context_ref context;
   lp_context_create(&context);

   gallivm = gallivm_create("test_module", &context, NULL);

   for(i = 0; i < NUM_FUNC; i++){
      func[i] = add_printf_test(gallivm, i, func_name[i]);
   }

   gallivm_compile_module(gallivm);

   for(i = 0; i < NUM_FUNC; i++){
      test_lookup_multiple_func[i] = (test_printf_t) gallivm_jit_function(gallivm, func[i], func_name[i]);
   }

   gallivm_free_ir(gallivm);

   for(i = 0; i < NUM_FUNC; i++){
      test_lookup_multiple_func[i](0);
   }
   FREE(func);
   FREE(test_lookup_multiple_func);
   gallivm_destroy(gallivm);
   lp_context_destroy(&context);
   return success;
}


bool
test_all(unsigned verbose, FILE *fp)
{
   bool success = true;

   test_lookup_multiple(verbose, fp, NULL);

   return success;
}


bool
test_some(unsigned verbose, FILE *fp,
          unsigned long n)
{
   return test_all(verbose, fp);
}


bool
test_single(unsigned verbose, FILE *fp)
{
   printf("no test_single()");
   return true;
}
