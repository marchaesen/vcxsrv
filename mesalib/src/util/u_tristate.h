/*
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include <stdbool.h>

#ifndef U_TRISTATE_H
#define U_TRISTATE_H

/*
 * Simple tri-state data structure.
 *
 * The tri-state can be set to a boolean or unset. The semantics of "unset"
 * depend on the application, it could be either "don't care" or "maybe".
 */
enum u_tristate {
   U_TRISTATE_UNSET,
   U_TRISTATE_NO,
   U_TRISTATE_YES,
};

/*
 * Construct a tristate from an immediate value.
 */
static inline enum u_tristate
u_tristate_make(bool value)
{
   return value ? U_TRISTATE_YES : U_TRISTATE_NO;
}

/*
 * Try to set a tristate value to a specific boolean value, returning whether
 * the operation is successful.
 */
static inline bool
u_tristate_set(enum u_tristate *state, bool value)
{
   switch (*state) {
   case U_TRISTATE_UNSET:
      *state = u_tristate_make(value);
      return true;

   case U_TRISTATE_NO:
      return (value == false);

   case U_TRISTATE_YES:
      return (value == true);

   default:
      unreachable("Invalid tristate value");
   }
}

/*
 * Invert a tristate, returning the new value.
 */
static inline enum u_tristate
u_tristate_invert(enum u_tristate tri)
{
   switch (tri) {
   case U_TRISTATE_UNSET: return U_TRISTATE_UNSET;
   case U_TRISTATE_YES: return U_TRISTATE_NO;
   case U_TRISTATE_NO: return U_TRISTATE_YES;
   }

   unreachable("invalid tristate");
}

#endif
