#!/usr/bin/env python3

from jinja2 import Environment, FileSystemLoader
import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument("--template")
parser.add_argument("--pipeline-info")
parser.add_argument("--base-artifacts-url")
parser.add_argument("--device-type")
parser.add_argument("--kernel-image-name")
parser.add_argument("--kernel-image-type", nargs='?', default="")
parser.add_argument("--gpu-version")
parser.add_argument("--boot-method")
parser.add_argument("--lava-tags", nargs='?', default="")
parser.add_argument("--env-vars", nargs='?', default="")
parser.add_argument("--deqp-version")
parser.add_argument("--arch")
parser.add_argument("--ci-node-index")
parser.add_argument("--ci-node-total")
args = parser.parse_args()

env = Environment(loader = FileSystemLoader(os.path.dirname(args.template)), trim_blocks=True, lstrip_blocks=True)
template = env.get_template(os.path.basename(args.template))

values = {}
values['pipeline_info'] = args.pipeline_info
values['base_artifacts_url'] = args.base_artifacts_url
values['device_type'] = args.device_type
values['kernel_image_name'] = args.kernel_image_name
values['kernel_image_type'] = args.kernel_image_type
values['gpu_version'] = args.gpu_version
values['boot_method'] = args.boot_method
values['tags'] = args.lava_tags
values['env_vars'] = args.env_vars
values['deqp_version'] = args.deqp_version
values['arch'] = args.arch
values['ci_node_index'] = args.ci_node_index
values['ci_node_total'] = args.ci_node_total

f = open('lava-deqp.yml', "w")
f.write(template.render(values))
f.close()

