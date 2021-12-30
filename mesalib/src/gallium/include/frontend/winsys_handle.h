
#ifndef _WINSYS_HANDLE_H_
#define _WINSYS_HANDLE_H_

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define WINSYS_HANDLE_TYPE_SHARED 0
#define WINSYS_HANDLE_TYPE_KMS    1
#define WINSYS_HANDLE_TYPE_FD     2
/* Win32 handles serve the same purpose as FD, just on Windows, so alias the value */
#define WINSYS_HANDLE_TYPE_WIN32_HANDLE WINSYS_HANDLE_TYPE_FD
#define WINSYS_HANDLE_TYPE_SHMID   3
#define WINSYS_HANDLE_TYPE_D3D12_RES 4

/**
 * For use with pipe_screen::{texture_from_handle|texture_get_handle}.
 */
struct winsys_handle
{
   /**
    * Input for texture_from_handle, valid values are
    * WINSYS_HANDLE_TYPE_SHARED or WINSYS_HANDLE_TYPE_FD.
    * Input to texture_get_handle,
    * to select handle for kms, flink, or prime.
    */
   unsigned type;
   /**
    * Input for texture_get_handle, allows to export the offset
    * of a specific layer of an array texture.
    */
   unsigned layer;
   /**
    * Input for texture_get_handle, allows to export of a specific plane of a
    * texture.
    */
   unsigned plane;
   /**
    * Input to texture_from_handle.
    * Output for texture_get_handle.
    */
#ifdef _WIN32
   HANDLE handle;
#else
   unsigned handle;
#endif
   /**
    * Input to texture_from_handle.
    * Output for texture_get_handle.
    */
   unsigned stride;
   /**
    * Input to texture_from_handle.
    * Output for texture_get_handle.
    */
   unsigned offset;

   /**
    * Input to resource_from_handle.
    * Output from resource_get_handle.
    */
   uint64_t format;

   /**
    * Input to resource_from_handle.
    * Output from resource_get_handle.
    */
   uint64_t modifier;

   /**
    * Input to resource_from_handle.
    * Output for resource_get_handle.
    */
   void *com_obj;
};

#ifdef __cplusplus
}
#endif

#endif /* _WINSYS_HANDLE_H_ */
