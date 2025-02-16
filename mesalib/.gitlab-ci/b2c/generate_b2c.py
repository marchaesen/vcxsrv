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
from os import environ, path


# Pass all the environment variables prefixed by B2C_
values = {
    key.removeprefix("B2C_").lower(): environ[key]
    for key in environ if key.startswith("B2C_")
}

env = Environment(loader=FileSystemLoader(path.dirname(values['job_template'])),
                  trim_blocks=True, lstrip_blocks=True)

template = env.get_template(path.basename(values['job_template']))

values['ci_job_id'] = environ['CI_JOB_ID']
values['ci_runner_description'] = environ['CI_RUNNER_DESCRIPTION']
values['job_volume_exclusions'] = [excl for excl in values['job_volume_exclusions'].split(",") if excl]
values['working_dir'] = environ['CI_PROJECT_DIR']

values['image_under_test'] = environ['IMAGE_UNDER_TEST']
values['machine_registration_image'] = environ.get('MACHINE_REGISTRATION_IMAGE', "registry.freedesktop.org/gfx-ci/ci-tron/machine-registration:latest")
values['telegraf_image'] = environ.get('TELEGRAF_IMAGE', "registry.freedesktop.org/gfx-ci/ci-tron/telegraf:latest")

# Pull all our images through our proxy registry
for image in ['image_under_test', 'machine_registration_image', 'telegraf_image']:
    values[image] = values[image].replace(
        'registry.freedesktop.org',
        '{{ fdo_proxy_registry }}'
    )

if 'kernel_cmdline_extras' not in values:
    values['kernel_cmdline_extras'] = ''

with open(path.splitext(path.basename(values['job_template']))[0], "w") as f:
    f.write(template.render(values))
