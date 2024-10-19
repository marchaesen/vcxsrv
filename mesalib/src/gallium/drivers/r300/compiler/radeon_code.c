/*
 * Copyright 2009 Nicolai Haehnle.
 * SPDX-License-Identifier: MIT
 */

#include "radeon_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radeon_program.h"

void
rc_constants_init(struct rc_constant_list *c)
{
   memset(c, 0, sizeof(*c));
}

/**
 * Copy a constants structure, assuming that the destination structure
 * is not initialized.
 */
void
rc_constants_copy(struct rc_constant_list *dst, struct rc_constant_list *src)
{
   dst->Constants = malloc(sizeof(struct rc_constant) * src->Count);
   memcpy(dst->Constants, src->Constants, sizeof(struct rc_constant) * src->Count);
   dst->Count = src->Count;
   dst->_Reserved = src->Count;
}

void
rc_constants_destroy(struct rc_constant_list *c)
{
   free(c->Constants);
   memset(c, 0, sizeof(*c));
}

unsigned
rc_constants_add(struct rc_constant_list *c, struct rc_constant *constant)
{
   unsigned index = c->Count;

   if (c->Count >= c->_Reserved) {
      struct rc_constant *newlist;

      c->_Reserved = c->_Reserved * 2;
      if (!c->_Reserved)
         c->_Reserved = 16;

      newlist = malloc(sizeof(struct rc_constant) * c->_Reserved);
      memcpy(newlist, c->Constants, sizeof(struct rc_constant) * c->Count);

      free(c->Constants);
      c->Constants = newlist;
   }

   c->Constants[index] = *constant;
   c->Count++;

   return index;
}

/**
 * Add a state vector to the constant list, while trying to avoid duplicates.
 */
unsigned
rc_constants_add_state(struct rc_constant_list *c, unsigned state0, unsigned state1)
{
   unsigned index;
   struct rc_constant constant;

   for (index = 0; index < c->Count; ++index) {
      if (c->Constants[index].Type == RC_CONSTANT_STATE) {
         if (c->Constants[index].u.State[0] == state0 && c->Constants[index].u.State[1] == state1)
            return index;
      }
   }

   memset(&constant, 0, sizeof(constant));
   constant.Type = RC_CONSTANT_STATE;
   constant.UseMask = RC_MASK_XYZW;
   constant.u.State[0] = state0;
   constant.u.State[1] = state1;

   return rc_constants_add(c, &constant);
}

/**
 * Add an immediate vector to the constant list, while trying to avoid
 * duplicates.
 */
unsigned
rc_constants_add_immediate_vec4(struct rc_constant_list *c, const float *data)
{
   unsigned index;
   struct rc_constant constant;

   for (index = 0; index < c->Count; ++index) {
      if (c->Constants[index].Type == RC_CONSTANT_IMMEDIATE) {
         if (!memcmp(c->Constants[index].u.Immediate, data, sizeof(float) * 4))
            return index;
      }
   }

   memset(&constant, 0, sizeof(constant));
   constant.Type = RC_CONSTANT_IMMEDIATE;
   constant.UseMask = RC_MASK_XYZW;
   memcpy(constant.u.Immediate, data, sizeof(float) * 4);

   return rc_constants_add(c, &constant);
}

/**
 * Add an immediate scalar to the constant list, while trying to avoid
 * duplicates.
 */
unsigned
rc_constants_add_immediate_scalar(struct rc_constant_list *c, float data, unsigned *swizzle)
{
   unsigned index, free_comp;
   int free_index = -1;
   struct rc_constant constant;

   for (index = 0; index < c->Count; ++index) {
      if (c->Constants[index].Type == RC_CONSTANT_IMMEDIATE) {
         unsigned comp;
         for (comp = 0; comp < 4; ++comp) {
            if (c->Constants[index].UseMask & 1 << comp) {
               if (c->Constants[index].u.Immediate[comp] == data) {
                  *swizzle = RC_MAKE_SWIZZLE_SMEAR(comp);
                  return index;
               }
            } else {
               if (free_index == -1) {
                  free_index = index;
                  free_comp = comp;
               }
            }
         }
      }
   }

   if (free_index >= 0) {
      c->Constants[free_index].u.Immediate[free_comp] = data;
      c->Constants[free_index].UseMask |= 1 << free_comp;
      *swizzle = RC_MAKE_SWIZZLE_SMEAR(free_comp);
      return free_index;
   }

   memset(&constant, 0, sizeof(constant));
   constant.Type = RC_CONSTANT_IMMEDIATE;
   constant.UseMask = RC_MASK_X;
   constant.u.Immediate[0] = data;
   *swizzle = RC_SWIZZLE_XXXX;

   return rc_constants_add(c, &constant);
}

static char
swizzle_char(unsigned swz)
{
   switch (swz) {
   case RC_SWIZZLE_X:
      return 'x';
   case RC_SWIZZLE_Y:
      return 'y';
   case RC_SWIZZLE_Z:
      return 'z';
   case RC_SWIZZLE_W:
      return 'w';
   default:
      return 'u';
   }
}

void
rc_constants_print(struct rc_constant_list *c, struct const_remap *r)
{
   for (unsigned i = 0; i < c->Count; i++) {
      if (c->Constants[i].Type == RC_CONSTANT_IMMEDIATE) {
         float *values = c->Constants[i].u.Immediate;
         fprintf(stderr, "CONST[%u] = {", i);
         for (unsigned chan = 0; chan < 4; chan++) {
            if (c->Constants[i].UseMask & 1 << chan)
               fprintf(stderr, "%11.6f ", values[chan]);
            else
               fprintf(stderr, "     unused ");
         }
         fprintf(stderr, "}\n");
      }
      if (r && c->Constants[i].Type == RC_CONSTANT_EXTERNAL) {
         fprintf(stderr, "CONST[%u] = {", i);
         for (unsigned chan = 0; chan < 4; chan++) {
            fprintf(stderr, "CONST[%i].%c ", r[i].index[chan], swizzle_char(r[i].swizzle[chan]));
         }
         fprintf(stderr, " }\n");
      }
   }
}
