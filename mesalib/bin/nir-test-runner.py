#!/usr/bin/env python3

# Copyright © 2024 Valve Corporation
# SPDX-License-Identifier: MIT

import argparse
import collections
import subprocess
import os
import re
import sys
import tempfile
import textwrap
from pathlib import Path

class TestFileChange:
    def __init__(self, line, result):
        self.line = line
        self.result = result

class TestFileChanges:
    def __init__(self, name):
        self.name = name
        self.changes = []

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', '-B', required=False)
    parser.add_argument('--test-filter', '-f', required=False)
    parser.add_argument('--update-all', '-u', action='store_true')
    args = parser.parse_args()

    bin_path = 'src/compiler/nir/nir_tests'
    if args.build_dir:
        bin_path = args.build_dir + '/' + bin_path

    if not os.path.isfile(bin_path):
        print(f'{bin_path} \033[91m does not exist!\033[0m')
        exit(1)

    build_args = ['meson', 'compile']
    if args.build_dir:
        build_args.append(f'-C{args.build_dir}')
    subprocess.run(build_args)

    test_args = [bin_path]
    if args.test_filter:
        test_args.append(f'--gtest_filter={args.test_filter}')

    env = os.environ.copy()
    if args.update_all:
        env['NIR_TEST_DUMP_SHADERS'] = 'true'

    output = subprocess.run(test_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, env=env)

    expected_pattern = re.compile(r'Expected \(([\d\w\W/.-_]+):(\d+)\):')

    test_result = None
    expectations = collections.defaultdict(list)

    # Parse the output of the test binary and gather the changed shaders.
    for output_line in output.stdout.split('\n'):
        if output_line.startswith('Got:'):
            test_result = ''

            continue

        if output_line.startswith('Expected ('):
            match = expected_pattern.match(output_line)
            file = match.group(1).removeprefix('../')
            line = int(match.group(2))

            expectations[file].append(TestFileChange(line, test_result.strip()))

            test_result = None

            continue

        if test_result is not None:
            test_result += output_line + '\n'

    patches = []

    # Generate patches for the changed shaders.
    for file in expectations:
        changes = expectations[file]

        updated_test_file = ''
        change_index = 0
        line_index = 1
        inside_expectation = False

        with open(file) as test_file:
            for test_line in test_file:
                if test_line.strip().startswith(')\"'):
                    inside_expectation = False

                if not inside_expectation:
                    updated_test_file += test_line

                if change_index < len(changes) and line_index == changes[change_index].line:
                    inside_expectation = True
                    indentation = len(test_line) - len(test_line.lstrip()) + 3
                    updated_test_file += textwrap.indent(changes[change_index].result, " " * indentation) + '\n'
                    change_index += 1

                line_index += 1

        with tempfile.NamedTemporaryFile(delete_on_close=False) as tmp:
            tmp.write(bytes(updated_test_file, encoding="utf-8"))
            tmp.close()

            diff = subprocess.run(
                ['git', 'diff', '--no-index', file, tmp.name],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                universal_newlines=True,
            )
            patch = diff.stdout.replace(tmp.name, '/' + file)

            print(patch)

            patches.append(patch)

    if len(patches) != 0:
        sys.stdout.write('\033[96mApply the changes listed above?\033[0m [Y/n]')
        response = None
        try:
            response = input()
        except KeyboardInterrupt:
            print()
            sys.exit(1)

        if response in ['', 'y', 'Y']:
            for patch in patches:
                apply = subprocess.Popen(
                    ['git', 'apply', '--allow-empty'],
                    stdin=subprocess.PIPE,
                )
                apply.communicate(input=bytes(patch, encoding="utf-8"))
