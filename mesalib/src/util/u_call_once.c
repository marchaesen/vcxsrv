/*
 * Copyright 2022 Yonggang Luo
 * SPDX-License-Identifier: MIT
 */

#include "u_call_once.h"

struct util_call_once_context_t
{
   void *context;
   util_call_once_callback_t callback;
};

static thread_local struct util_call_once_context_t call_once_context;

static void util_call_once_with_context_callback(void)
{
   struct util_call_once_context_t *once_context = &call_once_context;
   once_context->callback(once_context->context);
}

void util_call_once_with_context(once_flag *once, void *context, util_call_once_callback_t callback)
{
   struct util_call_once_context_t *once_context = &call_once_context;
   once_context->context = context;
   once_context->callback = callback;
   call_once(once, util_call_once_with_context_callback);
}
