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
 * @brief Definitions for events.  auto-generated file
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
#include "core/state.h"

<%
    always_enabled_knob_groups = ['Framework', 'SWTagFramework', 'ApiSwr']
    group_knob_remap_table = {
        "ShaderStats": "KNOB_AR_ENABLE_SHADER_STATS",
        "PipelineStats" : "KNOB_AR_ENABLE_PIPELINE_STATS",
        "SWTagData" : "KNOB_AR_ENABLE_SWTAG_DATA",
 }
%>
namespace ArchRast
{
<% sorted_enums = sorted(protos['enums']['defs']) %>
% for name in sorted_enums:
    enum ${name}
    {<% names = protos['enums']['defs'][name]['names'] %>
        % for i in range(len(names)):
        ${names[i].lstrip()}
        % endfor
    };
% endfor

    // Forward decl
    class EventHandler;

    //////////////////////////////////////////////////////////////////////////
    /// Event - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    struct Event
    {
        const uint32_t eventId = {0xFFFFFFFF};
        Event() {}
        virtual ~Event() {}

        virtual bool IsEnabled() const { return true; };
        virtual const uint32_t GetEventId() const = 0;
        virtual void Accept(EventHandler* pHandler) const = 0;
    };

<%  sorted_groups = sorted(protos['events']['groups']) %>
% for group in sorted_groups:
    % for event_key in protos['events']['groups'][group]:
<%
        event = protos['events']['defs'][event_key]
%>
    //////////////////////////////////////////////////////////////////////////
    /// ${event_key}Data
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct ${event['name']}Data
    {<%
        fields = event['fields'] %>
        // Fields
        % for i in range(len(fields)):
            % if fields[i]['size'] > 1:
        ${fields[i]['type']} ${fields[i]['name']}[${fields[i]['size']}];
            % else:
        ${fields[i]['type']} ${fields[i]['name']};
            % endif
        % endfor
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// ${event_key}
    //////////////////////////////////////////////////////////////////////////
    struct ${event['name']} : Event
    {<%
        fields = event['fields'] %>
        const uint32_t eventId = {${ event['id'] }};
        ${event['name']}Data data;

        // Constructor
        ${event['name']}(
        % for i in range(len(fields)):
            % if i < len(fields)-1:
                % if fields[i]['size'] > 1:
            ${fields[i]['type']}* ${fields[i]['name']},
            uint32_t ${fields[i]['name']}_size,
                % else:
            ${fields[i]['type']} ${fields[i]['name']},
                % endif
            % endif
            % if i == len(fields)-1:
                % if fields[i]['size'] > 1:
            ${fields[i]['type']}* ${fields[i]['name']},
            uint32_t ${fields[i]['name']}_size
                % else:
            ${fields[i]['type']} ${fields[i]['name']}
                % endif
            % endif
        % endfor
        )
        {
        % for i in range(len(fields)):
            % if fields[i]['size'] > 1:
                % if fields[i]['type'] == 'char':
            // Copy size of string (null-terminated) followed by string into entire buffer
            SWR_ASSERT(${fields[i]['name']}_size + 1 < ${fields[i]['size']} - sizeof(uint32_t), "String length must be less than size of char buffer - size(uint32_t)!");
            memcpy(data.${fields[i]['name']}, &${fields[i]['name']}_size, sizeof(uint32_t));
            strcpy_s(data.${fields[i]['name']} + sizeof(uint32_t), ${fields[i]['name']}_size + 1, ${fields[i]['name']});
                % else:
            memcpy(data.${fields[i]['name']}, ${fields[i]['name']}, ${fields[i]['name']}_size);
                % endif
            % else:
            data.${fields[i]['name']} = ${fields[i]['name']};
            % endif
        % endfor
        }

        virtual void Accept(EventHandler* pHandler) const;
        inline const uint32_t GetEventId() const { return eventId; }
        % if group not in always_enabled_knob_groups:
        <% 
            if group in group_knob_remap_table:
                group_knob_define = group_knob_remap_table[group]
            else:
                group_knob_define = 'KNOB_AR_ENABLE_' + group.upper() + '_EVENTS'
        %>
        bool IsEnabled() const
        {
            static const bool IsEventEnabled = true;    // TODO: Replace with knob for each event
            return ${group_knob_define} && IsEventEnabled;
        }
        % endif
    };

    % endfor

% endfor
} // namespace ArchRast
// clang-format on
