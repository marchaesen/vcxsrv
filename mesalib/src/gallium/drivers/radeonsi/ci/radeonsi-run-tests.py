#!/usr/bin/python3
#
# Copyright 2021 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
# THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.
#

import os
import sys
import argparse
import subprocess
import shutil
from datetime import datetime
import tempfile
import itertools
import filecmp
import multiprocessing
import csv


def print_red(txt, end_line=True, prefix=None):
    if prefix:
        print(prefix, end="")
    print("\033[0;31m{}\033[0m".format(txt), end="\n" if end_line else " ")


def print_yellow(txt, end_line=True, prefix=None):
    if prefix:
        print(prefix, end="")
    print("\033[1;33m{}\033[0m".format(txt), end="\n" if end_line else " ")


parser = argparse.ArgumentParser(description="radeonsi tester")
parser.add_argument(
    "--jobs",
    "-j",
    type=int,
    help="Number of processes/threads to use.",
    default=multiprocessing.cpu_count(),
)
parser.add_argument("--piglit-path", type=str, help="Path to piglit source folder.")
parser.add_argument("--glcts-path", type=str, help="Path to GLCTS source folder.")
parser.add_argument("--deqp-path", type=str, help="Path to dEQP source folder.")
parser.add_argument(
    "--parent-path",
    type=str,
    help="Path to folder containing piglit/GLCTS and dEQP source folders.",
    default=os.getenv("MAREKO_BUILD_PATH"),
)
parser.add_argument("--verbose", "-v", action="count", default=0)
parser.add_argument(
    "--include-tests",
    "-t",
    action="append",
    dest="include_tests",
    default=[],
    help="Only run the test matching this expression. This can only be a filename containing a list of failing tests to re-run.",
)

parser.add_argument(
    "--no-piglit", dest="piglit", help="Disable piglit tests", action="store_false"
)
parser.add_argument(
    "--no-glcts", dest="glcts", help="Disable GLCTS tests", action="store_false"
)
parser.add_argument(
    "--no-deqp", dest="deqp", help="Disable dEQP tests", action="store_false"
)
parser.add_argument(
    "--no-deqp-egl",
    dest="deqp_egl",
    help="Disable dEQP-EGL tests",
    action="store_false",
)
parser.add_argument(
    "--no-deqp-gles2",
    dest="deqp_gles2",
    help="Disable dEQP-gles2 tests",
    action="store_false",
)
parser.add_argument(
    "--no-deqp-gles3",
    dest="deqp_gles3",
    help="Disable dEQP-gles3 tests",
    action="store_false",
)
parser.add_argument(
    "--no-deqp-gles31",
    dest="deqp_gles31",
    help="Disable dEQP-gles31 tests",
    action="store_false",
)
parser.set_defaults(piglit=True)
parser.set_defaults(glcts=True)
parser.set_defaults(deqp=True)
parser.set_defaults(deqp_egl=True)
parser.set_defaults(deqp_gles2=True)
parser.set_defaults(deqp_gles3=True)
parser.set_defaults(deqp_gles31=True)

parser.add_argument("output_folder", help="Output folder (logs, etc)")

args = parser.parse_args(sys.argv[1:])

piglit_path = args.piglit_path
glcts_path = args.glcts_path
deqp_path = args.deqp_path

if args.parent_path:
    if args.piglit_path or args.glcts_path or args.deqp_path:
        parser.print_help()
        sys.exit(0)
    piglit_path = os.path.join(args.parent_path, "piglit")
    glcts_path = os.path.join(args.parent_path, "glcts")
    deqp_path = os.path.join(args.parent_path, "deqp")
else:
    if not args.piglit_path or not args.glcts_path or not args.deqp_path:
        parser.print_help()
        sys.exit(0)

base = os.path.dirname(__file__)
skips = os.path.join(base, "skips.csv")

# Use piglit's glinfo to determine the GPU name
gpu_name = "unknown"
p = subprocess.run(
    ["./glinfo"],
    capture_output="True",
    cwd=os.path.join(piglit_path, "bin"),
    check=True,
)
for line in p.stdout.decode().split("\n"):
    if "GL_RENDER" in line:
        gpu_name = line.replace("(TM)", "").split("(")[1].split(",")[0].lower()
        break

output_folder = args.output_folder
count = 1
while os.path.exists(output_folder):
    output_folder = "{}.{}".format(args.output_folder, count)
    count += 1

os.mkdir(output_folder)
new_baseline_folder = os.path.join(output_folder, "new_baseline")
os.mkdir(new_baseline_folder)

logfile = open(os.path.join(output_folder, "{}-run-tests.log".format(gpu_name)), "w")

spin = itertools.cycle("-\\|/")


def run_cmd(args, verbosity, env=None):
    if verbosity > 1:
        print_yellow(
            "| Command line argument '"
            + " ".join(['"{}"'.format(a) for a in args])
            + "'"
        )
    start = datetime.now()
    proc = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env
    )
    while True:
        line = proc.stdout.readline().decode()
        if verbosity > 0:
            if "ERROR" in line:
                print_red(line.strip(), prefix="| ")
            else:
                print("| " + line.strip())
        else:
            sys.stdout.write(next(spin))
            sys.stdout.flush()
            sys.stdout.write("\b")

        logfile.write(line)

        if proc.poll() is not None:
            break
    proc.wait()
    end = datetime.now()

    if verbosity == 0:
        sys.stdout.write(" ... ")

    print_yellow(
        "Completed in {} seconds".format(int((end - start).total_seconds())),
        prefix="└ " if verbosity > 0 else None,
    )


