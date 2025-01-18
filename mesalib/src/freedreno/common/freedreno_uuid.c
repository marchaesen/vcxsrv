/*
 * Copyright © 2020 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "freedreno_dev_info.h"
#include "freedreno_uuid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util/mesa-sha1.h"
#include "git_sha1.h"

/* (Re)define UUID_SIZE to avoid including vulkan.h (or p_defines.h) here. */
#define UUID_SIZE 16

void
fd_get_driver_uuid(void *uuid)
{
   const char *driver_id = PACKAGE_VERSION MESA_GIT_SHA1;

   /* The driver UUID is used for determining sharability of images and memory
    * between two Vulkan instances in separate processes, but also to
    * determining memory objects and sharability between Vulkan and OpenGL
    * driver. People who want to share memory need to also check the device
    * UUID.
    */
   struct mesa_sha1 sha1_ctx;
   _mesa_sha1_init(&sha1_ctx);

   _mesa_sha1_update(&sha1_ctx, driver_id, strlen(driver_id));

   uint8_t sha1[SHA1_DIGEST_LENGTH];
   _mesa_sha1_final(&sha1_ctx, sha1);

   assert(SHA1_DIGEST_LENGTH >= UUID_SIZE);
   memcpy(uuid, sha1, UUID_SIZE);
}

void
fd_get_device_uuid(void *uuid, const struct fd_dev_id *id)
{
   struct mesa_sha1 sha1_ctx;
   _mesa_sha1_init(&sha1_ctx);

   /* The device UUID uniquely identifies the given device within the machine.
    * Since we never have more than one device, this doesn't need to be a real
    * UUID, so we use SHA1("freedreno" + gpu_id).
    *
    * @TODO: Using the GPU id could be too restrictive on the off-chance that
    * someone would like to use this UUID to cache pre-tiled images or something
    * of the like, and use them across devices. In the future, we could allow
    * that by:
    * * Being a bit loose about GPU id and hash only the generation's
    * 'major' number (e.g, '6' instead of '630').
    *
    * * Include HW specific constants that are relevant for layout resolving,
    * like minimum width to enable UBWC, tile_align_w, etc.
    *
    * This would allow cached device memory to be safely used from HW in
    * (slightly) different revisions of the same generation.
    */

   static const char *device_name = "freedreno";
   _mesa_sha1_update(&sha1_ctx, device_name, strlen(device_name));

   _mesa_sha1_update(&sha1_ctx, id, sizeof(*id));

   uint8_t sha1[SHA1_DIGEST_LENGTH];
   _mesa_sha1_final(&sha1_ctx, sha1);

   assert(SHA1_DIGEST_LENGTH >= UUID_SIZE);
   memcpy(uuid, sha1, UUID_SIZE);
}
