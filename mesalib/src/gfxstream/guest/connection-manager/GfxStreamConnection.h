/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef GFXSTREAM_CONNECTION_H
#define GFXSTREAM_CONNECTION_H

class GfxStreamConnection {
   public:
    GfxStreamConnection();
    virtual ~GfxStreamConnection();

    virtual void* getEncoder() = 0;
};

#endif
