/*  SPDX-License-Identifier: MIT */

#include "dri_util.h"

int64_t
kopperSwapBuffers(__DRIdrawable *dPriv, uint32_t flush_flags)
{
   return 0;
}

int64_t
kopperSwapBuffersWithDamage(__DRIdrawable *dPriv, uint32_t flush_flags, int nrects, const int *rects)
{
   return 0;
}

void
kopperSetSwapInterval(__DRIdrawable *dPriv, int interval)
{
}

int
kopperQueryBufferAge(__DRIdrawable *dPriv)
{
   return 0;
}

const __DRIconfig **
kopper_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);
const __DRIconfig **
kopper_init_screen(struct dri_screen *screen, bool driver_name_is_inferred)
{
   return NULL;
}

struct dri_drawable;
void
kopper_init_drawable(struct dri_drawable *drawable, bool isPixmap, int alphaBits);
void
kopper_init_drawable(struct dri_drawable *drawable, bool isPixmap, int alphaBits)
{
}
