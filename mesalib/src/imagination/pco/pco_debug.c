/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_debug.c
 *
 * \brief Debug-related functions.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/macros.h"
#include "util/u_call_once.h"
#include "util/u_debug.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const struct debug_named_value pco_debug_options[] = {
   { "val_skip", PCO_DEBUG_VAL_SKIP, "Skip IR validation." },
   { "reindex", PCO_DEBUG_REINDEX, "Reindex IR at the end of each pass." },
   DEBUG_NAMED_VALUE_END,
};

static const struct debug_named_value pco_debug_print_options[] = {
   { "vs", PCO_DEBUG_PRINT_VS, "Print the IR for vertex shaders." },
   { "fs", PCO_DEBUG_PRINT_FS, "Print the IR for fragment shaders." },
   { "cs", PCO_DEBUG_PRINT_CS, "Print the IR for compute shaders." },
   { "all", PCO_DEBUG_PRINT_ALL, "Print the IR for all shaders." },
   { "internal",
     PCO_DEBUG_PRINT_INTERNAL,
     "Print the IR for internal shader types." },
   { "passes", PCO_DEBUG_PRINT_PASSES, "Print the IR after each pass." },
   { "nir", PCO_DEBUG_PRINT_NIR, "Print the resulting NIR." },
   { "binary", PCO_DEBUG_PRINT_BINARY, "Print the resulting binary." },
   { "verbose", PCO_DEBUG_PRINT_VERBOSE, "Print verbose IR." },
   { "ra", PCO_DEBUG_PRINT_RA, "Print register alloc info." },
   DEBUG_NAMED_VALUE_END,
};

DEBUG_GET_ONCE_FLAGS_OPTION(pco_debug, "PCO_DEBUG", pco_debug_options, 0U)
uint64_t pco_debug = 0U;

DEBUG_GET_ONCE_FLAGS_OPTION(pco_debug_print,
                            "PCO_DEBUG_PRINT",
                            pco_debug_print_options,
                            0U)
uint64_t pco_debug_print = 0U;

DEBUG_GET_ONCE_OPTION(pco_skip_passes, "PCO_SKIP_PASSES", "")
const char *pco_skip_passes = "";

DEBUG_GET_ONCE_OPTION(pco_color, "PCO_COLOR", NULL)
bool pco_color = false;

static void pco_debug_init_once(void)
{
   /* Get debug flags. */
   pco_debug = debug_get_option_pco_debug();
   pco_debug_print = debug_get_option_pco_debug_print();
   pco_skip_passes = debug_get_option_pco_skip_passes();

   /* Get/parse color option. */
   const char *color_opt = debug_get_option_pco_color();
   if (!color_opt || !strcmp(color_opt, "auto") || !strcmp(color_opt, "a"))
      pco_color = isatty(fileno(stdout));
   else if (!strcmp(color_opt, "on") || !strcmp(color_opt, "1"))
      pco_color = true;
   else if (!strcmp(color_opt, "off") || !strcmp(color_opt, "0"))
      pco_color = false;
}

void pco_debug_init(void)
{
   static util_once_flag flag = UTIL_ONCE_FLAG_INIT;
   util_call_once(&flag, pco_debug_init_once);
}
