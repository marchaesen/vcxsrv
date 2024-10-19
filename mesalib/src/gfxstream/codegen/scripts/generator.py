#!/usr/bin/python3 -i
#
# Copyright 2013-2023 The Khronos Group Inc.
# Copyright 2023-2024 Google Inc.
#
# SPDX-License-Identifier: Apache-2.0
"""Base class for source/header/doc generators, as well as some utility functions."""

from __future__ import unicode_literals

import io
import os
import re
import shutil
import sys
import tempfile
try:
    from pathlib import Path
except ImportError:
    from pathlib2 import Path  # type: ignore

CATEGORIES_REQUIRING_VALIDATION = set(('handle',
                                       'enum',
                                       'bitmask',
                                       'basetype',
                                       None))

# These are basic C types pulled in via openxr_platform_defines.h
TYPES_KNOWN_ALWAYS_VALID = set(('char',
                                'float',
                                'int8_t', 'uint8_t',
                                'int16_t', 'uint16_t',
                                'int32_t', 'uint32_t',
                                'int64_t', 'uint64_t',
                                'size_t',
                                'intptr_t', 'uintptr_t',
                                'int',
                                ))

def getElemName(elem, default=None):
    """Get the name associated with an element, either a name child or name attribute."""
    name_elem = elem.find('name')
    if name_elem is not None:
        return name_elem.text
    # Fallback if there is no child.
    return elem.get('name', default)


def getElemType(elem, default=None):
    """Get the type associated with an element, either a type child or type attribute."""
    type_elem = elem.find('type')
    if type_elem is not None:
        return type_elem.text
    # Fallback if there is no child.
    return elem.get('type', default)

def write(*args, **kwargs):
    file = kwargs.pop('file', sys.stdout)
    end = kwargs.pop('end', '\n')
    file.write(' '.join(str(arg) for arg in args))
    file.write(end)

def category_requires_validation(category):
    """Return True if the given type 'category' always requires validation.

    Defaults to a reasonable implementation.

    May override."""
    return category in CATEGORIES_REQUIRING_VALIDATION

def type_always_valid(typename):
    """Return True if the given type name is always valid (never requires validation).

    This is for things like integers.

    Defaults to a reasonable implementation.

    May override."""
    return typename in TYPES_KNOWN_ALWAYS_VALID

def noneStr(s):
    """Return string argument, or "" if argument is None.

    Used in converting etree Elements into text.
    s - string to convert"""
    if s:
        return s
    return ""

def regSortCategoryKey(feature):
    """Sort key for regSortFeatures.
    Sorts by category of the feature name string:

    - Core API features (those defined with a `<feature>` tag)
        - (sort VKSC after VK - this is Vulkan-specific)
    - ARB/KHR/OES (Khronos extensions)
    - other       (EXT/vendor extensions)"""

    if feature.elem.tag == 'feature':
        if feature.name.startswith('VKSC'):
            return 0.5
        else:
            return 0
    if (feature.category == 'ARB'
        or feature.category == 'KHR'
            or feature.category == 'OES'):
        return 1

    return 2


def regSortOrderKey(feature):
    """Sort key for regSortFeatures - key is the sortorder attribute."""

    return feature.sortorder


def regSortNameKey(feature):
    """Sort key for regSortFeatures - key is the extension name."""

    return feature.name


def regSortFeatureVersionKey(feature):
    """Sort key for regSortFeatures - key is the feature version.
    `<extension>` elements all have version number 0."""

    return float(feature.versionNumber)


def regSortExtensionNumberKey(feature):
    """Sort key for regSortFeatures - key is the extension number.
    `<feature>` elements all have extension number 0."""

    return int(feature.number)


def regSortFeatures(featureList):
    """Default sort procedure for features.

    - Sorts by explicit sort order (default 0) relative to other features
    - then by feature category ('feature' or 'extension'),
    - then by version number (for features)
    - then by extension number (for extensions)"""
    featureList.sort(key=regSortExtensionNumberKey)
    featureList.sort(key=regSortFeatureVersionKey)
    featureList.sort(key=regSortCategoryKey)
    featureList.sort(key=regSortOrderKey)


