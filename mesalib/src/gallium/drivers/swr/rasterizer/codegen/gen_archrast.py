# Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
from __future__ import print_function
import os
import sys
import re
from gen_common import *

def parse_event_fields(lines, idx, event_dict):
    """
        Parses lines from a proto file that contain an event definition and stores it in event_dict
    """
    fields = []
    end_of_event = False

    # record all fields in event definition.
    # note: we don't check if there's a leading brace.
    while not end_of_event and idx < len(lines):
        line = lines[idx].rstrip()
        idx += 1

        # ex 1: uint32_t    numSampleCLZExecuted; // number of sample_cl_z instructions executed
        # ex 2: char        reason[256]; // size of reason
        match = re.match(r'^(\s*)([\w\*]+)(\s+)([\w]+)(\[\d+\])*;\s*(\/\/.*)*$', line)
        # group 1 -
        # group 2 type
        # group 3 -
        # group 4 name
        # group 5 [array size]
        # group 6 //comment

        if match:
            field = {
                "type": match.group(2),
                "name": match.group(4),
                "size": int(match.group(5)[1:-1]) if match.group(5) else 1,
                "desc": match.group(6)[2:].strip() if match.group(6) else "",
            }
            fields.append(field)

        end_of_event = re.match(r'(\s*)};', line)

    event_dict['fields'] = fields
    event_dict['num_fields'] = len(fields)

    return idx

def parse_enums(lines, idx, event_dict):
    """
        Parses lines from a proto file that contain an enum definition and stores it in event_dict
    """
    enum_names = []
    end_of_enum = False

    # record all enum values in enumeration
    # note: we don't check if there's a leading brace.
    while not end_of_enum and idx < len(lines):
        line = lines[idx].rstrip()
        idx += 1

        preprocessor = re.search(r'#if|#endif', line)

        if not preprocessor:
            enum = re.match(r'(\s*)(\w+)(\s*)', line)

            if enum:
                enum_names.append(line)

            end_of_enum = re.match(r'(\s*)};', line)

    event_dict['names'] = enum_names
    return idx

def parse_protos(files, verbose=False):
    """
        Parses a proto file and returns a dictionary of event definitions
    """

    # Protos structure:
    #
    # {
    #   "events": {
    #     "defs": {     // dict of event definitions where keys are 'group_name::event_name"
    #       ...,
    #       "ApiStat::DrawInfoEvent": {
    #         "id": 3,
    #         "group": "ApiStat",
    #         "name": "DrawInfoEvent",  // name of event without 'group_name::' prefix
    #         "desc": "",
    #         "fields": [
    #           {
    #             "type": "uint32_t",
    #             "name": "drawId",
    #             "size": 1,
    #             "desc": "",
    #           },
    #           ...
    #         ]
    #       },
    #       ...
    #     },
    #     "groups": {   // dict of groups with lists of event keys
    #       "ApiStat": [
    #         "ApiStat::DispatchEvent",
    #         "ApiStat::DrawInfoEvent",
    #         ...
    #       ],
    #       "Framework": [
    #         "Framework::ThreadStartApiEvent",
    #         "Framework::ThreadStartWorkerEvent",
    #         ...
    #       ],
    #       ...
    #     },
    #     "map": {  // map of event ids to match archrast output to event key
    #       "1": "Framework::ThreadStartApiEvent",
    #       "2": "Framework::ThreadStartWorkerEvent",
    #       "3": "ApiStat::DrawInfoEvent",
    #       ...
    #     }
    #   },
    #   "enums": { ... }    // enums follow similar defs, map (groups?) structure
    # }

    protos = {
        'events': {
            'defs': {},             # event dictionary containing events with their fields
            'map': {},              # dictionary to map event ids to event names
            'groups': {}            # event keys stored by groups
        },
        'enums': {
            'defs': {},
            'map': {}
        }
    }

    event_id = 0
    enum_id = 0

    if type(files) is not list:
        files = [files]

    for filename in files:
        if verbose:
            print("Parsing proto file: %s" % os.path.normpath(filename))

        with open(filename, 'r') as f:
            lines = f.readlines()
            in_brief = False
            brief = []
            idx = 0
            while idx < len(lines):
                line = lines[idx].strip()
                idx += 1

                # If currently processing a brief, keep processing or change state
                if in_brief:
                    match = re.match(r'^\s*\/\/\/\s*(.*)$', line)                   # i.e. "/// more event desc..."
                    if match:
                        brief.append(match.group(1).strip())
                        continue
                    else:
                        in_brief = False

                # Match event/enum brief
                match = re.match(r'^\s*\/\/\/\s*@(brief|breif)\s*(.*)$', line)       # i.e. "///@brief My event desc..."
                if match:
                    in_brief = True
                    brief.append(match.group(2).strip())
                    continue

                # Match event definition
                match = re.match(r'event(\s*)(((\w*)::){0,1}(\w+))', line)          # i.e. "event SWTag::CounterEvent"
                if match:
                    event_id += 1

                    # Parse event attributes
                    event_key = match.group(2)                                      # i.e. SWTag::CounterEvent
                    event_group = match.group(4) if match.group(4) else ""          # i.e. SWTag
                    event_name = match.group(5)                                     # i.e. CounterEvent

                    # Define event attributes
                    event = {
                        'id': event_id,
                        'group': event_group,
                        'name': event_name,
                        'desc': ' '.join(brief)
                    }
                    # Add period at end of event desc if necessary
                    if event["desc"] and event["desc"][-1] != '.':
                        event["desc"] += '.'

                    # Reset brief
                    brief = []

                    # Now add event fields
                    idx = parse_event_fields(lines, idx, event)

                    # Register event and mapping
                    protos['events']['defs'][event_key] = event
                    protos['events']['map'][event_id] = event_key

                    continue

                # Match enum definition
                match = re.match(r'enum(\s*)(\w+)', line)
                if match:
                    enum_id += 1

                    # Parse enum attributes
                    enum_name = match.group(2)

                    # Define enum attr
                    enum = {
                        'name': enum_name,
                        'desc': ' '.join(brief)
                    }
                    # Add period at end of event desc if necessary
                    if enum["desc"] and enum["desc"][-1] != '.':
                        enum["desc"] += '.'

                    # Reset brief
                    brief = []

                    # Now add enum fields
                    idx = parse_enums(lines, idx, enum)

                    # Register enum and mapping
                    protos['enums']['defs'][enum_name] = enum
                    protos['enums']['map'][enum_id] = enum_name

                    continue

    # Sort and group events
    event_groups = protos['events']['groups']
    for key in sorted(protos['events']['defs']):
        group = protos['events']['defs'][key]['group']
        if group not in event_groups:
            event_groups[group] = []
        event_groups[group].append(key)

    return protos


