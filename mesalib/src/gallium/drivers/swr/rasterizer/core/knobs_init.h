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
 * @file knobs_init.h
 *
 * @brief Dynamic Knobs Initialization for Core.
 *
 ******************************************************************************/
#pragma once

#include <core/knobs.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Assume the type is compatible with a 32-bit integer
template <typename T>
static inline void ConvertEnvToKnob(const char* pOverride, T& knobValue)
{
    uint32_t value    = 0;
    char*    pStopped = nullptr;
    value             = strtoul(pOverride, &pStopped, 0);
    if (pStopped != pOverride)
    {
        knobValue = static_cast<T>(value);
    }
}

static inline void ConvertEnvToKnob(const char* pOverride, bool& knobValue)
{
    size_t len = strlen(pOverride);
    if (len == 1)
    {
        auto c = tolower(pOverride[0]);
        if (c == 'y' || c == 't' || c == '1')
        {
            knobValue = true;
            return;
        }
        if (c == 'n' || c == 'f' || c == '0')
        {
            knobValue = false;
            return;
        }
    }

    // Try converting to a number and casting to bool
    uint32_t value    = 0;
    char*    pStopped = nullptr;
    value             = strtoul(pOverride, &pStopped, 0);
    if (pStopped != pOverride)
    {
        knobValue = value != 0;
    }
}

static inline void ConvertEnvToKnob(const char* pOverride, float& knobValue)
{
    float value = knobValue;
    if (sscanf(pOverride, "%f", &value))
    {
        knobValue = value;
    }
}

static inline void ConvertEnvToKnob(const char* pOverride, std::string& knobValue)
{
    knobValue = pOverride;
}

template <typename T>
static inline void InitKnob(T& knob)
{
    // Read environment variables
    const char* pOverride = getenv(knob.Name());

    if (pOverride)
    {
        auto knobValue = knob.DefaultValue();
        ConvertEnvToKnob(pOverride, knobValue);
        knob.Value(knobValue);
    }
    else
    {
        // Set default value
        knob.Value(knob.DefaultValue());
    }
}
