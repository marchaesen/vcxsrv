
#ifndef _SW_DRIVER_H_
#define _SW_DRIVER_H_

#include "pipe/p_compiler.h"

struct pipe_screen;
struct sw_winsys;

struct sw_driver_descriptor
{
   struct pipe_screen *(*create_screen)(struct sw_winsys *ws, bool sw_vk);
   struct {
       const char * const name;
       struct sw_winsys *(*create_winsys)();
   } winsys[];
};

#endif