class MissingGeneratorOptionsError(RuntimeError):
    """Error raised when a Generator tries to do something that requires GeneratorOptions but it is None."""

    def __init__(self, msg=None):
        full_msg = 'Missing generator options object self.genOpts'
        if msg:
            full_msg += ': ' + msg
        super().__init__(full_msg)


class MissingRegistryError(RuntimeError):
    """Error raised when a Generator tries to do something that requires a Registry object but it is None."""

    def __init__(self, msg=None):
        full_msg = 'Missing Registry object self.registry'
        if msg:
            full_msg += ': ' + msg
        super().__init__(full_msg)

class GeneratorOptions:
    """Base class for options used during header/documentation production.

    These options are target language independent, and used by
    Registry.apiGen() and by base OutputGenerator objects."""

    def __init__(self,
                 filename=None,
                 directory='.',
                 versions='.*',
                 emitversions='.*',
                 addExtensions=None,
                 emitExtensions=None,
                 sortProcedure=regSortFeatures,
                ):
        """Constructor.

        Arguments:

        an object that implements ConventionsBase
        - filename - basename of file to generate, or None to write to stdout.
        - directory - directory in which to generate filename
        - versions - regex matching API versions to process interfaces for.
        Normally `'.*'` or `'[0-9][.][0-9]'` to match all defined versions.
        - emitversions - regex matching API versions to actually emit
        interfaces for (though all requested versions are considered
        when deciding which interfaces to generate). For GL 4.3 glext.h,
        this might be `'1[.][2-5]|[2-4][.][0-9]'`.
        - addExtensions - regex matching names of additional extensions
        to include. Defaults to None.
        - emitExtensions - regex matching names of extensions to actually emit
        interfaces for (though all requested versions are considered when
        deciding which interfaces to generate). Defaults to None.
        - sortProcedure - takes a list of FeatureInfo objects and sorts
        them in place to a preferred order in the generated output.

        Default is
          - core API versions
          - Khronos (ARB/KHR/OES) extensions
          - All other extensions
          - By core API version number or extension number in each group.

        The regex patterns can be None or empty, in which case they match
        nothing."""

        self.filename = filename
        "basename of file to generate, or None to write to stdout."

        self.directory = directory
        "directory in which to generate filename"

        self.versions = self.emptyRegex(versions)
        """regex matching API versions to process interfaces for.
        Normally `'.*'` or `'[0-9][.][0-9]'` to match all defined versions."""

        self.emitversions = self.emptyRegex(emitversions)
        """regex matching API versions to actually emit
        interfaces for (though all requested versions are considered
        when deciding which interfaces to generate). For GL 4.3 glext.h,
        this might be `'1[.][2-5]|[2-4][.][0-9]'`."""

        self.addExtensions = self.emptyRegex(addExtensions)
        """regex matching names of additional extensions
        to include. Defaults to None."""

        self.emitExtensions = self.emptyRegex(emitExtensions)
        """regex matching names of extensions to actually emit
        interfaces for (though all requested versions are considered when
        deciding which interfaces to generate)."""

        self.sortProcedure = sortProcedure
        """takes a list of FeatureInfo objects and sorts
        them in place to a preferred order in the generated output.
        Default is core API versions, ARB/KHR/OES extensions, all
        other extensions, alphabetically within each group."""

        self.registry = None
        """Populated later with the registry object."""

    def emptyRegex(self, pat):
        """Substitute a regular expression which matches no version
        or extension names for None or the empty string."""
        if not pat:
            return '_nomatch_^'

        return pat


