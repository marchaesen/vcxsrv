#ifndef U_BLEND_H
#define U_BLEND_H

#include "pipe/p_state.h"

/**
 * When faking RGBX render target formats with RGBA ones, the blender is still
 * supposed to treat the destination's alpha channel as 1 instead of the
 * garbage that's there. Return a blend factor that will take that into
 * account.
 */
static inline int
util_blend_dst_alpha_to_one(int factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_DST_ALPHA:
      return PIPE_BLENDFACTOR_ONE;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      return PIPE_BLENDFACTOR_ZERO;
   default:
      return factor;
   }
}

#endif /* U_BLEND_H */
