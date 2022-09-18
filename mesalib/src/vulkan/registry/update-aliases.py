#!/usr/bin/env python3
"""
Check for and replace aliases with their new names from vk.xml
"""

import argparse
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as et

THIS_FILE = pathlib.Path(__file__)
CWD = pathlib.Path.cwd()

VK_XML = THIS_FILE.parent / 'vk.xml'
EXCLUDE_PATHS = [
    VK_XML.relative_to(CWD).as_posix(),

    # These files come from other repos, there's no point checking and
    # fixing them here as that would be overwritten in the next sync.
    'src/amd/vulkan/radix_sort/',
    'src/virtio/venus-protocol/',
]


def get_aliases(xml_file: pathlib.Path):
    """
    Get all the aliases defined in vk.xml
    """
    xml = et.parse(xml_file)

    for node in ([]
        + xml.findall('.//enum[@alias]')
        + xml.findall('.//type[@alias]')
        + xml.findall('.//command[@alias]')
    ):
        yield node.attrib['name'], node.attrib['alias']


def remove_prefix(string: str, prefix: str):
    """
    Remove prefix if string starts with it, and return the full string
    otherwise.
    """
    if not string.startswith(prefix):
        return string
    return string[len(prefix):]


# Function from https://stackoverflow.com/a/312464
def chunks(lst: list, n: int):
    """
    Yield successive n-sized chunks from lst.
    """
    for i in range(0, len(lst), n):
        yield lst[i:i + n]


def main(check_only: bool):
    """
    Entrypoint; perform the search for all the aliases, and if `check_only`
    is not True, replace them.
    """
    def prepare_identifier(identifier: str) -> str:
        # vk_find_struct() prepends `VK_STRUCTURE_TYPE_`, so that prefix
        # might not appear in the code
        identifier = remove_prefix(identifier, 'VK_STRUCTURE_TYPE_')
        return identifier

    aliases = {}
    for old_name, alias_for in get_aliases(VK_XML):
        old_name = prepare_identifier(old_name)
        alias_for = prepare_identifier(alias_for)
        aliases[old_name] = alias_for

    print(f'Found {len(aliases)} aliases in {VK_XML.name}')

    # Some aliases have aliases
    recursion_needs_checking = True
    while recursion_needs_checking:
        recursion_needs_checking = False
        for old, new in aliases.items():
            if new in aliases:
                aliases[old] = aliases[new]
                recursion_needs_checking = True

    # Doing the whole search in a single command breaks grep, so only
    # look for 500 aliases at a time. Searching them one at a time would
    # be extremely slow.
    files_with_aliases = set()
    for aliases_chunk in chunks([*aliases], 500):
        search_output = subprocess.check_output([
            'git',
            'grep',
            '-rlP',
            '|'.join(aliases_chunk),
            'src/'
        ], stderr=subprocess.DEVNULL).decode()
        files_with_aliases.update(search_output.splitlines())

    def file_matches_path(file: str, path: str) -> bool:
        # if path is a folder; match any file within
        if path.endswith('/') and file.startswith(path):
            return True
        return file == path

    for excluded_path in EXCLUDE_PATHS:
        files_with_aliases = {
            file for file in files_with_aliases
            if not file_matches_path(file, excluded_path)
        }

    if not files_with_aliases:
        print('No alias found in any file.')
        sys.exit(0)

    print(f'{len(files_with_aliases)} files contain aliases:')
    print('\n'.join(f'- {file}' for file in files_with_aliases))

    if check_only:
        print('You can automatically fix this by running '
              f'`{THIS_FILE.relative_to(CWD)}`.')
        sys.exit(1)

    command = [
        'sed',
        '-i',
        ";".join([f's/{old}/{new}/g' for old, new in aliases.items()]),
    ]
    command += files_with_aliases
    subprocess.check_call(command, stderr=subprocess.DEVNULL)
    print('All aliases have been replaced')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check-only',
                        action='store_true',
                        help='Replace aliases found')
    args = parser.parse_args()
    main(**vars(args))