def verify_results(baseline1, baseline2):
    # We're not using baseline1 because piglit-runner/deqp-runner already are:
    #  - if no baseline, baseline2 will contain the list of failures
    #  - if there's a baseline, baseline2 will contain the diff
    # So in both cases, an empty baseline2 files means a successful run
    if len(open(baseline2, "r").readlines()) != 0:
        print_red("New errors. Check {}".format(baseline2))
        return False
    return True


def parse_test_filters(include_tests):
    cmd = []
    for t in include_tests:
        if os.path.exists(t):
            with open(t, "r") as file:
                for row in csv.reader(file, delimiter=","):
                    cmd += ["-t", row[0]]
        else:
            cmd += ["-t", t]
    return cmd


filters_args = parse_test_filters(args.include_tests)

# piglit test
if args.piglit:
    out = os.path.join(output_folder, "piglit")
    baseline = os.path.join(base, "{}-piglit-quick-fail.csv".format(gpu_name))
    new_baseline = os.path.join(
        new_baseline_folder, "{}-piglit-quick-fail.csv".format(gpu_name)
    )
    print_yellow("Running piglit tests", args.verbose > 0)
    cmd = [
        "piglit-runner",
        "run",
        "--piglit-folder",
        piglit_path,
        "--profile",
        "quick",
        "--output",
        out,
        "--process-isolation",
        "--timeout",
        "300",
        "--jobs",
        str(args.jobs),
        "--skips",
        skips,
    ] + filters_args

    if os.path.exists(baseline):
        cmd += ["--baseline", baseline]
    env = os.environ.copy()
    env["PIGLIT_PLATFORM"] = "gbm"
    run_cmd(cmd, args.verbose, env)
    shutil.copy(os.path.join(out, "failures.csv"), new_baseline)
    verify_results(baseline, new_baseline)

deqp_args = "-- --deqp-surface-width=256 --deqp-surface-height=256 --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-visibility=hidden".split(
    " "
)

# glcts test
if args.glcts:
    out = os.path.join(output_folder, "glcts")
    baseline = os.path.join(base, "{}-glcts-fail.csv".format(gpu_name))
    new_baseline = os.path.join(
        new_baseline_folder, "{}-glcts-fail.csv".format(gpu_name)
    )
    print_yellow("Running  GLCTS tests", args.verbose > 0)
    os.mkdir(os.path.join(output_folder, "glcts"))

    cmd = [
        "deqp-runner",
        "run",
        "--deqp",
        "{}/external/openglcts/modules/glcts".format(glcts_path),
        "--caselist",
        "{}/external/openglcts/modules/gl_cts/data/mustpass/gl/khronos_mustpass/4.6.1.x/gl46-master.txt".format(
            glcts_path
        ),
        "--output",
        out,
        "--skips",
        skips,
        "--jobs",
        str(args.jobs),
        "--timeout",
        "1000",
    ] + filters_args

    if os.path.exists(baseline):
        cmd += ["--baseline", baseline]
    cmd += deqp_args
    run_cmd(cmd, args.verbose)
    shutil.copy(os.path.join(out, "failures.csv"), new_baseline)
    verify_results(baseline, new_baseline)

if args.deqp:
    if args.include_tests:
        print_yellow("dEQP tests cannot be run with the -t/--include-tests option yet.")
        sys.exit(0)

    print_yellow("Running   dEQP tests", args.verbose > 0)

    # Generate a test-suite file
    suite_filename = os.path.join(output_folder, "deqp-suite.toml")
    suite = open(suite_filename, "w")
    os.mkdir(os.path.join(output_folder, "deqp"))
    baseline = os.path.join(base, "{}-deqp-fail.csv".format(gpu_name))
    new_baseline = os.path.join(
        new_baseline_folder, "{}-deqp-fail.csv".format(gpu_name)
    )

    deqp_tests = {
        "egl": args.deqp_egl,
        "gles2": args.deqp_gles2,
        "gles3": args.deqp_gles3,
        "gles31": args.deqp_gles31,
    }

    for k in deqp_tests:
        if not deqp_tests[k]:
            continue

        suite.write("[[deqp]]\n")
        suite.write(
            'deqp = "{}"\n'.format(
                "{}/modules/{subtest}/deqp-{subtest}".format(deqp_path, subtest=k)
            )
        )
        suite.write(
            'caselists = ["{}"]\n'.format(
                "{}/android/cts/master/{}-master.txt".format(deqp_path, k)
            )
        )
        if os.path.exists(baseline):
            suite.write('baseline = "{}"\n'.format(baseline))
        suite.write('skips = ["{}"]\n'.format(skips))
        suite.write("deqp_args = [\n")
        for a in deqp_args[1:-1]:
            suite.write('    "{}",\n'.format(a))
        suite.write('    "{}"\n'.format(deqp_args[-1]))
        suite.write("]\n")

    suite.close()

    cmd = [
        "deqp-runner",
        "suite",
        "--jobs",
        str(args.jobs),
        "--output",
        os.path.join(output_folder, "deqp"),
        "--suite",
        suite_filename,
    ]
    run_cmd(cmd, args.verbose)
    shutil.copy(os.path.join(out, "failures.csv"), new_baseline)
    verify_results(baseline, new_baseline)
