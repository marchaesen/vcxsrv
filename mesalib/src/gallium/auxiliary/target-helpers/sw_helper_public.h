#ifndef _SW_HELPER_PUBLIC_H
#define _SW_HELPER_PUBLIC_H

struct pipe_screen;
struct sw_winsys;

struct pipe_screen *
sw_screen_create(struct sw_winsys *winsys);

#endif /* _SW_HELPER_PUBLIC_H */
