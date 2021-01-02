/****************************************************************************
 * Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file rdtsc_buckets.h
 *
 * @brief declaration for rdtsc buckets.
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once

#include <vector>
#include <cassert>

struct BUCKET
{
    uint32_t id{0};
    uint64_t start{0};
    uint64_t elapsed{0};
    uint32_t count{0};

    BUCKET*             pParent{nullptr};
    std::vector<BUCKET> children;
};

struct BUCKET_DESC
{
    // name of bucket, used in reports
    std::string name;

    // description of bucket, used in threadviz
    std::string description;

    // enable for threadviz dumping
    bool enableThreadViz;

    // threadviz color of bucket, in RGBA8_UNORM format
    uint32_t color;
};


struct BUCKET_THREAD
{
    // name of thread, used in reports
    std::string name;

    // id for this thread, assigned by the thread manager
    uint32_t id{0};

    // root of the bucket hierarchy for this thread
    BUCKET root;

    // currently executing bucket somewhere in the hierarchy
    BUCKET* pCurrent{nullptr};

    // currently executing hierarchy level
    uint32_t level{0};

    // threadviz file object
    FILE* vizFile{nullptr};


    BUCKET_THREAD() {}
    BUCKET_THREAD(const BUCKET_THREAD& that)
    {
        name     = that.name;
        id       = that.id;
        root     = that.root;
        pCurrent = &root;
        vizFile  = that.vizFile;
    }
};

enum VIZ_TYPE
{
    VIZ_START = 0,
    VIZ_STOP  = 1,
    VIZ_DATA  = 2
};

struct VIZ_START_DATA
{
    uint8_t  type;
    uint32_t bucketId;
    uint64_t timestamp;
};

struct VIZ_STOP_DATA
{
    uint8_t  type;
    uint64_t timestamp;
};

inline void Serialize(FILE* f, const VIZ_START_DATA& data)
{
    fwrite(&data, sizeof(VIZ_START_DATA), 1, f);
}

inline void Deserialize(FILE* f, VIZ_START_DATA& data)
{
    fread(&data, sizeof(VIZ_START_DATA), 1, f);
    assert(data.type == VIZ_START);
}

inline void Serialize(FILE* f, const VIZ_STOP_DATA& data)
{
    fwrite(&data, sizeof(VIZ_STOP_DATA), 1, f);
}

inline void Deserialize(FILE* f, VIZ_STOP_DATA& data)
{
    fread(&data, sizeof(VIZ_STOP_DATA), 1, f);
    assert(data.type == VIZ_STOP);
}

inline void Serialize(FILE* f, const std::string& string)
{
    assert(string.size() <= 256);

    uint8_t length = (uint8_t)string.size();
    fwrite(&length, sizeof(length), 1, f);
    fwrite(string.c_str(), string.size(), 1, f);
}

inline void Deserialize(FILE* f, std::string& string)
{
    char    cstr[256];
    uint8_t length;
    fread(&length, sizeof(length), 1, f);
    fread(cstr, length, 1, f);
    cstr[length] = 0;
    string.assign(cstr);
}

inline void Serialize(FILE* f, const BUCKET_DESC& desc)
{
    Serialize(f, desc.name);
    Serialize(f, desc.description);
    fwrite(&desc.enableThreadViz, sizeof(desc.enableThreadViz), 1, f);
    fwrite(&desc.color, sizeof(desc.color), 1, f);
}

inline void Deserialize(FILE* f, BUCKET_DESC& desc)
{
    Deserialize(f, desc.name);
    Deserialize(f, desc.description);
    fread(&desc.enableThreadViz, sizeof(desc.enableThreadViz), 1, f);
    fread(&desc.color, sizeof(desc.color), 1, f);
}
