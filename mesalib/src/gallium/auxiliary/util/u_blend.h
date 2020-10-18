#ifndef U_BLEND_H
#define U_BLEND_H

#include "pipe/p_state.h"
#include "compiler/shader_enums.h"

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

/** To lower blending to software shaders, the Gallium blend mode has to
 * be translated to something API-agnostic, as defined in shader_enums.h
 * */

static inline enum blend_func
util_blend_func_to_shader(enum pipe_blend_func func)
{
   switch (func) {
      case PIPE_BLEND_ADD:
         return BLEND_FUNC_ADD;
      case PIPE_BLEND_SUBTRACT:
         return BLEND_FUNC_SUBTRACT;
      case PIPE_BLEND_REVERSE_SUBTRACT:
         return BLEND_FUNC_REVERSE_SUBTRACT;
      case PIPE_BLEND_MIN:
         return BLEND_FUNC_MIN;
      case PIPE_BLEND_MAX:
         return BLEND_FUNC_MAX;
      default:
         unreachable("Invalid blend function");
   }
}

static inline enum blend_factor
util_blend_factor_to_shader(enum pipe_blendfactor factor)
{
   switch (factor) {
      case PIPE_BLENDFACTOR_ZERO:
      case PIPE_BLENDFACTOR_ONE:
         return BLEND_FACTOR_ZERO;

      case PIPE_BLENDFACTOR_SRC_COLOR:
      case PIPE_BLENDFACTOR_INV_SRC_COLOR:
         return BLEND_FACTOR_SRC_COLOR;

      case PIPE_BLENDFACTOR_SRC_ALPHA:
      case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
         return BLEND_FACTOR_SRC_ALPHA;

      case PIPE_BLENDFACTOR_DST_ALPHA:
      case PIPE_BLENDFACTOR_INV_DST_ALPHA:
         return BLEND_FACTOR_DST_ALPHA;

      case PIPE_BLENDFACTOR_DST_COLOR:
      case PIPE_BLENDFACTOR_INV_DST_COLOR:
         return BLEND_FACTOR_DST_COLOR;

      case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
         return BLEND_FACTOR_SRC_ALPHA_SATURATE;

      case PIPE_BLENDFACTOR_CONST_COLOR:
      case PIPE_BLENDFACTOR_INV_CONST_COLOR:
         return BLEND_FACTOR_CONSTANT_COLOR;

      case PIPE_BLENDFACTOR_CONST_ALPHA:
      case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
         return BLEND_FACTOR_CONSTANT_ALPHA;

      case PIPE_BLENDFACTOR_SRC1_COLOR:
      case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
         return BLEND_FACTOR_SRC1_COLOR;

      case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      case PIPE_BLENDFACTOR_SRC1_ALPHA:
         return BLEND_FACTOR_SRC1_ALPHA;

      default:
         unreachable("Invalid factor");
   }
}

static inline bool
util_blend_factor_is_inverted(enum pipe_blendfactor factor)
{
   switch (factor) {
      case PIPE_BLENDFACTOR_ONE:
      case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      case PIPE_BLENDFACTOR_INV_DST_COLOR:
      case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
         return true;

      default:
         return false;
   }
}

/* To determine if the destination needs to be read while blending */

static inline bool
util_blend_factor_uses_dest(enum pipe_blendfactor factor, bool alpha)
{
   switch (factor) {
      case PIPE_BLENDFACTOR_DST_ALPHA:
      case PIPE_BLENDFACTOR_DST_COLOR:
      case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      case PIPE_BLENDFACTOR_INV_DST_COLOR:
         return true;
      case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
         return !alpha;
      default:
         return false;
   }
}

static inline bool
util_blend_uses_dest(struct pipe_rt_blend_state rt)
{
   return rt.blend_enable &&
      (util_blend_factor_uses_dest(rt.rgb_src_factor, false) ||
       util_blend_factor_uses_dest(rt.alpha_src_factor, true) ||
       rt.rgb_dst_factor != PIPE_BLENDFACTOR_ZERO ||
       rt.alpha_dst_factor != PIPE_BLENDFACTOR_ZERO);
}

#endif /* U_BLEND_H */
