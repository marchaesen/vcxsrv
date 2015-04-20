/**************************************************************************
 *
 * Copyright 2014 VMware, Inc.
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


/* Force assertions, even on debug builds. */
#undef NDEBUG


#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "u_atomic.h"

#ifdef _MSC_VER
#pragma warning( disable : 28112 ) /* Accessing a local variable via an Interlocked function */
#pragma warning( disable : 28113 ) /* A variable which is accessed via an Interlocked function must always be accessed via an Interlocked function */
#endif


/* Test only assignment-like operations, which are supported on all types */
#define test_atomic_assign(type, ones) \
   static void test_atomic_assign_##type (void) { \
      type v, r; \
      \
      p_atomic_set(&v, ones); \
      assert(v == ones && "p_atomic_set"); \
      \
      r = p_atomic_read(&v); \
      assert(r == ones && "p_atomic_read"); \
      \
      v = ones; \
      r = p_atomic_cmpxchg(&v, 0, 1); \
      assert(v == ones && "p_atomic_cmpxchg"); \
      assert(r == ones && "p_atomic_cmpxchg"); \
      r = p_atomic_cmpxchg(&v, ones, 0); \
      assert(v == 0 && "p_atomic_cmpxchg"); \
      assert(r == ones && "p_atomic_cmpxchg"); \
      \
      (void) r; \
   }


/* Test arithmetic operations that are supported on 8 bits integer types */
#define test_atomic_8bits(type, ones) \
   test_atomic_assign(type, ones) \
   \
   static void test_atomic_8bits_##type (void) { \
      type v, r; \
      \
      test_atomic_assign_##type(); \
      \
      v = 23; \
      p_atomic_add(&v, 42); \
      r = p_atomic_read(&v); \
      assert(r == 65 && "p_atomic_add"); \
      \
      (void) r; \
   }


/* Test all operations */
#define test_atomic(type, ones) \
   test_atomic_8bits(type, ones) \
   \
   static void test_atomic_##type (void) { \
      type v, r; \
      bool b; \
      \
      test_atomic_8bits_##type(); \
      \
      v = 2; \
      b = p_atomic_dec_zero(&v); \
      assert(v == 1 && "p_atomic_dec_zero"); \
      assert(b == false && "p_atomic_dec_zero"); \
      b = p_atomic_dec_zero(&v); \
      assert(v == 0 && "p_atomic_dec_zero"); \
      assert(b == true && "p_atomic_dec_zero"); \
      b = p_atomic_dec_zero(&v); \
      assert(v == ones && "p_atomic_dec_zero"); \
      assert(b == false && "p_atomic_dec_zero"); \
      \
      v = ones; \
      p_atomic_inc(&v); \
      assert(v == 0 && "p_atomic_inc"); \
      \
      v = ones; \
      r = p_atomic_inc_return(&v); \
      assert(v == 0 && "p_atomic_inc_return"); \
      assert(r == v && "p_atomic_inc_return"); \
      \
      v = 0; \
      p_atomic_dec(&v); \
      assert(v == ones && "p_atomic_dec"); \
      \
      v = 0; \
      r = p_atomic_dec_return(&v); \
      assert(v == ones && "p_atomic_dec_return"); \
      assert(r == v && "p_atomic_dec_return"); \
      \
      (void) r; \
      (void) b; \
   }


test_atomic(int, -1)
test_atomic(unsigned, ~0U)

test_atomic(int16_t, INT16_C(-1))
test_atomic(uint16_t, UINT16_C(0xffff))
test_atomic(int32_t, INT32_C(-1))
test_atomic(uint32_t, UINT32_C(0xffffffff))
test_atomic(int64_t, INT64_C(-1))
test_atomic(uint64_t, UINT64_C(0xffffffffffffffff))

test_atomic_8bits(int8_t, INT8_C(-1))
test_atomic_8bits(uint8_t, UINT8_C(0xff))
test_atomic_assign(bool, true)

int
main()
{
   test_atomic_int();
   test_atomic_unsigned();

   test_atomic_int16_t();
   test_atomic_uint16_t();
   test_atomic_int32_t();
   test_atomic_uint32_t();
   test_atomic_int64_t();
   test_atomic_uint64_t();

   test_atomic_8bits_int8_t();
   test_atomic_8bits_uint8_t();
   test_atomic_assign_bool();

   return 0;
}
