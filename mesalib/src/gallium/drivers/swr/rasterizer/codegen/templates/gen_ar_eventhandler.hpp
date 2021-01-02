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
// clang-format on
#pragma once

#include "${event_header}"

namespace ArchRast
{
    //////////////////////////////////////////////////////////////////////////
    /// EventHandler - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    class EventHandler
    {
    public:
        EventHandler() {}
        virtual ~EventHandler() {}

        virtual void FlushDraw(uint32_t drawId) {}

<%  sorted_groups = sorted(protos['events']['groups']) %>
%   for group in sorted_groups:
%       for event_key in protos['events']['groups'][group]:
<%
            event = protos['events']['defs'][event_key]
%>        virtual void Handle(const ${event['name']}& event) {}
%       endfor
%   endfor
    };
} // namespace ArchRast
// clan-format off
