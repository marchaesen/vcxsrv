#!/usr/bin/python3 -i
#
# Copyright 2013-2023 The Khronos Group Inc.
# Copyright 2023-2024 Google Inc.
#
# SPDX-License-Identifier: Apache-2.0

import os
import re

from generator import (GeneratorOptions,
                       MissingGeneratorOptionsError, MissingRegistryError,
                       OutputGenerator, noneStr, regSortFeatures, write)

class CGeneratorOptions(GeneratorOptions):
    """CGeneratorOptions - subclass of GeneratorOptions.

    Adds options used by COutputGenerator objects during C language header
    generation."""

    def __init__(self,
                 prefixText='',
                 apientry='',
                 apientryp='',
                 alignFuncParam=0,
                 **kwargs
                 ):
        """Constructor.
        Additional parameters beyond parent class:

        - prefixText - list of strings to prefix generated header with
        (usually a copyright statement + calling convention macros)
        - apientry - string to use for the calling convention macro,
        in typedefs, such as APIENTRY
        - apientryp - string to use for the calling convention macro
        in function pointer typedefs, such as APIENTRYP
        - alignFuncParam - if nonzero and parameters are being put on a
        separate line, align parameter names at the specified column"""

        GeneratorOptions.__init__(self, **kwargs)

        self.prefixText = prefixText
        """list of strings to prefix generated header with (usually a copyright statement + calling convention macros)."""

        self.apientry = apientry
        """string to use for the calling convention macro, in typedefs, such as APIENTRY."""

        self.apientryp = apientryp
        """string to use for the calling convention macro in function pointer typedefs, such as APIENTRYP."""

        self.alignFuncParam = alignFuncParam
        """if nonzero and parameters are being put on a separate line, align parameter names at the specified column"""

