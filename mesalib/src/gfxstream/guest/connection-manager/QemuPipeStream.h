/*
 * Copyright 2011 Google LLC
 * SPDX-License-Identifier: MIT
 */
#ifndef __QEMU_PIPE_STREAM_H
#define __QEMU_PIPE_STREAM_H

/* This file implements an IOStream that uses a QEMU fast-pipe
 * to communicate with the emulator's 'opengles' service. See
 * <hardware/qemu_pipe.h> for more details.
 */
#include <stdlib.h>

#include <memory>

#include "gfxstream/guest/IOStream.h"

class QemuPipeStream : public gfxstream::guest::IOStream {
   public:
    typedef enum { ERR_INVALID_SOCKET = -1000 } QemuPipeStreamError;

    explicit QemuPipeStream(size_t bufsize = 10000);
    ~QemuPipeStream();

    virtual int connect(const char* serviceName = nullptr);
    virtual uint64_t processPipeInit();

    virtual void* allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char* readFully(void* buf, size_t len);
    virtual const unsigned char* commitBufferAndReadFully(size_t size, void* buf, size_t len);
    virtual const unsigned char* read(void* buf, size_t* inout_len);

    bool valid();
    int recv(void* buf, size_t len);

    virtual int writeFully(const void* buf, size_t len);

   private:
    int m_sock;
    size_t m_bufsize;
    unsigned char* m_buf;
    size_t m_read;
    size_t m_readLeft;
    QemuPipeStream(int sock, size_t bufSize);
};

#endif
