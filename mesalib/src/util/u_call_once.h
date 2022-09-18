/*
 * Copyright 2022 Yonggang Luo
 * SPDX-License-Identifier: MIT
 *
 * Extend C11 call_once to support context parameter
 */

#ifndef U_CALL_ONCE_H_
#define U_CALL_ONCE_H_

#include <stdbool.h>

#include "c11/threads.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*util_call_once_callback_t)(void *context);

void util_call_once_with_context(once_flag *once, void *context, util_call_once_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* U_CALL_ONCE_H_ */
