/*
 * Copyright © 2020 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_UUID_H__
#define __FREEDRENO_UUID_H__

#ifdef __cplusplus
extern "C" {
#endif

struct fd_dev_id;

void fd_get_driver_uuid(void *uuid);
void fd_get_device_uuid(void *uuid, const struct fd_dev_id *id);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* __FREEDRENO_UUID_H__ */
