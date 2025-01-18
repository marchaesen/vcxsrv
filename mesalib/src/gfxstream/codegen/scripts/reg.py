#!/usr/bin/python3 -i
#
# Copyright 2013-2023 The Khronos Group Inc.
# Copyright 2023-2024 Google Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Types and classes for manipulating an API registry."""

import copy
import re
import sys
import xml.etree.ElementTree as etree
from collections import defaultdict, deque, namedtuple

from generator import GeneratorOptions, OutputGenerator, noneStr, write

def apiNameMatch(str, supported):
    """Return whether a required api name matches a pattern specified for an
    XML <feature> 'api' attribute or <extension> 'supported' attribute.

    - str - API name such as 'vulkan' or 'openxr'. May be None, in which
        case it never matches (this should not happen).
    - supported - comma-separated list of XML API names. May be None, in
        which case str always matches (this is the usual case)."""

    if str is not None:
        return supported is None or str in supported.split(',')

    # Fallthrough case - either str is None or the test failed
    return False

def matchAPIProfile(api, elem):
    """Return whether an API and profile
    like `"gl(core)|gles1(common-lite)"`."""
    # Match 'api', if present
    elem_api = elem.get('api')
    if elem_api:
        if api is None:
            raise UserWarning("No API requested, but 'api' attribute is present with value '"
                              + elem_api + "'")
        elif api != elem_api:
            # Requested API does not match attribute
            return False
    return True

class BaseInfo:
    """Base class for information about a registry feature
    (type/group/enum/command/API/extension).

    Represents the state of a registry feature, used during API generation.
    """

    def __init__(self, elem):
        self.required = False
        """should this feature be defined during header generation
        (has it been removed by a profile or version)?"""

        self.declared = False
        "has this feature been defined already?"

        self.elem = elem
        "etree Element for this feature"

    def resetState(self):
        """Reset required/declared to initial values. Used
        prior to generating a new API interface."""
        self.required = False
        self.declared = False

    def compareKeys(self, info, key, required = False):
        """Return True if self.elem and info.elem have the same attribute
           value for key.
           If 'required' is not True, also returns True if neither element
           has an attribute value for key."""

        if required and key not in self.elem.keys():
            return False
        return self.elem.get(key) == info.elem.get(key)

    def compareElem(self, info, infoName):
        """Return True if self.elem and info.elem have the same definition.
        info - the other object
        infoName - 'type' / 'group' / 'enum' / 'command' / 'feature' /
                   'extension'"""

        if infoName == 'enum':
            if self.compareKeys(info, 'extends'):
                # Either both extend the same type, or no type
                if (self.compareKeys(info, 'value', required = True) or
                    self.compareKeys(info, 'bitpos', required = True)):
                    # If both specify the same value or bit position,
                    # they are equal
                    return True
                elif (self.compareKeys(info, 'extnumber') and
                      self.compareKeys(info, 'offset') and
                      self.compareKeys(info, 'dir')):
                    # If both specify the same relative offset, they are equal
                    return True
                elif (self.compareKeys(info, 'alias')):
                    # If both are aliases of the same value
                    return True
                else:
                    return False
            else:
                # The same enum cannot extend two different types
                return False
        else:
            # Non-<enum>s should never be redefined
            return False


class TypeInfo(BaseInfo):
    """Registry information about a type. No additional state
      beyond BaseInfo is required."""

    def __init__(self, elem):
        BaseInfo.__init__(self, elem)
        self.additionalValidity = []
        self.removedValidity = []

    def getMembers(self):
        """Get a collection of all member elements for this type, if any."""
        return self.elem.findall('member')

    def resetState(self):
        BaseInfo.resetState(self)
        self.additionalValidity = []
        self.removedValidity = []


class GroupInfo(BaseInfo):
    """Registry information about a group of related enums
    in an <enums> block, generally corresponding to a C "enum" type."""

    def __init__(self, elem):
        BaseInfo.__init__(self, elem)


