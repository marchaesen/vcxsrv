/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 *
 * This file contains helpers that are implemented in C so that, among other
 * things, we avoid pulling in all of libc as bindings only to access a few
 * functions.
 */

#include <stdio.h>
#include <stdlib.h>

#include "rust_helpers.h"

void compiler_rs_free(void *ptr)
{
   free(ptr);
}

long compiler_rs_ftell(FILE *f)
{
   return ftell(f);
}

int compiler_rs_fseek(FILE *f, long offset, int whence)
{
   return fseek(f, offset, whence);
}

size_t compiler_rs_fwrite(const void *ptr,
                          size_t size, size_t nmemb,
                          FILE *stream)
{
   return fwrite(ptr, size, nmemb, stream);
}
