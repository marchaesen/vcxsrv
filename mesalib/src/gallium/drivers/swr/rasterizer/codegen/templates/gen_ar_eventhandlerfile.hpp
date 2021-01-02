/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file ${filename}
 *
 * @brief Event handler interface.  auto-generated file
 *
 * DO NOT EDIT
 *
 * Generation Command Line:
 *  ${'\n *    '.join(cmdline)}
 *
 ******************************************************************************/
// clang-format off
#pragma once

#include "common/os.h"
#include "${event_header}"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// EventHandlerFile - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    class EventHandlerFile : public EventHandler
    {
    public:
        EventHandlerFile(uint32_t id) : mBufOffset(0)
        {
#if defined(_WIN32)
            DWORD pid = GetCurrentProcessId();
            TCHAR procname[MAX_PATH];
            GetModuleFileName(NULL, procname, MAX_PATH);
            const char*       pBaseName = strrchr(procname, '\\');
            std::stringstream outDir;
            outDir << KNOB_DEBUG_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
            mOutputDir = outDir.str();
            if (CreateDirectory(mOutputDir.c_str(), NULL))
            {
                std::cout << std::endl
                          << "ArchRast Dir:       " << mOutputDir << std::endl
                          << std::endl
                          << std::flush;
            }

            // There could be multiple threads creating thread pools. We
            // want to make sure they are uniquely identified by adding in
            // the creator's thread id into the filename.
            std::stringstream fstr;
            fstr << outDir.str().c_str() << "\\ar_event" << std::this_thread::get_id();
            fstr << "_" << id << ".bin" << std::ends;
            mFilename = fstr.str();
#else
            // There could be multiple threads creating thread pools. We
            // want to make sure they are uniquely identified by adding in
            // the creator's thread id into the filename.
            std::stringstream fstr;
            fstr << "/tmp/ar_event" << std::this_thread::get_id();
            fstr << "_" << id << ".bin" << std::ends;
            mFilename = fstr.str();
#endif
        }

        virtual ~EventHandlerFile() { FlushBuffer(); }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Flush buffer to file.
        bool FlushBuffer()
        {
            if (mBufOffset > 0)
            {
                if (mBufOffset == mHeaderBufOffset)
                {
                    // Nothing to flush. Only header has been generated.
                    return false;
                }

                std::ofstream file;
                file.open(mFilename, std::ios::out | std::ios::app | std::ios::binary);

                if (!file.is_open())
                {
                    SWR_INVALID("ArchRast: Could not open event file!");
                    return false;
                }

                file.write((char*)mBuffer, mBufOffset);
                file.close();

                mBufOffset       = 0;
                mHeaderBufOffset = 0; // Reset header offset so its no longer considered.
            }
            return true;
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief Write event and its payload to the memory buffer.
        void Write(uint32_t eventId, const char* pBlock, uint32_t size)
        {
            if ((mBufOffset + size + sizeof(eventId)) > mBufferSize)
            {
                if (!FlushBuffer())
                {
                    // Don't corrupt what's already in the buffer?
                    /// @todo Maybe add corrupt marker to buffer here in case we can open file in
                    /// future?
                    return;
                }
            }

            memcpy(&mBuffer[mBufOffset], (char*)&eventId, sizeof(eventId));
            mBufOffset += sizeof(eventId);
            memcpy(&mBuffer[mBufOffset], pBlock, size);
            mBufOffset += size;
        }
<%  sorted_groups = sorted(protos['events']['groups']) %>
%   for group in sorted_groups:
%       for event_key in protos['events']['groups'][group]:
<%
            event = protos['events']['defs'][event_key]
%>
        //////////////////////////////////////////////////////////////////////////
        /// @brief Handle ${event_key} event
        virtual void Handle(const ${event['name']}& event)
        {
% if event['num_fields'] == 0:
            Write(event.eventId, (char*)&event.data, 0);
% else:
            Write(event.eventId, (char*)&event.data, sizeof(event.data));
% endif
        }
%       endfor
%   endfor

        //////////////////////////////////////////////////////////////////////////
        /// @brief Everything written to buffer this point is the header.
        virtual void MarkHeader()
        {
            mHeaderBufOffset = mBufOffset;
        }

        std::string mFilename;
        std::string mOutputDir;

        static const uint32_t mBufferSize = 1024;
        uint8_t               mBuffer[mBufferSize];
        uint32_t mBufOffset{0};
        uint32_t mHeaderBufOffset{0};
    };
} // namespace ArchRast
// clang-format on
