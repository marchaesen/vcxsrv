/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "QemuPipeStream.h"

QemuPipeStream::QemuPipeStream(size_t bufSize) : IOStream(bufSize) { (void)bufSize; }

QemuPipeStream::~QemuPipeStream() {}

int QemuPipeStream::connect(const char* serviceName) {
    (void)serviceName;
    return 0;
}

uint64_t QemuPipeStream::processPipeInit() { return 0; }

void* QemuPipeStream::allocBuffer(size_t minSize) {
    (void)minSize;
    return nullptr;
};

int QemuPipeStream::commitBuffer(size_t size) {
    (void)size;
    return 0;
}

int QemuPipeStream::writeFully(const void* buf, size_t len) {
    (void)buf;
    (void)len;
    return 0;
}

const unsigned char* QemuPipeStream::readFully(void* buf, size_t len) {
    (void)buf;
    (void)len;
    return nullptr;
}

const unsigned char* QemuPipeStream::commitBufferAndReadFully(size_t writeSize,
                                                              void* userReadBufPtr,
                                                              size_t totalReadSize) {
    (void)writeSize;
    (void)userReadBufPtr;
    (void)totalReadSize;
    return nullptr;
}

const unsigned char* QemuPipeStream::read(void* buf, size_t* inout_len) {
    (void)buf;
    (void)inout_len;
    return nullptr;
}

bool QemuPipeStream::valid() { return false; }

int QemuPipeStream::recv(void* buf, size_t len) {
    (void)buf;
    (void)len;
    return 0;
}
