# Copyright 2024 Google LLC
# SPDX-License-Identifier: MIT

import argparse
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--utrace-src', required=True)
    parser.add_argument('--utrace-hdr', required=True)
    parser.add_argument('--perfetto-hdr', required=True)
    return parser.parse_args()


args = parse_args()
sys.path.insert(0, args.import_path)

from u_trace import ForwardDecl, Header, HeaderScope  # noqa: E402
from u_trace import Tracepoint  # noqa: E402
from u_trace import TracepointArg as Arg  # noqa: E402
from u_trace import TracepointArgStruct as ArgStruct  # noqa: E402
from u_trace import utrace_generate, utrace_generate_perfetto_utils  # noqa: E402

Header('vulkan/vulkan_core.h', scope=HeaderScope.HEADER)
ForwardDecl('struct panvk_device')


def begin_end_tp(name, args=[], tp_struct=None):
    Tracepoint(
        f'begin_{name}',
        tp_perfetto=f'panvk_utrace_perfetto_begin_{name}',
    )

    Tracepoint(
        f'end_{name}',
        args=args,
        tp_struct=tp_struct,
        tp_perfetto=f'panvk_utrace_perfetto_end_{name}',
    )


def define_tracepoints():
    begin_end_tp(
        'cmdbuf',
        args=[
            Arg(
                type='VkCommandBufferUsageFlags',
                var='flags',
                c_format='0x%x',
            ),
        ],
    )


def generate_code():
    utrace_generate(
        cpath=args.utrace_src,
        hpath=args.utrace_hdr,
        ctx_param='struct panvk_device *dev',
    )

    utrace_generate_perfetto_utils(hpath=args.perfetto_hdr)


def main():
    define_tracepoints()
    generate_code()


if __name__ == '__main__':
    main()
