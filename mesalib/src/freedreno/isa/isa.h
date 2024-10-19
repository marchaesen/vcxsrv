/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef _ISA_H_
#define _ISA_H_

#include "compiler/isaspec/isaspec.h"

struct ir3_shader_variant;
void * isa_assemble(struct ir3_shader_variant *v);

#endif /* _ISA_H_ */