def main():

    # Parse args...
    parser = ArgumentParser()
    parser.add_argument("--proto", "-p", dest="protos", nargs='+', help="Path to all proto file(s) to process. Accepts one or more paths (i.e. events.proto and events_private.proto)", required=True)
    parser.add_argument("--output-dir", help="Output dir (defaults to ./codegen). Will create folder if it does not exist.", required=False, default="codegen")
    parser.add_argument("--verbose", "-v", help="Verbose", action="store_true")
    args = parser.parse_args()

    if not os.path.exists(args.output_dir):
        MakeDir(args.output_dir)

    for f in args.protos:
        if not os.path.exists(f):
            print('Error: Could not find proto file %s' % f, file=sys.stderr)
            return 1

    # Parse each proto file and add to protos container
    protos = parse_protos(args.protos, args.verbose)

    files = [
        ["gen_ar_event.hpp", ""],
        ["gen_ar_event.cpp", ""],
        ["gen_ar_eventhandler.hpp", "gen_ar_event.hpp"],
        ["gen_ar_eventhandlerfile.hpp", "gen_ar_eventhandler.hpp"]
    ]

    rval = 0

    try:
        # Delete existing files
        for f in files:
            filename = f[0]
            output_fullpath = os.path.join(args.output_dir, filename)
            if os.path.exists(output_fullpath):
                if args.verbose:
                    print("Deleting existing file: %s" % output_fullpath)
                os.remove(output_fullpath)

        # Generate files from templates
        print("Generating c++ from proto files...")
        for f in files:
            filename = f[0]
            event_header = f[1]
            curdir = os.path.dirname(os.path.abspath(__file__))
            template_file = os.path.join(curdir, 'templates', filename)
            output_fullpath = os.path.join(args.output_dir, filename)

            if args.verbose:
                print("Generating: %s" % output_fullpath)
            MakoTemplateWriter.to_file(template_file, output_fullpath,
                    cmdline=sys.argv,
                    filename=filename,
                    protos=protos,
                    event_header=event_header)

    except Exception as e:
        print(e)
        rval = 1

    return rval

if __name__ == '__main__':
    sys.exit(main())