class COutputGenerator(OutputGenerator):
    """Generates C-language API interfaces."""

    # This is an ordered list of sections in the header file.
    TYPE_SECTIONS = ['include', 'define', 'basetype', 'handle', 'enum',
                     'group', 'bitmask', 'funcpointer', 'struct']
    ALL_SECTIONS = TYPE_SECTIONS + ['commandPointer', 'command']

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Internal state - accumulators for different inner block text
        self.sections = {section: [] for section in self.ALL_SECTIONS}
        self.feature_not_empty = False

    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        # User-supplied prefix text, if any (list of strings)
        if genOpts.prefixText:
            for s in genOpts.prefixText:
                write(s, file=self.outFile)

        # C++ extern wrapper - after prefix lines so they can add includes.
        self.newline()
        write('#ifdef __cplusplus', file=self.outFile)
        write('extern "C" {', file=self.outFile)
        write('#endif', file=self.outFile)
        self.newline()

    def endFile(self):
        # C-specific
        # Finish C++ wrapper and multiple inclusion protection
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()
        self.newline()
        write('#ifdef __cplusplus', file=self.outFile)
        write('}', file=self.outFile)
        write('#endif', file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFile(self)

    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # C-specific
        # Accumulate includes, defines, types, enums, function pointer typedefs,
        # end function prototypes separately for this feature. They are only
        # printed in endFeature().
        self.sections = {section: [] for section in self.ALL_SECTIONS}
        self.feature_not_empty = False

    def endFeature(self):
        "Actually write the interface to the output file."
        # C-specific
        if self.emit:
            if self.feature_not_empty:
                if self.genOpts is None:
                    raise MissingGeneratorOptionsError()
                is_core = self.featureName and self.featureName.startswith('VK_VERSION_')
                self.newline()

                # Generate warning of possible use in IDEs
                write(f'// {self.featureName} is a preprocessor guard. Do not pass it to API calls.', file=self.outFile)
                write('#define', self.featureName, '1', file=self.outFile)
                for section in self.TYPE_SECTIONS:
                    contents = self.sections[section]
                    if contents:
                        write('\n'.join(contents), file=self.outFile)

                if self.sections['commandPointer']:
                    write('\n'.join(self.sections['commandPointer']), file=self.outFile)
                    self.newline()

                if self.sections['command']:
                    write('\n'.join(self.sections['command']), end='', file=self.outFile)

        # Finish processing in superclass
        OutputGenerator.endFeature(self)

    def appendSection(self, section, text):
        "Append a definition to the specified section"

        if section is None:
            exit(1)

        self.sections[section].append(text)
        self.feature_not_empty = True

    def genType(self, typeinfo, name, alias):
        "Generate type."
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem

        # Vulkan:
        # Determine the category of the type, and the type section to add
        # its definition to.
        # 'funcpointer' is added to the 'struct' section as a workaround for
        # internal issue #877, since structures and function pointer types
        # can have cross-dependencies.
        category = typeElem.get('category')
        if category == 'funcpointer':
            section = 'struct'
        else:
            section = category

        if category in ('struct', 'union'):
            # If the type is a struct type, generate it using the
            # special-purpose generator.
            self.genStruct(typeinfo, name, alias)
        else:
            if self.genOpts is None:
                raise MissingGeneratorOptionsError()
            # Replace <apientry /> tags with an APIENTRY-style string
            # (from self.genOpts). Copy other text through unchanged.
            # If the resulting text is an empty string, do not emit it.
            body = noneStr(typeElem.text)
            for elem in typeElem:
                if elem.tag == 'apientry':
                    body += self.genOpts.apientry + noneStr(elem.tail)
                else:
                    body += noneStr(elem.text) + noneStr(elem.tail)
            if body:
                # Add extra newline after multi-line entries.
                if '\n' in body[0:-1]:
                    body += '\n'
                self.appendSection(section, body)

    def genProtectString(self, protect_str):
        """Generate protection string.

        Protection strings are the strings defining the OS/Platform/Graphics
        requirements for a given API command.  When generating the
        language header files, we need to make sure the items specific to a
        graphics API or OS platform are properly wrapped in #ifs."""
        protect_if_str = ''
        protect_end_str = ''
        if not protect_str:
            return (protect_if_str, protect_end_str)

        if ',' in protect_str:
            protect_list = protect_str.split(',')
            protect_defs = ('defined(%s)' % d for d in protect_list)
            protect_def_str = ' && '.join(protect_defs)
            protect_if_str = '#if %s\n' % protect_def_str
            protect_end_str = '#endif // %s\n' % protect_def_str
        else:
            protect_if_str = '#ifdef %s\n' % protect_str
            protect_end_str = '#endif // %s\n' % protect_str

        return (protect_if_str, protect_end_str)

    def genStruct(self, typeinfo, typeName, alias):
        """Generate struct (e.g. C "struct" type).

        This is a special case of the <type> tag where the contents are
        interpreted as a set of <member> tags instead of freeform C
        C type declarations. The <member> tags are just like <param>
        tags - they are a declaration of a struct or union member.
        Only simple member declarations are supported (no nested
        structs etc.)

        If alias is not None, then this struct aliases another; just
        generate a typedef of that alias."""
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)

        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        typeElem = typeinfo.elem

        if alias:
            body = 'typedef ' + alias + ' ' + typeName + ';\n'
        else:
            body = ''
            (protect_begin, protect_end) = self.genProtectString(typeElem.get('protect'))
            if protect_begin:
                body += protect_begin

            body += 'typedef ' + typeElem.get('category')

            body += ' ' + typeName + ' {\n'

            targetLen = self.getMaxCParamTypeLength(typeinfo)
            for member in typeElem.findall('.//member'):
                body += self.makeCParamDecl(member, targetLen + 4)
                body += ';\n'
            body += '} ' + typeName + ';\n'
            if protect_end:
                body += protect_end

        self.appendSection('struct', body)

    def genGroup(self, groupinfo, groupName, alias=None):
        """Generate groups (e.g. C "enum" type).

        These are concatenated together with other types.

        If alias is not None, it is the name of another group type
        which aliases this type; just generate that alias."""
        OutputGenerator.genGroup(self, groupinfo, groupName, alias)
        groupElem = groupinfo.elem

        # After either enumerated type or alias paths, add the declaration
        # to the appropriate section for the group being defined.
        if groupElem.get('type') == 'bitmask':
            section = 'bitmask'
        else:
            section = 'group'

        if alias:
            # If the group name is aliased, just emit a typedef declaration
            # for the alias.
            body = 'typedef ' + alias + ' ' + groupName + ';\n'
            self.appendSection(section, body)

    def genEnum(self, enuminfo, name, alias):
        """Generate the C declaration for a constant (a single <enum> value).

        <enum> tags may specify their values in several ways, but are usually
        just integers."""

        OutputGenerator.genEnum(self, enuminfo, name, alias)

        body = self.buildConstantCDecl(enuminfo, name, alias)
        self.appendSection('enum', body)

    def genCmd(self, cmdinfo, name, alias):
        "Command generation"
        OutputGenerator.genCmd(self, cmdinfo, name, alias)

        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        prefix = ''
        decls = self.makeCDecls(cmdinfo.elem)
        self.appendSection('command', prefix + decls[0] + '\n')
        self.appendSection('commandPointer', decls[1])
