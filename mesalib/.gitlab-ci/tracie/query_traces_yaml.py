#!/usr/bin/python3

# Copyright (c) 2019 Collabora Ltd
# Copyright © 2020 Valve Corporation.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

import argparse
import yaml
from traceutil import all_trace_type_names, trace_type_from_name
from traceutil import trace_type_from_filename

def trace_devices(trace):
    return [e['device'] for e in trace['expectations']]

def cmd_traces_db_gitlab_project_url(args):
    with open(args.file, 'r') as f:
        y = yaml.safe_load(f)
    print(y['traces-db']['gitlab-project-url'])

def cmd_traces_db_commit(args):
    with open(args.file, 'r') as f:
        y = yaml.safe_load(f)
    print(y['traces-db']['commit'])

def cmd_traces(args):
    with open(args.file, 'r') as f:
        y = yaml.safe_load(f)

    traces = y['traces']
    traces = filter(lambda t: trace_type_from_filename(t['path']) in args.trace_types,
                    traces)
    if args.device_name:
        traces = filter(lambda t: args.device_name in trace_devices(t), traces)

    traces = list(traces)

    if len(traces) == 0:
        return

    print('\n'.join((t['path'] for t in traces)))

def cmd_checksum(args):
    with open(args.file, 'r') as f:
        y = yaml.safe_load(f)

    traces = y['traces']
    trace = next(t for t in traces if t['path'] == args.trace_path)
    expectation = next(e for e in trace['expectations'] if e['device'] == args.device_name)

    print(expectation['checksum'])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--file', required=True,
                        help='the name of the yaml file')

    subparsers = parser.add_subparsers(help='sub-command help')

    parser_traces_db_gitlab_project_url = subparsers.add_parser('traces_db_gitlab_project_url')
    parser_traces_db_gitlab_project_url.set_defaults(func=cmd_traces_db_gitlab_project_url)

    parser_traces_db_commit = subparsers.add_parser('traces_db_commit')
    parser_traces_db_commit.set_defaults(func=cmd_traces_db_commit)

    parser_traces = subparsers.add_parser('traces')
    parser_traces.add_argument('--device-name', required=False,
                               help="the name of the graphics device used to "
                                     "produce images")
    parser_traces.add_argument('--trace-types', required=False,
                               default=",".join(all_trace_type_names()),
                               help="the types of traces to look for in recursive "
                                    "dir walks " "(by default all types)")
    parser_traces.set_defaults(func=cmd_traces)

    parser_checksum = subparsers.add_parser('checksum')
    parser_checksum.add_argument('--device-name', required=True,
                               help="the name of the graphics device used to "
                                     "produce images")
    parser_checksum.add_argument('trace_path')
    parser_checksum.set_defaults(func=cmd_checksum)

    args = parser.parse_args()
    if hasattr(args, 'trace_types'):
        args.trace_types = [trace_type_from_name(t) for t in args.trace_types.split(",")]

    args.func(args)

if __name__ == "__main__":
    main()
