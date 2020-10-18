#!/usr/bin/python3

# Copyright (c) 2019 Collabora Ltd
# Copyright © 2019-2020 Valve Corporation.
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
import os
import sys
import subprocess
from pathlib import Path
from traceutil import trace_type_from_filename, TraceType

def log(severity, msg, end='\n'):
    print("[dump_trace_images] %s: %s" % (severity, msg), flush=True, end=end)

def log_result(msg):
    print(msg, flush=True)

def run_logged_command(cmd, env, log_path):
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    logoutput = ("[dump_trace_images] Running: %s\n" % " ".join(cmd)).encode() + \
                ret.stdout
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open(mode='wb') as log:
        log.write(logoutput)
    if ret.returncode:
        raise RuntimeError(
            logoutput.decode(errors='replace') +
            "[dump_traces_images] Process failed with error code: %d" % ret.returncode)

def get_last_apitrace_frame_call(cmd_wrapper, trace_path):
    cmd = cmd_wrapper + ["apitrace", "dump", "--calls=frame", str(trace_path)]
    ret = subprocess.run(cmd, stdout=subprocess.PIPE)
    for l in reversed(ret.stdout.decode(errors='replace').splitlines()):
        s = l.split(None, 1)
        if len(s) >= 1 and s[0].isnumeric():
            return int(s[0])
    return -1

def get_last_gfxreconstruct_frame_call(trace_path):
    cmd = ["gfxrecon-info", str(trace_path)]
    ret = subprocess.run(cmd, stdout=subprocess.PIPE)
    lines = ret.stdout.decode(errors='replace').splitlines()
    if len(lines) >= 1:
        c = lines[0].split(": ", 1)
        if len(c) >= 2 and c[1].isnumeric():
            return int(c[1])
    return -1

def dump_with_apitrace(retrace_cmd, trace_path, calls, device_name):
    outputdir = str(trace_path.parent / "test" / device_name)
    os.makedirs(outputdir, exist_ok=True)
    outputprefix = str(Path(outputdir) / trace_path.name) + "-"
    if len(calls) == 0:
        calls = [str(get_last_apitrace_frame_call(retrace_cmd[:-1], trace_path))]
    cmd = retrace_cmd + ["--headless",
                         "--snapshot=" + ','.join(calls),
                         "--snapshot-prefix=" + outputprefix, str(trace_path)]
    log_path = Path(outputdir) / (trace_path.name + ".log")
    run_logged_command(cmd, None, log_path)

def dump_with_renderdoc(trace_path, calls, device_name):
    outputdir = str(trace_path.parent / "test" / device_name)
    script_path = Path(os.path.dirname(os.path.abspath(__file__)))
    cmd = [str(script_path / "renderdoc_dump_images.py"), str(trace_path), outputdir]
    cmd.extend(calls)
    log_path = Path(outputdir) / (trace_path.name + ".log")
    run_logged_command(cmd, None, log_path)

def dump_with_gfxreconstruct(trace_path, calls, device_name):
    from PIL import Image
    outputdir_path = trace_path.parent / "test" / device_name
    outputdir_path.mkdir(parents=True, exist_ok=True)
    outputprefix = str(outputdir_path / trace_path.name) + "-"
    if len(calls) == 0:
        # FIXME: The VK_LAYER_LUNARG_screenshot numbers the calls from
        # 0 to (total-num-calls - 1) while gfxreconstruct does it from
        # 1 to total-num-calls:
        # https://github.com/LunarG/gfxreconstruct/issues/284
        calls = [str(get_last_gfxreconstruct_frame_call(trace_path) - 1)]
    cmd = ["gfxrecon-replay", str(trace_path)]
    log_path = outputdir_path / (trace_path.name + ".log")
    env = os.environ.copy()
    env["VK_INSTANCE_LAYERS"] = "VK_LAYER_LUNARG_screenshot"
    env["VK_SCREENSHOT_FRAMES"] = ",".join(calls)
    env["VK_SCREENSHOT_DIR"] = str(outputdir_path)
    run_logged_command(cmd, env, log_path)
    for c in calls:
        ppm = str(outputdir_path / c) + ".ppm"
        outputfile = outputprefix + c + ".png"
        with log_path.open(mode='w') as log:
            log.write("Writing: %s to %s" % (ppm, outputfile))
        Image.open(ppm).save(outputfile)
        os.remove(ppm)

def dump_with_testtrace(trace_path, calls, device_name):
    from PIL import Image
    outputdir_path = trace_path.parent / "test" / device_name
    outputdir_path.mkdir(parents=True, exist_ok=True)
    with trace_path.open() as f:
        rgba = f.read()
    color = [int(rgba[0:2], 16), int(rgba[2:4], 16),
             int(rgba[4:6], 16), int(rgba[6:8], 16)]
    if len(calls) == 0: calls = ["0"]
    for c in calls:
        outputfile = str(outputdir_path / trace_path.name) + "-" + c + ".png"
        log_path = outputdir_path / (trace_path.name + ".log")
        with log_path.open(mode='w') as log:
            log.write("Writing RGBA: %s to %s" % (rgba, outputfile))
        Image.frombytes('RGBA', (32, 32), bytes(color * 32 * 32)).save(outputfile)

def dump_from_trace(trace_path, calls, device_name):
    log("Info", "Dumping trace %s" % trace_path, end='... ')
    trace_type = trace_type_from_filename(trace_path.name)
    try:
        if trace_type == TraceType.APITRACE:
            dump_with_apitrace(["eglretrace"], trace_path, calls, device_name)
        elif trace_type == TraceType.APITRACE_DXGI:
            dump_with_apitrace(["wine", "d3dretrace"], trace_path, calls, device_name)
        elif trace_type == TraceType.RENDERDOC:
            dump_with_renderdoc(trace_path, calls, device_name)
        elif trace_type == TraceType.GFXRECONSTRUCT:
            dump_with_gfxreconstruct(trace_path, calls, device_name)
        elif trace_type == TraceType.TESTTRACE:
            dump_with_testtrace(trace_path, calls, device_name)
        else:
            raise RuntimeError("Unknown tracefile extension")
        log_result("OK")
        return True
    except Exception as e:
        log_result("ERROR")
        log("Debug", "=== Failure log start ===")
        print(e)
        log("Debug", "=== Failure log end ===")
        return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('tracepath', help="trace to dump")
    parser.add_argument('--device-name', required=True,
                        help="the name of the graphics device used to produce images")
    parser.add_argument('--calls', required=False,
                        help="the call numbers from the trace to dump (default: last frame)")

    args = parser.parse_args()
    if args.calls is not None:
        args.calls = args.calls.split(",")
    else:
        args.calls = []

    success = dump_from_trace(Path(args.tracepath), args.calls, args.device_name)

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
