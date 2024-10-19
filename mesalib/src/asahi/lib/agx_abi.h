/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* See compiler/README.md for the ABI */

#define AGX_ABI_VIN_ATTRIB(i)   (2 * (8 + i))
#define AGX_ABI_VIN_VERTEX_ID   (2 * 5)
#define AGX_ABI_VIN_INSTANCE_ID (2 * 6)

#define AGX_ABI_FIN_SAMPLE_MASK (2)

#define AGX_ABI_FOUT_SAMPLE_MASK   (2)
#define AGX_ABI_FOUT_Z             (4)
#define AGX_ABI_FOUT_S             (6)
#define AGX_ABI_FOUT_WRITE_SAMPLES (7)
#define AGX_ABI_FOUT_COLOUR(rt)    (2 * (4 + (4 * rt)))
