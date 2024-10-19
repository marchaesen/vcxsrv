#!/usr/bin/env python3

import argparse
import pathlib
import re


def detect_misleading_indentation(
    toml_path: str,
    toml_lines: list[str],
) -> bool:
    issue_detected = False
    previous_indentation = 0
    for line_number, line in enumerate(toml_lines, start=1):
        if match := re.match(r'^(\s*)\S', line):
            line_indentation = len(match.group(1))
            if line_indentation < previous_indentation:
                # Allow de-indenting when starting a new section (`[`) or
                # terminating a multi-line list (`]`)
                if not re.match(r'^\s*(\[|\])', line):
                    print(f'{toml_path}:{line_number}: '
                          f'Misleading indentation found')
                    issue_detected = True
        else:
            line_indentation = 0
        previous_indentation = line_indentation

    return issue_detected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'toml_files',
        type=pathlib.Path,
        nargs=argparse.ZERO_OR_MORE,
        help='*.toml files to lint (default: src/**/ci/*.toml)',
    )

    args = parser.parse_args()

    if not args.toml_files:
        args.toml_files = pathlib.Path('src').glob('**/ci/*.toml')

    error = False

    for path in args.toml_files:
        with path.open('r') as toml_file:
            toml_lines = toml_file.readlines()
        if detect_misleading_indentation(path.as_posix(), toml_lines):
            error = True

    if error:
        exit(1)


if __name__ == '__main__':
    main()
