
#ifndef _DRM_DRIVER_H_
#define _DRM_DRIVER_H_

#include "pipe/p_compiler.h"

#include "winsys_handle.h"

struct pipe_screen;
struct pipe_screen_config;
struct pipe_context;
struct pipe_resource;

/**
 * Configuration queries.
 */
enum drm_conf {
   /* How many frames to allow before throttling. Or -1 to indicate any number */
   DRM_CONF_THROTTLE, /* DRM_CONF_INT. */
   /* Can this driver, running on this kernel, import and export dma-buf fds? */
   DRM_CONF_SHARE_FD, /* DRM_CONF_BOOL. */
   /* XML string describing the available config options. */
   DRM_CONF_XML_OPTIONS, /* DRM_CONF_POINTER */
   DRM_CONF_MAX
};

/**
 * Type of configuration answer
 */
enum drm_conf_type {
   DRM_CONF_INT,
   DRM_CONF_BOOL,
   DRM_CONF_FLOAT,
   DRM_CONF_POINTER
};

/**
 * Return value from the configuration function.
 */
struct drm_conf_ret {
   enum drm_conf_type type;
   union {
      int val_int;
      bool val_bool;
      float val_float;
      void *val_pointer;
   } val;
};

struct drm_driver_descriptor
{
   /**
    * Identifying prefix/suffix of the binary, used by the pipe-loader.
    */
   const char *driver_name;

   /**
    * Create a pipe srcreen.
    *
    * This function does any wrapping of the screen.
    * For example wrapping trace or rbug debugging drivers around it.
    */
   struct pipe_screen* (*create_screen)(int drm_fd,
                                        const struct pipe_screen_config *config);

   /**
    * Return a configuration value.
    *
    * If this function is NULL, or if it returns NULL
    * the state tracker- or state
    * tracker manager should provide a reasonable default value.
    */
   const struct drm_conf_ret *(*configuration) (enum drm_conf conf);
};

extern const struct drm_driver_descriptor driver_descriptor;

/**
 * Instantiate a drm_driver_descriptor struct.
 */
#define DRM_DRIVER_DESCRIPTOR(driver_name_str, func, conf) \
const struct drm_driver_descriptor driver_descriptor = {       \
   .driver_name = driver_name_str,                             \
   .create_screen = func,                                      \
   .configuration = (conf),				       \
};

#endif
