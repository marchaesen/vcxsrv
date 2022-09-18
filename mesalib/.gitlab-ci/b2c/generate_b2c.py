#!/usr/bin/env python3

# Copyright © 2022 Valve Corporation
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

from jinja2 import Environment, FileSystemLoader
from argparse import ArgumentParser
from os import environ, path


parser = ArgumentParser()
parser.add_argument('--ci-job-id')
parser.add_argument('--container-cmd')
parser.add_argument('--initramfs-url')
parser.add_argument('--job-success-regex')
parser.add_argument('--job-warn-regex')
parser.add_argument('--kernel-url')
parser.add_argument('--log-level', type=int)
parser.add_argument('--poweroff-delay', type=int)
parser.add_argument('--session-end-regex')
parser.add_argument('--session-reboot-regex')
parser.add_argument('--tags', nargs='?', default='')
parser.add_argument('--template', default='b2c.yml.jinja2.jinja2')
parser.add_argument('--timeout-boot-minutes', type=int)
parser.add_argument('--timeout-boot-retries', type=int)
parser.add_argument('--timeout-first-minutes', type=int)
parser.add_argument('--timeout-first-retries', type=int)
parser.add_argument('--timeout-minutes', type=int)
parser.add_argument('--timeout-overall-minutes', type=int)
parser.add_argument('--timeout-retries', type=int)
parser.add_argument('--job-volume-exclusions', nargs='?', default='')
parser.add_argument('--volume', action='append')
parser.add_argument('--mount-volume', action='append')
parser.add_argument('--local-container', default=environ.get('B2C_LOCAL_CONTAINER', 'alpine:latest'))
parser.add_argument('--working-dir')
args = parser.parse_args()

env = Environment(loader=FileSystemLoader(path.dirname(args.template)),
                  trim_blocks=True, lstrip_blocks=True)

template = env.get_template(path.basename(args.template))

values = {}
values['ci_job_id'] = args.ci_job_id
values['container_cmd'] = args.container_cmd
values['initramfs_url'] = args.initramfs_url
values['job_success_regex'] = args.job_success_regex
values['job_warn_regex'] = args.job_warn_regex
values['kernel_url'] = args.kernel_url
values['log_level'] = args.log_level
values['poweroff_delay'] = args.poweroff_delay
values['session_end_regex'] = args.session_end_regex
values['session_reboot_regex'] = args.session_reboot_regex
values['tags'] = args.tags
values['template'] = args.template
values['timeout_boot_minutes'] = args.timeout_boot_minutes
values['timeout_boot_retries'] = args.timeout_boot_retries
values['timeout_first_minutes'] = args.timeout_first_minutes
values['timeout_first_retries'] = args.timeout_first_retries
values['timeout_minutes'] = args.timeout_minutes
values['timeout_overall_minutes'] = args.timeout_overall_minutes
values['timeout_retries'] = args.timeout_retries
if len(args.job_volume_exclusions) > 0:
    exclusions = args.job_volume_exclusions.split(",")
    values['job_volume_exclusions'] = [excl for excl in exclusions if len(excl) > 0]
if args.volume is not None:
    values['volumes'] = args.volume
if args.mount_volume is not None:
    values['mount_volumes'] = args.mount_volume
values['working_dir'] = args.working_dir

assert(len(args.local_container) > 0)
values['local_container'] = args.local_container.replace(
    # Use the gateway's pull-through registry cache to reduce load on fd.o.
    'registry.freedesktop.org', '{{ fdo_proxy_registry }}'
)

if 'B2C_KERNEL_CMDLINE_EXTRAS' in environ:
    values['cmdline_extras'] = environ['B2C_KERNEL_CMDLINE_EXTRAS']

f = open(path.splitext(path.basename(args.template))[0], "w")
f.write(template.render(values))
f.close()