class EnumInfo(BaseInfo):
    """Registry information about an enum"""

    def __init__(self, elem):
        BaseInfo.__init__(self, elem)
        self.type = elem.get('type')
        """numeric type of the value of the <enum> tag
        ( '' for GLint, 'u' for GLuint, 'ull' for GLuint64 )"""
        if self.type is None:
            self.type = ''


class CmdInfo(BaseInfo):
    """Registry information about a command"""

    def __init__(self, elem):
        BaseInfo.__init__(self, elem)
        self.additionalValidity = []
        self.removedValidity = []

    def getParams(self):
        """Get a collection of all param elements for this command, if any."""
        return self.elem.findall('param')

    def resetState(self):
        BaseInfo.resetState(self)
        self.additionalValidity = []
        self.removedValidity = []


class FeatureInfo(BaseInfo):
    """Registry information about an API <feature>
    or <extension>."""

    def __init__(self, elem):
        BaseInfo.__init__(self, elem)
        self.name = elem.get('name')
        "feature name string (e.g. 'VK_KHR_surface')"

        self.emit = False
        "has this feature been defined already?"

        self.sortorder = int(elem.get('sortorder', 0))
        """explicit numeric sort key within feature and extension groups.
        Defaults to 0."""

        # Determine element category (vendor). Only works
        # for <extension> elements.
        if elem.tag == 'feature':
            # Element category (vendor) is meaningless for <feature>
            self.category = 'VERSION'
            """category, e.g. VERSION or khr/vendor tag"""

            self.version = elem.get('name')
            """feature name string"""

            self.versionNumber = elem.get('number')
            """versionNumber - API version number, taken from the 'number'
               attribute of <feature>. Extensions do not have API version
               numbers and are assigned number 0."""

            self.number = 0
            self.supported = None
        else:
            # Extract vendor portion of <APIprefix>_<vendor>_<name>
            self.category = self.name.split('_', 2)[1]
            self.version = "0"
            self.versionNumber = "0"

            self.number = int(elem.get('number','0'))
            """extension number, used for ordering and for assigning
            enumerant offsets. <feature> features do not have extension
            numbers and are assigned number 0, as are extensions without
            numbers, so sorting works."""

            self.supported = elem.get('supported', 'disabled')

