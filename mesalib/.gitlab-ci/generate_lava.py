#!/usr/bin/env python3

from jinja2 import Environment, FileSystemLoader
import argparse

device_types = {
    "rk3288-veyron-jaq": {
        "gpu_version": "panfrost-t760",
        "boot_method": "depthcharge",
        "lava_device_type": "rk3288-veyron-jaq",
        "kernel_image_type": "",
    },
    "rk3399-gru-kevin": {
        "gpu_version": "panfrost-t860",
        "boot_method": "depthcharge",
        "lava_device_type": "rk3399-gru-kevin",
        "kernel_image_type": "",
    },
    "sun8i-h3-libretech-all-h3-cc": {
        "gpu_version": "lima",
        "boot_method": "u-boot",
        "lava_device_type": "sun8i-h3-libretech-all-h3-cc",
        "kernel_image_type": "type: zimage",
    },
    "meson-gxl-s905x-libretech-cc": {
        "gpu_version": "lima",
        "boot_method": "u-boot",
        "lava_device_type": "meson-gxl-s905x-libretech-cc",
        "kernel_image_type": "type: image",
    },
}

parser = argparse.ArgumentParser()
parser.add_argument("--template")
parser.add_argument("--base-artifacts-url")
parser.add_argument("--arch")
parser.add_argument("--device-types", nargs="+")
parser.add_argument("--kernel-image-name")
args = parser.parse_args()

env = Environment(loader = FileSystemLoader('.'), trim_blocks=True, lstrip_blocks=True)
template = env.get_template(args.template)

for device_type in args.device_types:
    values = {}
    values['base_artifacts_url'] = args.base_artifacts_url
    values['arch'] = args.arch
    values['device_type'] = device_type
    values['kernel_image_name'] = args.kernel_image_name
    values['lava_device_type'] = device_types[device_type]['lava_device_type']
    values['gpu_version'] = device_types[device_type]['gpu_version']
    values['boot_method'] = device_types[device_type]['boot_method']
    values['kernel_image_type'] = device_types[device_type]['kernel_image_type']

    f = open('results/lava-deqp-%s.yml' % device_type, "w")
    f.write(template.render(values))
    f.close()

