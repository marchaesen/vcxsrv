/*  SPDX-License-Identifier: MIT */

#include "dri_drawable.h"
#include "dri_util.h"

int64_t
kopperSwapBuffers(struct dri_drawable *dPriv, uint32_t flush_flags)
{
   return 0;
}

int64_t
kopperSwapBuffersWithDamage(struct dri_drawable *dPriv, uint32_t flush_flags, int nrects, const int *rects)
{
   return 0;
}

void
kopperSetSwapInterval(struct dri_drawable *dPriv, int interval)
{
}

int
kopperQueryBufferAge(struct dri_drawable *dPriv)
{
   return 0;
}

int
kopperGetSyncValues(struct dri_drawable *drawable, int64_t target_msc, int64_t divisor,
                    int64_t remainder, int64_t *ust, int64_t *msc, int64_t *sbc)
{
   return 0;
}

const struct dri_config **
kopper_init_screen(struct dri_screen *screen, bool driver_name_is_inferred);
const struct dri_config **
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

void
kopper_destroy_drawable(struct dri_drawable *drawable);
void
kopper_destroy_drawable(struct dri_drawable *drawable)
{
}
