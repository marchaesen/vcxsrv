/*
 * Copyright 2011 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "QemuPipeStream.h"

#include <errno.h>
#include <qemu_pipe_bp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/log.h"

static const size_t kReadSize = 512 * 1024;
static const size_t kWriteOffset = kReadSize;

QemuPipeStream::QemuPipeStream(size_t bufSize)
    : IOStream(bufSize), m_sock(-1), m_bufsize(bufSize), m_buf(NULL), m_read(0), m_readLeft(0) {}

QemuPipeStream::QemuPipeStream(QEMU_PIPE_HANDLE sock, size_t bufSize)
    : IOStream(bufSize), m_sock(sock), m_bufsize(bufSize), m_buf(NULL), m_read(0), m_readLeft(0) {}

QemuPipeStream::~QemuPipeStream() {
    if (valid()) {
        flush();
        qemu_pipe_close((QEMU_PIPE_HANDLE)m_sock);
    }
    if (m_buf != NULL) {
        free(m_buf);
    }
}

int QemuPipeStream::connect(const char* serviceName) {
    m_sock = (int)qemu_pipe_open("opengles");
    if (!valid()) {
        mesa_loge("%s: failed to connect to opengles pipe", __FUNCTION__);
        qemu_pipe_print_error(m_sock);
        return -1;
    }
    return 0;
}

uint64_t QemuPipeStream::processPipeInit() {
    QEMU_PIPE_HANDLE processPipe = qemu_pipe_open("GLProcessPipe");

    uint64_t procUID = 0;
    if (!qemu_pipe_valid(processPipe)) {
        processPipe = 0;
        mesa_logi("Process pipe failed");
        return 0;
    }

    // Send a confirmation int to the host
    int32_t confirmInt = 100;
    if (qemu_pipe_write_fully(processPipe, &confirmInt, sizeof(confirmInt))) {  // failed
        qemu_pipe_close(processPipe);
        processPipe = 0;
        mesa_logi("Process pipe failed");
        return 0;
    }

    // Ask the host for per-process unique ID
    if (qemu_pipe_read_fully(processPipe, &procUID, sizeof(procUID))) {
        qemu_pipe_close(processPipe);
        processPipe = 0;
        procUID = 0;
        mesa_logi("Process pipe failed");
        return 0;
    }

    return procUID;
}

void* QemuPipeStream::allocBuffer(size_t minSize) {
    // Add dedicated read buffer space at the front of the buffer.
    minSize += kReadSize;

    size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
    if (!m_buf) {
        m_buf = (unsigned char*)malloc(allocSize);
    } else if (m_bufsize < allocSize) {
        unsigned char* p = (unsigned char*)realloc(m_buf, allocSize);
        if (p != NULL) {
            m_buf = p;
            m_bufsize = allocSize;
        } else {
            mesa_loge("realloc (%zu) failed\n", allocSize);
            free(m_buf);
            m_buf = NULL;
            m_bufsize = 0;
        }
    }

    return m_buf + kWriteOffset;
};

int QemuPipeStream::commitBuffer(size_t size) {
    if (size == 0) return 0;
    return writeFully(m_buf + kWriteOffset, size);
}

int QemuPipeStream::writeFully(const void* buf, size_t len) {
    return qemu_pipe_write_fully(m_sock, buf, len);
}

const unsigned char* QemuPipeStream::readFully(void* buf, size_t len) {
    return commitBufferAndReadFully(0, buf, len);
}

const unsigned char* QemuPipeStream::commitBufferAndReadFully(size_t writeSize,
                                                              void* userReadBufPtr,
                                                              size_t totalReadSize) {
    unsigned char* userReadBuf = static_cast<unsigned char*>(userReadBufPtr);

    if (!valid()) return NULL;

    if (!userReadBuf) {
        if (totalReadSize > 0) {
            mesa_loge(
                "QemuPipeStream::commitBufferAndReadFully failed, userReadBuf=NULL, totalReadSize "
                "%zu, lethal"
                " error, exiting.",
                totalReadSize);
            abort();
        }
        if (!writeSize) {
            return NULL;
        }
    }

    // Advance buffered read if not yet consumed.
    size_t remaining = totalReadSize;
    size_t bufferedReadSize = m_readLeft < remaining ? m_readLeft : remaining;
    if (bufferedReadSize) {
        memcpy(userReadBuf, m_buf + (m_read - m_readLeft), bufferedReadSize);
        remaining -= bufferedReadSize;
        m_readLeft -= bufferedReadSize;
    }

    // Early out if nothing left to do.
    if (!writeSize && !remaining) {
        return userReadBuf;
    }

    writeFully(m_buf + kWriteOffset, writeSize);

    // Now done writing. Early out if no reading left to do.
    if (!remaining) {
        return userReadBuf;
    }

    // Read up to kReadSize bytes if all buffered read has been consumed.
    size_t maxRead = m_readLeft ? 0 : kReadSize;

    ssize_t actual = 0;

    if (maxRead) {
        actual = qemu_pipe_read(m_sock, m_buf, maxRead);
        // Updated buffered read size.
        if (actual > 0) {
            m_read = m_readLeft = actual;
        }

        if (actual == 0) {
            mesa_logi("%s: end of pipe", __FUNCTION__);
            return NULL;
        }
    }

    // Consume buffered read and read more if necessary.
    while (remaining) {
        bufferedReadSize = m_readLeft < remaining ? m_readLeft : remaining;
        if (bufferedReadSize) {
            memcpy(userReadBuf + (totalReadSize - remaining), m_buf + (m_read - m_readLeft),
                   bufferedReadSize);
            remaining -= bufferedReadSize;
            m_readLeft -= bufferedReadSize;
            continue;
        }

        actual = qemu_pipe_read(m_sock, m_buf, kReadSize);

        if (actual == 0) {
            mesa_logi("%s: Failed reading from pipe: %d", __FUNCTION__, errno);
            return NULL;
        }

        if (actual > 0) {
            m_read = m_readLeft = actual;
            continue;
        }

        if (!qemu_pipe_try_again(actual)) {
            mesa_logi("%s: Error reading from pipe: %d", __FUNCTION__, errno);
            return NULL;
        }
    }

    return userReadBuf;
}

const unsigned char* QemuPipeStream::read(void* buf, size_t* inout_len) {
    if (!valid()) return NULL;
    if (!buf) {
        mesa_loge("QemuPipeStream::read failed, buf=NULL");
        return NULL;  // do not allow NULL buf in that implementation
    }

    int n = recv(buf, *inout_len);

    if (n > 0) {
        *inout_len = n;
        return (const unsigned char*)buf;
    }

    return NULL;
}

bool QemuPipeStream::valid() { return qemu_pipe_valid(m_sock); }

int QemuPipeStream::recv(void* buf, size_t len) {
    if (!valid()) return int(ERR_INVALID_SOCKET);
    char* p = (char*)buf;
    int ret = 0;
    while (len > 0) {
        int res = qemu_pipe_read(m_sock, p, len);
        if (res > 0) {
            p += res;
            ret += res;
            len -= res;
            continue;
        }
        if (res == 0) { /* EOF */
            break;
        }
        if (qemu_pipe_try_again(res)) {
            continue;
        }

        /* A real error */
        if (ret == 0) ret = -1;
        break;
    }
    return ret;
}