class OutputGenerator:
    """Generate specified API interfaces in a specific style, such as a C header.

    Base class for generating API interfaces.
    Manages basic logic, logging, and output file control.
    Derived classes actually generate formatted output.
    """

    # categoryToPath - map XML 'category' to include file directory name
    categoryToPath = {
        'bitmask': 'flags',
        'enum': 'enums',
        'funcpointer': 'funcpointers',
        'handle': 'handles',
        'define': 'defines',
        'basetype': 'basetypes',
    }

    def __init__(self, errFile=sys.stderr, warnFile=sys.stderr, diagFile=sys.stdout):
        """Constructor

        - errFile, warnFile, diagFile - file handles to write errors,
          warnings, diagnostics to. May be None to not write."""
        self.outFile = None
        self.errFile = errFile
        self.warnFile = warnFile
        self.diagFile = diagFile
        # Internal state
        self.featureName = None
        """The current feature name being generated."""

        self.featureType = None
        """The current feature type being generated."""

        self.genOpts = None
        """The GeneratorOptions subclass instance."""

        self.registry = None
        """The specification registry object."""

        self.featureDictionary = {}
        """The dictionary of dictionaries of API features."""

        # Used for extension enum value generation
        self.extBase = 1000000000
        self.extBlockSize = 1000
        self.madeDirs = {}

        # API dictionary, which may be loaded by the beginFile method of
        # derived generators.
        self.apidict = None

    def enumToValue(self, elem, needsNum, bitwidth = 32,
                    forceSuffix = False, parent_for_alias_dereference=None):
        """Parse and convert an `<enum>` tag into a value.

        - elem - <enum> Element
        - needsNum - generate a numeric representation of the element value
        - bitwidth - size of the numeric representation in bits (32 or 64)
        - forceSuffix - if True, always use a 'U' / 'ULL' suffix on integers
        - parent_for_alias_dereference - if not None, an Element containing
          the parent of elem, used to look for elements this is an alias of

        Returns a list:

        - first element - integer representation of the value, or None
          if needsNum is False. The value must be a legal number
          if needsNum is True.
        - second element - string representation of the value

        There are several possible representations of values.

        - A 'value' attribute simply contains the value.
        - A 'bitpos' attribute defines a value by specifying the bit
          position which is set in that value.
        - An 'offset','extbase','extends' triplet specifies a value
          as an offset to a base value defined by the specified
          'extbase' extension name, which is then cast to the
          typename specified by 'extends'. This requires probing
          the registry database, and imbeds knowledge of the
          API extension enum scheme in this function.
        - An 'alias' attribute contains the name of another enum
          which this is an alias of. The other enum must be
          declared first when emitting this enum."""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        name = elem.get('name')
        numVal = None
        if 'value' in elem.keys():
            value = elem.get('value')
            # print('About to translate value =', value, 'type =', type(value))
            if needsNum:
                numVal = int(value, 0)
            # If there is a non-integer, numeric 'type' attribute (e.g. 'u' or
            # 'ull'), append it to the string value.
            # t = enuminfo.elem.get('type')
            # if t is not None and t != '' and t != 'i' and t != 's':
            #     value += enuminfo.type
            if forceSuffix:
              if bitwidth == 64:
                value = value + 'ULL'
              else:
                value = value + 'U'
            return [numVal, value]
        if 'bitpos' in elem.keys():
            value = elem.get('bitpos')
            bitpos = int(value, 0)
            numVal = 1 << bitpos
            value = '0x%08x' % numVal
            if bitwidth == 64 or bitpos >= 32:
              value = value + 'ULL'
            elif forceSuffix:
              value = value + 'U'
            return [numVal, value]
        if 'offset' in elem.keys():
            # Obtain values in the mapping from the attributes
            enumNegative = False
            offset = int(elem.get('offset'), 0)
            extnumber = int(elem.get('extnumber'), 0)
            extends = elem.get('extends')
            if 'dir' in elem.keys():
                enumNegative = True
            # Now determine the actual enumerant value, as defined
            # in the "Layers and Extensions" appendix of the spec.
            numVal = self.extBase + (extnumber - 1) * self.extBlockSize + offset
            if enumNegative:
                numVal *= -1
            value = '%d' % numVal
            # More logic needed!
            return [numVal, value]
        if 'alias' in elem.keys():
            alias_of = elem.get('alias')
            if parent_for_alias_dereference is None:
                return (None, alias_of)
            siblings = parent_for_alias_dereference.findall('enum')
            for sib in siblings:
                sib_name = sib.get('name')
                if sib_name == alias_of:
                    return self.enumToValue(sib, needsNum)
            raise RuntimeError("Could not find the aliased enum value")
        return [None, None]

    def buildConstantCDecl(self, enuminfo, name, alias):
        """Generate the C declaration for a constant (a single <enum>
        value).

        <enum> tags may specify their values in several ways, but are
        usually just integers or floating-point numbers."""

        (_, strVal) = self.enumToValue(enuminfo.elem, False)

        if enuminfo.elem.get('type') and not alias:
            # Generate e.g.: #define x (~0ULL)
            typeStr = enuminfo.elem.get('type');
            invert = '~' in strVal
            paren = '(' in strVal
            number = strVal.strip("()~UL")
            if typeStr != "float":
                if typeStr == "uint64_t":
                    number += 'ULL'
                else:
                    number += 'U'
            strVal = "~" if invert else ""
            strVal += number
            if paren:
                strVal = "(" + strVal + ")";
            body = '#define ' + name.ljust(33) + ' ' + strVal;
        else:
            body = '#define ' + name.ljust(33) + ' ' + strVal

        return body

    def beginFile(self, genOpts):
        """Start a new interface file

        - genOpts - GeneratorOptions controlling what is generated and how"""

        self.genOpts = genOpts
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        # Open a temporary file for accumulating output.
        if self.genOpts.filename is not None:
            self.outFile = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', delete=False)
        else:
            self.outFile = sys.stdout

    def endFile(self):
        if self.errFile:
            self.errFile.flush()
        if self.warnFile:
            self.warnFile.flush()
        if self.diagFile:
            self.diagFile.flush()
        if self.outFile:
            self.outFile.flush()
            if self.outFile != sys.stdout and self.outFile != sys.stderr:
                self.outFile.close()

            if self.genOpts is None:
                raise MissingGeneratorOptionsError()

            # On successfully generating output, move the temporary file to the
            # target file.
            if self.genOpts.filename is not None:
                if sys.platform == 'win32':
                    directory = Path(self.genOpts.directory)
                    if not Path.exists(directory):
                        os.makedirs(directory)
                shutil.copy(self.outFile.name, self.genOpts.directory + '/' + self.genOpts.filename)
                os.remove(self.outFile.name)
        self.genOpts = None

    def beginFeature(self, interface, emit):
        """Write interface for a feature and tag generated features as having been done.

        - interface - element for the `<version>` / `<extension>` to generate
        - emit - actually write to the header only when True"""
        self.emit = emit
        self.featureName = interface.get('name')
        self.featureType = interface.get('type')

    def endFeature(self):
        """Finish an interface file, closing it when done.

        Derived classes responsible for emitting feature"""
        self.featureName = None
        self.featureType = None

    def validateFeature(self, featureType, featureName):
        """Validate we are generating something only inside a `<feature>` tag"""
        if self.featureName is None:
            raise UserWarning('Attempt to generate', featureType,
                              featureName, 'when not in feature')

    def genType(self, typeinfo, name, alias):
        """Generate interface for a type

        - typeinfo - TypeInfo for a type

        Extend to generate as desired in your derived class."""
        self.validateFeature('type', name)

    def genStruct(self, typeinfo, typeName, alias):
        """Generate interface for a C "struct" type.

        - typeinfo - TypeInfo for a type interpreted as a struct

        Extend to generate as desired in your derived class."""
        self.validateFeature('struct', typeName)

        # The mixed-mode <member> tags may contain no-op <comment> tags.
        # It is convenient to remove them here where all output generators
        # will benefit.
        for member in typeinfo.elem.findall('.//member'):
            for comment in member.findall('comment'):
                member.remove(comment)

    def genGroup(self, groupinfo, groupName, alias):
        """Generate interface for a group of enums (C "enum")

        - groupinfo - GroupInfo for a group.

        Extend to generate as desired in your derived class."""

        self.validateFeature('group', groupName)

    def genEnum(self, enuminfo, typeName, alias):
        """Generate interface for an enum (constant).

        - enuminfo - EnumInfo for an enum
        - name - enum name

        Extend to generate as desired in your derived class."""
        self.validateFeature('enum', typeName)

    def genCmd(self, cmd, cmdinfo, alias):
        """Generate interface for a command.

        - cmdinfo - CmdInfo for a command

        Extend to generate as desired in your derived class."""
        self.validateFeature('command', cmdinfo)

    def makeProtoName(self, name, tail):
        """Turn a `<proto>` `<name>` into C-language prototype
        and typedef declarations for that name.

        - name - contents of `<name>` tag
        - tail - whatever text follows that tag in the Element"""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()
        return self.genOpts.apientry + name + tail

    def makeTypedefName(self, name, tail):
        """Make the function-pointer typedef name for a command."""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()
        return '(' + self.genOpts.apientryp + 'PFN_' + name + tail + ')'

    def makeCParamDecl(self, param, aligncol):
        """Return a string which is an indented, formatted
        declaration for a `<param>` or `<member>` block (e.g. function parameter
        or structure/union member).

        - param - Element (`<param>` or `<member>`) to format
        - aligncol - if non-zero, attempt to align the nested `<name>` element
          at this column"""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()
        indent = '    '
        paramdecl = indent
        prefix = noneStr(param.text)

        for elem in param:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)

            if elem.tag == 'name' and aligncol > 0:
                # Align at specified column, if possible
                paramdecl = paramdecl.rstrip()
                oldLen = len(paramdecl)
                # This works around a problem where very long type names -
                # longer than the alignment column - would run into the tail
                # text.
                paramdecl = paramdecl.ljust(aligncol - 1) + ' '
                newLen = len(paramdecl)

            paramdecl += prefix + text + tail

            # Clear prefix for subsequent iterations
            prefix = ''

        paramdecl = paramdecl + prefix

        if aligncol == 0:
            # Squeeze out multiple spaces other than the indentation
            paramdecl = indent + ' '.join(paramdecl.split())
        return paramdecl

    def getCParamTypeLength(self, param):
        """Return the length of the type field is an indented, formatted
        declaration for a `<param>` or `<member>` block (e.g. function parameter
        or structure/union member).

        - param - Element (`<param>` or `<member>`) to identify"""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()

        # Allow for missing <name> tag
        newLen = 0
        paramdecl = '    ' + noneStr(param.text)
        for elem in param:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)

            if elem.tag == 'name':
                # Align at specified column, if possible
                newLen = len(paramdecl.rstrip())
            paramdecl += text + tail

        return newLen

    def getMaxCParamTypeLength(self, info):
        """Return the length of the longest type field for a member/parameter.
        - info - TypeInfo or CommandInfo.
        """
        lengths = (self.getCParamTypeLength(member)
                   for member in info.getMembers())
        return max(lengths)

    def getTypeCategory(self, typename):
        """Get the category of a type."""
        if self.registry is None:
            raise MissingRegistryError()

        info = self.registry.typedict.get(typename)
        if info is None:
            return None

        elem = info.elem
        if elem is not None:
            return elem.get('category')
        return None

    def isStructAlwaysValid(self, structname):
        """Try to do check if a structure is always considered valid (i.e. there is no rules to its acceptance)."""
        # A conventions object is required for this call.
        if self.registry is None:
            raise MissingRegistryError()

        if type_always_valid(structname):
            return True

        category = self.getTypeCategory(structname)
        if category_requires_validation(category):
            return False

        info = self.registry.typedict.get(structname)
        members = info.getMembers()

        for member in members:
            member_name = getElemName(member)
            if member_name in ('sType', 'pNext'):
                return False

            if member.get('noautovalidity'):
                return False

            member_type = getElemType(member)

            if member_type in ('void', 'char') or self.paramIsArray(member) or self.paramIsPointer(member):
                return False

            if type_always_valid(member_type):
                continue

            member_category = self.getTypeCategory(member_type)

            if category_requires_validation(member_category):
                return False

            if member_category in ('struct', 'union'):
                if self.isStructAlwaysValid(member_type) is False:
                    return False

        return True

    def paramIsArray(self, param):
        """Check if the parameter passed in is a pointer to an array.

        param           the XML information for the param
        """
        return param.get('len') is not None

    def paramIsPointer(self, param):
        """Check if the parameter passed in is a pointer.

        param           the XML information for the param
        """
        tail = param.find('type').tail
        return tail is not None and '*' in tail

    def isEnumRequired(self, elem):
        """Return True if this `<enum>` element is
        required, False otherwise

        - elem - `<enum>` element to test"""
        required = elem.get('required') is not None
        return required

        # @@@ This code is overridden by equivalent code now run in
        # @@@ Registry.generateFeature

        required = False

        extname = elem.get('extname')
        if extname is not None:
            # 'supported' attribute was injected when the <enum> element was
            # moved into the <enums> group in Registry.parseTree()
            if 'vulkan' == elem.get('supported'):
                required = True
            elif re.match(self.genOpts.addExtensions, extname) is not None:
                required = True
        elif elem.get('version') is not None:
            required = re.match(self.genOpts.emitversions, elem.get('version')) is not None
        else:
            required = True

        return required

    def makeCDecls(self, cmd):
        """Return C prototype and function pointer typedef for a
        `<command>` Element, as a two-element list of strings.

        - cmd - Element containing a `<command>` tag"""
        if self.genOpts is None:
            raise MissingGeneratorOptionsError()
        proto = cmd.find('proto')
        params = cmd.findall('param')
        # Begin accumulating prototype and typedef strings
        pdecl = 'VKAPI_ATTR '
        tdecl = 'typedef '

        # Insert the function return type/name.
        # For prototypes, add APIENTRY macro before the name
        # For typedefs, add (APIENTRY *<name>) around the name and
        #   use the PFN_cmdnameproc naming convention.
        # Done by walking the tree for <proto> element by element.
        # etree has elem.text followed by (elem[i], elem[i].tail)
        #   for each child element and any following text
        # Leading text
        pdecl += noneStr(proto.text)
        tdecl += noneStr(proto.text)
        # For each child element, if it is a <name> wrap in appropriate
        # declaration. Otherwise append its contents and tail contents.
        for elem in proto:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)
            if elem.tag == 'name':
                pdecl += self.makeProtoName(text, tail)
                tdecl += self.makeTypedefName(text, tail)
            else:
                pdecl += text + tail
                tdecl += text + tail

        if self.genOpts.alignFuncParam == 0:
            # Squeeze out multiple spaces - there is no indentation
            pdecl = ' '.join(pdecl.split())
            tdecl = ' '.join(tdecl.split())

        # Now add the parameter declaration list, which is identical
        # for prototypes and typedefs. Concatenate all the text from
        # a <param> node without the tags. No tree walking required
        # since all tags are ignored.
        # Uses: self.indentFuncProto
        # self.indentFuncPointer
        # self.alignFuncParam
        n = len(params)
        # Indented parameters
        if n > 0:
            indentdecl = '(\n'
            indentdecl += ',\n'.join(self.makeCParamDecl(p, self.genOpts.alignFuncParam)
                                     for p in params)
            indentdecl += ');'
        else:
            indentdecl = '(void);'
        # Non-indented parameters
        paramdecl = '('
        if n > 0:
            paramnames = []
            paramnames = (''.join(t for t in p.itertext())
                          for p in params)
            paramdecl += ', '.join(paramnames)
        else:
            paramdecl += 'void'
        paramdecl += ");"
        return [pdecl + indentdecl, tdecl + paramdecl]

    def newline(self):
        """Print a newline to the output file (utility function)"""
        write('', file=self.outFile)

    def setRegistry(self, registry):
        self.registry = registry