class Registry:
    """Object representing an API registry, loaded from an XML file."""

    def __init__(self, gen=None, genOpts=None):
        if gen is None:
            # If not specified, give a default object so messaging will work
            self.gen = OutputGenerator()
        else:
            self.gen = gen
        "Output generator used to write headers / messages"

        if genOpts is None:
            # If no generator is provided, we may still need the XML API name
            # (for example, in genRef.py).
            self.genOpts = GeneratorOptions(apiname = 'vulkan')
        else:
            self.genOpts = genOpts
        "Options controlling features to write and how to format them"

        self.gen.registry = self
        self.gen.genOpts = self.genOpts
        self.gen.genOpts.registry = self

        self.tree = None
        "ElementTree containing the root `<registry>`"

        self.typedict = {}
        "dictionary of TypeInfo objects keyed by type name"

        self.groupdict = {}
        "dictionary of GroupInfo objects keyed by group name"

        self.enumdict = {}
        "dictionary of EnumInfo objects keyed by enum name"

        self.cmddict = {}
        "dictionary of CmdInfo objects keyed by command name"

        self.apidict = {}
        "dictionary of FeatureInfo objects for `<feature>` elements keyed by API name"

        self.extensions = []
        "list of `<extension>` Elements"

        self.extdict = {}
        "dictionary of FeatureInfo objects for `<extension>` elements keyed by extension name"

        self.emitFeatures = False
        """True to actually emit features for a version / extension,
        or False to just treat them as emitted"""

        self.filename     = None

    def loadElementTree(self, tree):
        """Load ElementTree into a Registry object and parse it."""
        self.tree = tree
        self.parseTree()

    def loadFile(self, file):
        """Load an API registry XML file into a Registry object and parse it"""
        self.filename = file
        self.tree = etree.parse(file)
        self.parseTree()

    def setGenerator(self, gen):
        """Specify output generator object.

        `None` restores the default generator."""
        self.gen = gen
        self.gen.setRegistry(self)

    def addElementInfo(self, elem, info, infoName, dictionary):
        """Add information about an element to the corresponding dictionary.

        Intended for internal use only.

        - elem - `<type>`/`<enums>`/`<enum>`/`<command>`/`<feature>`/`<extension>`/`<spirvextension>`/`<spirvcapability>`/`<format>`/`<syncstage>`/`<syncaccess>`/`<syncpipeline>` Element
        - info - corresponding {Type|Group|Enum|Cmd|Feature|Spirv|Format|SyncStage|SyncAccess|SyncPipeline}Info object
        - infoName - 'type' / 'group' / 'enum' / 'command' / 'feature' / 'extension' / 'spirvextension' / 'spirvcapability' / 'format' / 'syncstage' / 'syncaccess' / 'syncpipeline'
        - dictionary - self.{type|group|enum|cmd|api|ext|format|spirvext|spirvcap|sync}dict

        The dictionary key is the element 'name' attribute."""

        key = elem.get('name')
        if key in dictionary:
            if not dictionary[key].compareElem(info, infoName):
                return
        else:
            dictionary[key] = info

    def lookupElementInfo(self, fname, dictionary):
        """Find a {Type|Enum|Cmd}Info object by name.

        Intended for internal use only.

        If an object qualified by API name exists, use that.

        - fname - name of type / enum / command
        - dictionary - self.{type|enum|cmd}dict"""
        key = (fname, 'vulkan')
        if key in dictionary:
            return dictionary[key]
        if fname in dictionary:
            return dictionary[fname]

        return None

    def parseTree(self):
        """Parse the registry Element, once created"""
        # This must be the Element for the root <registry>
        if self.tree is None:
            raise RuntimeError("Tree not initialized!")
        self.reg = self.tree.getroot()

        # There is usually one <types> block; more are OK
        # Required <type> attributes: 'name' or nested <name> tag contents
        self.typedict = {}
        for type_elem in self.reg.findall('types/type'):
            # If the <type> does not already have a 'name' attribute, set
            # it from contents of its <name> tag.
            if type_elem.get('name') is None:
                name_elem = type_elem.find('name')
                if name_elem is None or not name_elem.text:
                    raise RuntimeError("Type without a name!")
                type_elem.set('name', name_elem.text)
            self.addElementInfo(type_elem, TypeInfo(type_elem), 'type', self.typedict)

        # Create dictionary of registry enum groups from <enums> tags.
        #
        # Required <enums> attributes: 'name'. If no name is given, one is
        # generated, but that group cannot be identified and turned into an
        # enum type definition - it is just a container for <enum> tags.
        self.groupdict = {}
        for group in self.reg.findall('enums'):
            self.addElementInfo(group, GroupInfo(group), 'group', self.groupdict)

        # Create dictionary of registry enums from <enum> tags
        #
        # <enums> tags usually define different namespaces for the values
        #   defined in those tags, but the actual names all share the
        #   same dictionary.
        # Required <enum> attributes: 'name', 'value'
        # For containing <enums> which have type="enum" or type="bitmask",
        # tag all contained <enum>s are required. This is a stopgap until
        # a better scheme for tagging core and extension enums is created.
        self.enumdict = {}
        for enums in self.reg.findall('enums'):
            required = (enums.get('type') is not None)
            for enum in enums.findall('enum'):
                enumInfo = EnumInfo(enum)
                enumInfo.required = required
                self.addElementInfo(enum, enumInfo, 'enum', self.enumdict)

        # Create dictionary of registry commands from <command> tags
        # and add 'name' attribute to each <command> tag (where missing)
        # based on its <proto><name> element.
        #
        # There is usually only one <commands> block; more are OK.
        # Required <command> attributes: 'name' or <proto><name> tag contents
        self.cmddict = {}
        # List of commands which alias others. Contains
        #   [ aliasName, element ]
        # for each alias
        cmdAlias = []
        for cmd in self.reg.findall('commands/command'):
            # If the <command> does not already have a 'name' attribute, set
            # it from contents of its <proto><name> tag.
            name = cmd.get('name')
            if name is None:
                name_elem = cmd.find('proto/name')
                if name_elem is None or not name_elem.text:
                    raise RuntimeError("Command without a name!")
                name = cmd.set('name', name_elem.text)
            ci = CmdInfo(cmd)
            self.addElementInfo(cmd, ci, 'command', self.cmddict)
            alias = cmd.get('alias')
            if alias:
                cmdAlias.append([name, alias, cmd])

        # Now loop over aliases, injecting a copy of the aliased command's
        # Element with the aliased prototype name replaced with the command
        # name - if it exists.
        for (name, alias, cmd) in cmdAlias:
            if alias in self.cmddict:
                aliasInfo = self.cmddict[alias]
                cmdElem = copy.deepcopy(aliasInfo.elem)
                cmdElem.find('proto/name').text = name
                cmdElem.set('name', name)
                cmdElem.set('alias', alias)
                ci = CmdInfo(cmdElem)
                # Replace the dictionary entry for the CmdInfo element
                self.cmddict[name] = ci

        # Create dictionaries of API and extension interfaces
        #   from toplevel <api> and <extension> tags.
        self.apidict = {}
        for feature in self.reg.findall('feature'):
            featureInfo = FeatureInfo(feature)
            self.addElementInfo(feature, featureInfo, 'feature', self.apidict)

            # Add additional enums defined only in <feature> tags
            # to the corresponding enumerated type.
            # When seen here, the <enum> element, processed to contain the
            # numeric enum value, is added to the corresponding <enums>
            # element, as well as adding to the enum dictionary. It is no
            # longer removed from the <require> element it is introduced in.
            # Instead, generateRequiredInterface ignores <enum> elements
            # that extend enumerated types.
            #
            # For <enum> tags which are actually just constants, if there is
            # no 'extends' tag but there is a 'value' or 'bitpos' tag, just
            # add an EnumInfo record to the dictionary. That works because
            # output generation of constants is purely dependency-based, and
            # does not need to iterate through the XML tags.
            for elem in feature.findall('require'):
                for enum in elem.findall('enum'):
                    addEnumInfo = False
                    groupName = enum.get('extends')
                    if groupName is not None:
                        # Add version number attribute to the <enum> element
                        enum.set('version', featureInfo.version)
                        # Look up the GroupInfo with matching groupName
                        if groupName in self.groupdict:
                            gi = self.groupdict[groupName]
                            gi.elem.append(copy.deepcopy(enum))
                        addEnumInfo = True
                    elif enum.get('value') or enum.get('bitpos') or enum.get('alias'):
                        addEnumInfo = True
                    if addEnumInfo:
                        enumInfo = EnumInfo(enum)
                        self.addElementInfo(enum, enumInfo, 'enum', self.enumdict)

        self.extensions = self.reg.findall('extensions/extension')
        self.extdict = {}
        for feature in self.extensions:
            featureInfo = FeatureInfo(feature)
            self.addElementInfo(feature, featureInfo, 'extension', self.extdict)

            # Add additional enums defined only in <extension> tags
            # to the corresponding core type.
            # Algorithm matches that of enums in a "feature" tag as above.
            #
            # This code also adds a 'extnumber' attribute containing the
            # extension number, used for enumerant value calculation.
            for elem in feature.findall('require'):
                for enum in elem.findall('enum'):
                    addEnumInfo = False
                    groupName = enum.get('extends')
                    if groupName is not None:

                        # Add <extension> block's extension number attribute to
                        # the <enum> element unless specified explicitly, such
                        # as when redefining an enum in another extension.
                        extnumber = enum.get('extnumber')
                        if not extnumber:
                            enum.set('extnumber', str(featureInfo.number))

                        enum.set('extname', featureInfo.name)
                        enum.set('supported', noneStr(featureInfo.supported))
                        # Look up the GroupInfo with matching groupName
                        if groupName in self.groupdict:
                            gi = self.groupdict[groupName]
                            gi.elem.append(copy.deepcopy(enum))

                        addEnumInfo = True
                    elif enum.get('value') or enum.get('bitpos') or enum.get('alias'):
                        addEnumInfo = True
                    if addEnumInfo:
                        enumInfo = EnumInfo(enum)
                        self.addElementInfo(enum, enumInfo, 'enum', self.enumdict)

    def markTypeRequired(self, typename, required):
        """Require (along with its dependencies) or remove (but not its dependencies) a type.

        - typename - name of type
        - required - boolean (to tag features as required or not)
        """
        # Get TypeInfo object for <type> tag corresponding to typename
        typeinfo = self.lookupElementInfo(typename, self.typedict)
        if typeinfo is not None:
            if required:
                # Tag type dependencies in 'alias' and 'required' attributes as
                # required. This does not un-tag dependencies in a <remove>
                # tag. See comments in markRequired() below for the reason.
                for attrib_name in ['requires', 'alias']:
                    depname = typeinfo.elem.get(attrib_name)
                    if depname:
                        # Do not recurse on self-referential structures.
                        if typename != depname:
                            self.markTypeRequired(depname, required)
                # Tag types used in defining this type (e.g. in nested
                # <type> tags)
                # Look for <type> in entire <command> tree,
                # not just immediate children
                for subtype in typeinfo.elem.findall('.//type'):
                    if typename != subtype.text:
                        self.markTypeRequired(subtype.text, required)
                # Tag enums used in defining this type, for example in
                #   <member><name>member</name>[<enum>MEMBER_SIZE</enum>]</member>
                for subenum in typeinfo.elem.findall('.//enum'):
                    self.markEnumRequired(subenum.text, required)
                # Tag type dependency in 'bitvalues' attributes as
                # required. This ensures that the bit values for a flag
                # are emitted
                depType = typeinfo.elem.get('bitvalues')
                if depType:
                    self.markTypeRequired(depType, required)
                    group = self.lookupElementInfo(depType, self.groupdict)
                    if group is not None:
                        group.flagType = typeinfo

            typeinfo.required = required

    def markEnumRequired(self, enumname, required):
        """Mark an enum as required or not.

        - enumname - name of enum
        - required - boolean (to tag features as required or not)"""

        enum = self.lookupElementInfo(enumname, self.enumdict)
        if enum is not None:
            # If the enum is part of a group, and is being removed, then
            # look it up in that <enums> tag and remove the Element there,
            # so that it is not visible to generators (which traverse the
            # <enums> tag elements rather than using the dictionaries).
            if not required:
                groupName = enum.elem.get('extends')
                if groupName is not None:

                    # Look up the Info with matching groupName
                    if groupName in self.groupdict:
                        gi = self.groupdict[groupName]
                        gienum = gi.elem.find("enum[@name='" + enumname + "']")
                        if gienum is not None:
                            # Remove copy of this enum from the group
                            gi.elem.remove(gienum)
                else:
                    # This enum is not an extending enum.
                    # The XML tree must be searched for all <enums> that
                    # might have it, so we know the parent to delete from.

                    enumName = enum.elem.get('name')
                    count = 0
                    for enums in self.reg.findall('enums'):
                        for thisEnum in enums.findall('enum'):
                            if thisEnum.get('name') == enumName:
                                # Actually remove it
                                count = count + 1
                                enums.remove(thisEnum)

            enum.required = required
            # Tag enum dependencies in 'alias' attribute as required
            depname = enum.elem.get('alias')
            if depname:
                self.markEnumRequired(depname, required)

    def markCmdRequired(self, cmdname, required):
        """Mark a command as required or not.

        - cmdname - name of command
        - required - boolean (to tag features as required or not)"""
        cmd = self.lookupElementInfo(cmdname, self.cmddict)
        if cmd is not None:
            cmd.required = required
            # Tag all parameter types of this command as required.
            # This does not remove types of commands in a <remove>
            # tag, because many other commands may use the same type.
            # We could be more clever and reference count types,
            # instead of using a boolean.
            if required:
                # Look for <type> in entire <command> tree,
                # not just immediate children
                for type_elem in cmd.elem.findall('.//type'):
                    self.markTypeRequired(type_elem.text, required)

    def markRequired(self, featurename, feature, required):
        """Require or remove features specified in the Element.

        - featurename - name of the feature
        - feature - Element for `<require>` or `<remove>` tag
        - required - boolean (to tag features as required or not)"""
        # Loop over types, enums, and commands in the tag
        # @@ It would be possible to respect 'api' and 'profile' attributes
        #  in individual features, but that is not done yet.
        for typeElem in feature.findall('type'):
            self.markTypeRequired(typeElem.get('name'), required)
        for enumElem in feature.findall('enum'):
            self.markEnumRequired(enumElem.get('name'), required)

        for cmdElem in feature.findall('command'):
            self.markCmdRequired(cmdElem.get('name'), required)

    def fillFeatureDictionary(self, interface, featurename, api):
        """Capture added interfaces for a `<version>` or `<extension>`.

        - interface - Element for `<version>` or `<extension>`, containing
          `<require>` and `<remove>` tags
        - featurename - name of the feature
        - api - string specifying API name being generated
        """

        # Explicitly initialize known types - errors for unhandled categories
        self.gen.featureDictionary[featurename] = {
            "enumconstant": {},
            "command": {},
            "enum": {},
            "struct": {},
            "handle": {},
            "basetype": {},
            "include": {},
            "define": {},
            "bitmask": {},
            "union": {},
            "funcpointer": {},
        }

    def requireFeatures(self, interface, featurename, api):
        """Process `<require>` tags for a `<version>` or `<extension>`.

        - interface - Element for `<version>` or `<extension>`, containing
          `<require>` tags
        - featurename - name of the feature
        - api - string specifying API name being generated
        - profile - string specifying API profile being generated"""

        # <require> marks things that are required by this version/profile
        for feature in interface.findall('require'):
            if matchAPIProfile(api, feature):
                self.markRequired(featurename, feature, True)

    def generateFeature(self, fname, ftype, dictionary, explicit=False):
        """Generate a single type / enum group / enum / command,
        and all its dependencies as needed.

        - fname - name of feature (`<type>`/`<enum>`/`<command>`)
        - ftype - type of feature, 'type' | 'enum' | 'command'
        - dictionary - of *Info objects - self.{type|enum|cmd}dict
        - explicit - True if this is explicitly required by the top-level
          XML <require> tag, False if it is a dependency of an explicit
          requirement."""

        f = self.lookupElementInfo(fname, dictionary)
        if f is None:
            return

        if not f.required:
            return

        # If feature is not required, or has already been declared, return
        if f.declared:
            return
        # Always mark feature declared, as though actually emitted
        f.declared = True

        # Determine if this is an alias, and of what, if so
        alias = f.elem.get('alias')
        # Pull in dependent declaration(s) of the feature.
        # For types, there may be one type in the 'requires' attribute of
        #   the element, one in the 'alias' attribute, and many in
        #   embedded <type> and <enum> tags within the element.
        # For commands, there may be many in <type> tags within the element.
        # For enums, no dependencies are allowed (though perhaps if you
        #   have a uint64 enum, it should require that type).
        genProc = None
        followupFeature = None
        if ftype == 'type':
            genProc = self.gen.genType

            # Generate type dependencies in 'alias' and 'requires' attributes
            if alias:
                self.generateFeature(alias, 'type', self.typedict)
            requires = f.elem.get('requires')
            if requires:
                self.generateFeature(requires, 'type', self.typedict)

            # Generate types used in defining this type (e.g. in nested
            # <type> tags)
            # Look for <type> in entire <command> tree,
            # not just immediate children
            for subtype in f.elem.findall('.//type'):
                self.generateFeature(subtype.text, 'type', self.typedict)

            # Generate enums used in defining this type, for example in
            #   <member><name>member</name>[<enum>MEMBER_SIZE</enum>]</member>
            for subtype in f.elem.findall('.//enum'):
                self.generateFeature(subtype.text, 'enum', self.enumdict)

            # If the type is an enum group, look up the corresponding
            # group in the group dictionary and generate that instead.
            if f.elem.get('category') == 'enum':
                group = self.lookupElementInfo(fname, self.groupdict)
                if alias is not None:
                    # Now, pass the *aliased* GroupInfo to the genGroup, but
                    # with an additional parameter which is the alias name.
                    genProc = self.gen.genGroup
                    f = self.lookupElementInfo(alias, self.groupdict)
                elif group is None:
                    return
                else:
                    genProc = self.gen.genGroup
                    f = group

                    # @ The enum group is not ready for generation. At this
                    # @   point, it contains all <enum> tags injected by
                    # @   <extension> tags without any verification of whether
                    # @   they are required or not. It may also contain
                    # @   duplicates injected by multiple consistent
                    # @   definitions of an <enum>.

                    # @ Pass over each enum, marking its enumdict[] entry as
                    # @ required or not. Mark aliases of enums as required,
                    # @ too.

                    enums = group.elem.findall('enum')
                    # Check for required enums, including aliases
                    # LATER - Check for, report, and remove duplicates?
                    enumAliases = []
                    for elem in enums:
                        name = elem.get('name')

                        required = False

                        extname = elem.get('extname')
                        version = elem.get('version')
                        if extname is not None:
                            # 'supported' attribute was injected when the <enum> element was
                            # moved into the <enums> group in Registry.parseTree()
                            supported_list = elem.get('supported').split(",")
                            if 'vulkan' in supported_list:
                                required = True
                            elif re.match(self.genOpts.addExtensions, extname) is not None:
                                required = True
                        elif version is not None:
                            required = re.match(self.genOpts.emitversions, version) is not None
                        else:
                            required = True

                        if required:
                            # Mark this element as required (in the element, not the EnumInfo)
                            elem.set('required', 'true')
                            # If it is an alias, track that for later use
                            enumAlias = elem.get('alias')
                            if enumAlias:
                                enumAliases.append(enumAlias)
                    for elem in enums:
                        name = elem.get('name')
                        if name in enumAliases:
                            elem.set('required', 'true')
            if f is None:
                raise RuntimeError("Should not get here")
            if f.elem.get('category') == 'bitmask':
                followupFeature = f.elem.get('bitvalues')
        elif ftype == 'command':
            # Generate command dependencies in 'alias' attribute
            if alias:
                self.generateFeature(alias, 'command', self.cmddict)

            genProc = self.gen.genCmd
            for type_elem in f.elem.findall('.//type'):
                depname = type_elem.text
                self.generateFeature(depname, 'type', self.typedict)
        elif ftype == 'enum':
            # Generate enum dependencies in 'alias' attribute
            if alias:
                self.generateFeature(alias, 'enum', self.enumdict)
            genProc = self.gen.genEnum

        # Actually generate the type only if emitting declarations
        if self.emitFeatures:
            if genProc is None:
                raise RuntimeError("genProc is None when we should be emitting")
            genProc(f, fname, alias)

        if followupFeature:
            self.generateFeature(followupFeature, "type", self.typedict)

    def generateRequiredInterface(self, interface):
        """Generate all interfaces required by an API version or extension.

        - interface - Element for `<version>` or `<extension>`"""

        # Loop over all features inside all <require> tags.
        for features in interface.findall('require'):
            for t in features.findall('type'):
                self.generateFeature(t.get('name'), 'type', self.typedict, explicit=True)
            for e in features.findall('enum'):
                # If this is an enum extending an enumerated type, do not
                # generate it - this has already been done in reg.parseTree,
                # by copying this element into the enumerated type.
                enumextends = e.get('extends')
                if not enumextends:
                    self.generateFeature(e.get('name'), 'enum', self.enumdict, explicit=True)
            for c in features.findall('command'):
                self.generateFeature(c.get('name'), 'command', self.cmddict, explicit=True)

    def apiGen(self):
        """Generate interface for specified versions using the current
        generator and generator options"""

        # Could reset required/declared flags for all features here.
        # This has been removed as never used. The initial motivation was
        # the idea of calling apiGen() repeatedly for different targets, but
        # this has never been done. The 20% or so build-time speedup that
        # might result is not worth the effort to make it actually work.
        #
        # self.apiReset()

        # Compile regexps used to select versions & extensions
        regVersions = re.compile(self.genOpts.versions)
        regEmitVersions = re.compile(self.genOpts.emitversions)
        regAddExtensions = re.compile(self.genOpts.addExtensions)
        regEmitExtensions = re.compile(self.genOpts.emitExtensions)

        # Get all matching API feature names & add to list of FeatureInfo
        # Note we used to select on feature version attributes, not names.
        features = []
        apiMatch = False
        for key in self.apidict:
            fi = self.apidict[key]
            api = fi.elem.get('api')
            if apiNameMatch('vulkan', api):
                apiMatch = True
                if regVersions.match(fi.name):
                    # Matches API & version #s being generated. Mark for
                    # emission and add to the features[] list .
                    # @@ Could use 'declared' instead of 'emit'?
                    fi.emit = (regEmitVersions.match(fi.name) is not None)
                    features.append(fi)

        # Get all matching extensions, in order by their extension number,
        # and add to the list of features.
        # Start with extensions whose 'supported' attributes match the API
        # being generated. Add extensions matching the pattern specified in
        # regExtensions, then remove extensions matching the pattern
        # specified in regRemoveExtensions
        for (extName, ei) in sorted(self.extdict.items(), key=lambda x: x[1].number if x[1].number is not None else '0'):
            extName = ei.name
            include = False

            # Include extension if defaultExtensions is not None and is
            # exactly matched by the 'supported' attribute.
            if apiNameMatch('vulkan', ei.elem.get('supported')):
                include = True

            # Include additional extensions if the extension name matches
            # the regexp specified in the generator options. This allows
            # forcing extensions into an interface even if they are not
            # tagged appropriately in the registry.
            # However, we still respect the 'supported' attribute.
            if regAddExtensions.match(extName) is not None:
                if not apiNameMatch('vulkan', ei.elem.get('supported')):
                    include = False
                else:
                    include = True
            # If the extension is to be included, add it to the
            # extension features list.
            if include:
                ei.emit = (regEmitExtensions.match(extName) is not None)
                features.append(ei)

        # Sort the features list, if a sort procedure is defined
        if self.genOpts.sortProcedure:
            self.genOpts.sortProcedure(features)

        # Passes 1+2: loop over requested API versions and extensions tagging
        #   types/commands/features as required (in an <require> block) or no
        #   longer required (in an <remove> block). <remove>s are processed
        #   after all <require>s, so removals win.
        # If a profile other than 'None' is being generated, it must
        #   match the profile attribute (if any) of the <require> and
        #   <remove> tags.
        for f in features:
            self.fillFeatureDictionary(f.elem, f.name, 'vulkan')
            self.requireFeatures(f.elem, f.name, 'vulkan')

        # @@May need to strip <spirvcapability> / <spirvextension> <enable>
        # tags of these forms:
        #   <enable version="VK_API_VERSION_1_0"/>
        #   <enable struct="VkPhysicalDeviceFeatures" feature="geometryShader" requires="VK_VERSION_1_0"/>
        #   <enable extension="VK_KHR_shader_draw_parameters"/>
        #   <enable property="VkPhysicalDeviceVulkan12Properties" member="shaderDenormPreserveFloat16" value="VK_TRUE" requires="VK_VERSION_1_2,VK_KHR_shader_float_controls"/>

        # Pass 3: loop over specified API versions and extensions printing
        #   declarations for required things which have not already been
        #   generated.
        self.gen.beginFile(self.genOpts)
        for f in features:
            emit = self.emitFeatures = f.emit
            # Generate the interface (or just tag its elements as having been
            # emitted, if they have not been).
            self.gen.beginFeature(f.elem, emit)
            self.generateRequiredInterface(f.elem)
            self.gen.endFeature()
        self.gen.endFile()
