
#ifndef _DRM_DRIVER_H_
#define _DRM_DRIVER_H_

#include "pipe/p_compiler.h"

#include "winsys_handle.h"

struct pipe_screen;
struct pipe_screen_config;
struct pipe_context;
struct pipe_resource;

struct drm_driver_descriptor
{
   /**
    * Identifying prefix/suffix of the binary, used by the pipe-loader.
    */
   const char *driver_name;

   /**
    * Pointer to the XML string describing driver-specific driconf options.
    * Use DRI_CONF_* macros to create the string.
    */
   const char **driconf_xml;

   /**
    * Create a pipe srcreen.
    *
    * This function does any wrapping of the screen.
    * For example wrapping trace or rbug debugging drivers around it.
    */
   struct pipe_screen* (*create_screen)(int drm_fd,
                                        const struct pipe_screen_config *config);
};

extern const struct drm_driver_descriptor driver_descriptor;

/**
 * Instantiate a drm_driver_descriptor struct.
 */
#define DRM_DRIVER_DESCRIPTOR(driver_name_str, driconf, func)  \
const struct drm_driver_descriptor driver_descriptor = {       \
   .driver_name = driver_name_str,                             \
   .driconf_xml = driconf,                                     \
   .create_screen = func,                                      \
};

#endif
