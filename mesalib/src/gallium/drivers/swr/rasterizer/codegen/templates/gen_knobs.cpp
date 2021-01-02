/******************************************************************************
 * Copyright (C) 2015-2018 Intel Corporation.   All Rights Reserved.
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
 * @file ${filename}.cpp
 *
 * @brief Dynamic Knobs for Core.
 *
 * ======================= AUTO GENERATED: DO NOT EDIT !!! ====================
 *
 * Generation Command Line:
 *  ${'\n *    '.join(cmdline)}
 *
 ******************************************************************************/
// clang-format off
<% calc_max_knob_len(knobs) %>
% for inc in includes:
#include <${inc}>
% endfor
#include <regex>
#include <core/utils.h>

//========================================================
// Implementation
//========================================================
void KnobBase::autoExpandEnvironmentVariables(std::string& text)
{
    size_t start;
    while ((start = text.find("${'${'}")) != std::string::npos)
    {
        size_t end = text.find("}");
        if (end == std::string::npos)
            break;
        const std::string var = GetEnv(text.substr(start + 2, end - start - 2));
        text.replace(start, end - start + 1, var);
    }
    // win32 style variable replacement
    while ((start = text.find("%")) != std::string::npos)
    {
        size_t end = text.find("%", start + 1);
        if (end == std::string::npos)
            break;
        const std::string var = GetEnv(text.substr(start + 1, end - start - 1));
        text.replace(start, end - start + 1, var);
    }
}

//========================================================
// Static Data Members
//========================================================
% for knob in knobs:
% if knob[1]['type'] == 'std::string':
${knob[1]['type']} GlobalKnobs::Knob_${knob[0]}::m_default = "${repr(knob[1]['default'])[1:-1]}";
% else:
${knob[1]['type']} GlobalKnobs::Knob_${knob[0]}::m_default = ${knob[1]['default']};
% endif
% endfor
GlobalKnobs g_GlobalKnobs;

//========================================================
// Knob Initialization
//========================================================
GlobalKnobs::GlobalKnobs()
{
    % for knob in knobs :
    InitKnob(${ knob[0] });
    % endfor
}

//========================================================
// Knob Display (Convert to String)
//========================================================
std::string GlobalKnobs::ToString(const char* optPerLinePrefix)
{
    std::basic_stringstream<char> str;
    str << std::showbase << std::setprecision(1) << std::fixed;

    if (optPerLinePrefix == nullptr)
    {
        optPerLinePrefix = "";
    }

    % for knob in knobs:
    str << optPerLinePrefix << "KNOB_${knob[0]}:${space_knob(knob[0])}";
    % if knob[1]['type'] == 'bool':
    str << (KNOB_${knob[0]} ? "+\n" : "-\n");
    % elif knob[1]['type'] != 'float' and knob[1]['type'] != 'std::string':
    str << std::hex << std::setw(11) << std::left << KNOB_${knob[0]};
    str << std::dec << KNOB_${knob[0]} << "\n";
    % else:
    str << KNOB_${knob[0]} << "\n";
    % endif
    % endfor
    str << std::ends;

    return str.str();
}
<%!
    # Globally available python 
    max_len = 0
    def calc_max_knob_len(knobs):
        global max_len
        max_len = 0
        for knob in knobs:
            if len(knob[0]) > max_len: max_len = len(knob[0])
        max_len += len('KNOB_ ')
        if max_len % 4: max_len += 4 - (max_len % 4)

    def space_knob(knob):
        knob_len = len('KNOB_' + knob)
        return ' '*(max_len - knob_len)

    def calc_max_name_len(choices_array):
        _max_len = 0
        for choice in choices_array:
            if len(choice['name']) > _max_len: _max_len = len(choice['name'])

        if _max_len % 4: _max_len += 4 - (_max_len % 4)
        return _max_len

    def space_name(name, max_len):
        name_len = len(name)
        return ' '*(max_len - name_len)
%>
// clang-format on
