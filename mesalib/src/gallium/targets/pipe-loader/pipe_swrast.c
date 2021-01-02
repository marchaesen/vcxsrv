
#include "target-helpers/inline_sw_helper.h"
#include "target-helpers/inline_debug_helper.h"
#include "frontend/sw_driver.h"
#include "sw/dri/dri_sw_winsys.h"
#include "sw/kms-dri/kms_dri_sw_winsys.h"
#include "sw/null/null_sw_winsys.h"
#include "sw/wrapper/wrapper_sw_winsys.h"

PUBLIC struct pipe_screen *
swrast_create_screen(struct sw_winsys *ws);

struct pipe_screen *
swrast_create_screen(struct sw_winsys *ws)
{
   struct pipe_screen *screen;

   screen = sw_screen_create(ws);
   if (screen)
      screen = debug_screen_wrap(screen);

   return screen;
}

PUBLIC
const struct sw_driver_descriptor swrast_driver_descriptor = {
   .create_screen = swrast_create_screen,
   .winsys = {
#ifdef HAVE_PIPE_LOADER_DRI
      {
         .name = "dri",
         .create_winsys = dri_create_sw_winsys,
      },
#endif
#ifdef HAVE_PIPE_LOADER_KMS
      {
         .name = "kms_dri",
         .create_winsys = kms_dri_create_winsys,
      },
#endif
      {
         .name = "null",
         .create_winsys = null_sw_create,
      },
      {
         .name = "wrapped",
         .create_winsys = wrapper_sw_winsys_wrap_pipe_screen,
      },
      { 0 },
   }
};
