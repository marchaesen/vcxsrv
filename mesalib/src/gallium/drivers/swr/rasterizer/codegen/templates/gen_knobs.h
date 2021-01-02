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
 * @file ${filename}.h
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
#pragma once
#include <string>

struct KnobBase
{
private:
    // Update the input string.
    static void autoExpandEnvironmentVariables(std::string& text);

protected:
    // Leave input alone and return new string.
    static std::string expandEnvironmentVariables(std::string const& input)
    {
        std::string text = input;
        autoExpandEnvironmentVariables(text);
        return text;
    }

    template <typename T>
    static T expandEnvironmentVariables(T const& input)
    {
        return input;
    }
};

template <typename T>
struct Knob : KnobBase
{
public:
    const T& Value() const { return m_Value; }
    const T& Value(T const& newValue)
    {
        m_Value = expandEnvironmentVariables(newValue);
        return Value();
    }

private:
    T m_Value;
};

#define DEFINE_KNOB(_name, _type)                               \\

    struct Knob_##_name : Knob<_type>                           \\

    {                                                           \\

        static const char* Name() { return "KNOB_" #_name; }    \\

        static _type DefaultValue() { return (m_default); }     \\

    private:                                                    \\

        static _type m_default;                                 \\

    } _name;

#define GET_KNOB(_name)             g_GlobalKnobs._name.Value()
#define SET_KNOB(_name, _newValue)  g_GlobalKnobs._name.Value(_newValue)

struct GlobalKnobs
{
    % for knob in knobs:
    //-----------------------------------------------------------
    // KNOB_${knob[0]}
    //
    % for line in knob[1]['desc']:
    // ${line}
    % endfor
    % if knob[1].get('choices'):
    <%
    choices = knob[1].get('choices')
    _max_len = calc_max_name_len(choices) %>//
    % for i in range(len(choices)):
    //     ${choices[i]['name']}${space_name(choices[i]['name'], _max_len)} = ${format(choices[i]['value'], '#010x')}
    % endfor
    % endif
    //
    DEFINE_KNOB(${knob[0]}, ${knob[1]['type']});

    % endfor

    std::string ToString(const char* optPerLinePrefix="");
    GlobalKnobs();
};
extern GlobalKnobs g_GlobalKnobs;

#undef DEFINE_KNOB

% for knob in knobs:
#define KNOB_${knob[0]}${space_knob(knob[0])} GET_KNOB(${knob[0]})
% endfor

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
