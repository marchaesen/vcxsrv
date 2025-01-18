#!/usr/bin/python3
#
# Copyright 2013-2023 The Khronos Group Inc.
# Copyright 2023-2024 Google Inc.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys
import xml.etree.ElementTree as etree

sys.path.append(os.path.abspath(os.path.dirname(__file__)))

from cgenerator import CGeneratorOptions, COutputGenerator

from generator import write
from reg import Registry

# gfxstream + cereal modules
from cerealgenerator import CerealGenerator

from typing import Optional

def makeREstring(strings, default=None, strings_are_regex=False):
    """Turn a list of strings into a regexp string matching exactly those strings."""
    if strings or default is None:
        if not strings_are_regex:
            strings = (re.escape(s) for s in strings)
        return '^(' + '|'.join(strings) + ')$'
    return default


def makeGenOpts(args):
    """Returns a directory of [ generator function, generator options ] indexed
    by specified short names. The generator options incorporate the following
    parameters:

    args is an parsed argument object; see below for the fields that are used."""
    global genOpts
    genOpts = {}

    # Output target directory
    directory = args.directory

    # Descriptive names for various regexp patterns used to select
    # versions and extensions
    allFormats = allFeatures = allExtensions = r'.*'

    # Turn lists of names/patterns into matching regular expressions
    emitExtensionsPat    = makeREstring([], allExtensions)
    emitFormatsPat       = makeREstring([], allFormats)
    featuresPat          = makeREstring([], allFeatures)

    # Copyright text prefixing all headers (list of strings).
    # The SPDX formatting below works around constraints of the 'reuse' tool
    prefixStrings = [
        '/*',
        '** Copyright 2015-2023 The Khronos Group Inc.',
        '**',
        '** SPDX-License-Identifier' + ': Apache-2.0',
        '*/',
        ''
    ]

    # Text specific to Vulkan headers
    vkPrefixStrings = [
        '/*',
        '** This header is generated from the Khronos Vulkan XML API Registry.',
        '**',
        '*/',
        ''
    ]

    genOpts['cereal'] = [
            CerealGenerator,
            CGeneratorOptions(
                directory         = directory,
                versions          = featuresPat,
                emitversions      = featuresPat,
                addExtensions     = None,
                emitExtensions    = emitExtensionsPat,
                prefixText        = prefixStrings + vkPrefixStrings,
                apientry          = 'VKAPI_CALL ',
                apientryp         = 'VKAPI_PTR *',
                alignFuncParam    = 48)
        ]

    gfxstreamPrefixStrings = [
        '#pragma once',
        '#ifdef VK_GFXSTREAM_STRUCTURE_TYPE_EXT',
        '#include "vulkan_gfxstream_structure_type.h"',
        '#endif',
    ]

    # gfxstream specific header
    genOpts['vulkan_gfxstream.h'] = [
          COutputGenerator,
          CGeneratorOptions(
            filename          = 'vulkan_gfxstream.h',
            directory         = directory,
            versions          = featuresPat,
            emitversions      = None,
            addExtensions     = makeREstring(['VK_GOOGLE_gfxstream'], None),
            emitExtensions    = makeREstring(['VK_GOOGLE_gfxstream'], None),
            prefixText        = prefixStrings + vkPrefixStrings + gfxstreamPrefixStrings,
            # Use #pragma once in the prefixText instead, so that we can put the copyright comments
            # at the beginning of the file.
            apientry          = 'VKAPI_CALL ',
            apientryp         = 'VKAPI_PTR *',
            alignFuncParam    = 48)
        ]

def genTarget(args):
    """Create an API generator and corresponding generator options based on
    the requested target and command line options.

    This is encapsulated in a function so it can be profiled and/or timed.
    The args parameter is an parsed argument object containing the following
    fields that are used:

    - target - target to generate
    - directory - directory to generate it in
    - extensions - list of additional extensions to include in generated interfaces"""

    # Create generator options with parameters specified on command line
    makeGenOpts(args)

    # Select a generator matching the requested target
    if args.target in genOpts:
        createGenerator = genOpts[args.target][0]
        options = genOpts[args.target][1]

        gen = createGenerator(errFile=errWarn,
                              warnFile=errWarn,
                              diagFile=diag)
        return (gen, options)
    else:
        return None


# -feature name
# -extension name
# For both, "name" may be a single name, or a space-separated list
# of names, or a regular expression.
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-registry', action='store',
                        default='vk.xml',
                        help='Use specified registry file instead of vk.xml')
    parser.add_argument('-registryGfxstream', action='store',
                        default=None,
                        help='Use specified gfxstream registry file')
    parser.add_argument('-o', action='store', dest='directory',
                        default='.',
                        help='Create target and related files in specified directory')
    parser.add_argument('target', metavar='target', nargs='?',
                        help='Specify target')

    args = parser.parse_args()

    errWarn = sys.stderr
    diag = None

    # Create the API generator & generator options
    (gen, options) = genTarget(args)

    # Create the registry object with the specified generator and generator
    # options. The options are set before XML loading as they may affect it.
    reg = Registry(gen, options)

    # Parse the specified registry XML into an ElementTree object
    tree = etree.parse(args.registry)

    # Merge the gfxstream registry with the official Vulkan registry if the
    # target is the cereal generator
    if args.registryGfxstream is not None and args.target == 'cereal':
        treeGfxstream = etree.parse(args.registryGfxstream)
        treeRoot = tree.getroot()
        treeGfxstreamRoot = treeGfxstream.getroot()

        def getEntryName(entry) -> Optional[str]:
            name = entry.get("name")
            if name is not None:
                return name
            try:
                return entry.find("proto").find("name").text
            except AttributeError:
                return None

        for entriesName in ['types', 'commands', 'extensions']:
            treeEntries = treeRoot.find(entriesName)

            originalEntryDict = {}
            for entry in treeEntries:
                name = getEntryName(entry)
                if name is not None:
                    originalEntryDict[name] = entry

            for entry in treeGfxstreamRoot.find(entriesName):
                name = getEntryName(entry)
                # New entry, just append to entry list
                if name not in originalEntryDict.keys():
                    treeEntries.append(entry)
                    continue

                originalEntry = originalEntryDict[name]

                # Extending an existing entry. This happens for MVK.
                if entriesName == "extensions":
                    for key, value in entry.attrib.items():
                        originalEntry.set(key, value)
                    require = entry.find("require")
                    if require is not None:
                        for child in require:
                            originalEntry.find("require").append(child)
                    continue

                # Overwriting an existing entry. This happen for
                # VkNativeBufferANDROID
                if entriesName == "types" or entriesName == "commands":
                    originalEntry.clear()
                    originalEntry.attrib = entry.attrib
                    for child in entry:
                        originalEntry.append(child)

    # Load the XML tree into the registry object
    reg.loadElementTree(tree)
    reg.apiGen()
