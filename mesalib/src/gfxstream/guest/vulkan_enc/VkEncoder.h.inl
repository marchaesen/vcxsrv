/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
    void flush();
    void lock();
    void unlock();
    void incRef();
    bool decRef();
    std::string getPacketContents(const uint8_t* ptr, size_t len);
    uint32_t refCount = 1;
    #define POOL_CLEAR_INTERVAL 10
    uint32_t encodeCount = 0;
    uint32_t featureBits = 0;
