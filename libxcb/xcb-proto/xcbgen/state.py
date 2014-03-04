'''
This module contains the namespace class and the singleton module class.
'''
from os.path import dirname, basename
from xml.etree.cElementTree import parse

from xcbgen import matcher
from xcbgen.error import *
from xcbgen.xtypes import *

import __main__

class Namespace(object):
    '''
    Contains the naming information for an extension.

    Public fields:

    header is the header attribute ("header file" name).
    is_ext is true for extensions, false for xproto.
    major_version and minor_version are extension version info.
    ext_xname is the X extension name string.
    ext_name is the XCB extension name prefix.
    '''
    def __init__(self, filename):
        # Path info
        self.path = filename
        self.dir = dirname(filename)
        self.file = basename(filename)

        # Parse XML
        self.root = parse(filename).getroot()
        self.header = self.root.get('header')
        self.ns = self.header + ':'
        
        # Get root element attributes
        if self.root.get('extension-xname', False): 
            self.is_ext = True
            self.major_version = self.root.get('major-version')
            self.minor_version = self.root.get('minor-version')
            self.ext_xname = self.root.get('extension-xname')
            self.ext_name = self.root.get('extension-name')
            self.prefix = ('xcb', self.ext_name)
        else:
            self.is_ext = False
            self.ext_name = ''
            self.prefix = ('xcb',)


class Module(object):
    '''
    This is the grand, encompassing class that represents an entire XCB specification.
    Only gets instantiated once, in the main() routine.

    Don't need to worry about this much except to declare it and to get the namespace.

    Public fields:
    namespace contains the namespace info for the spec.
    '''
    open = __main__.output['open']
    close = __main__.output['close']

    def __init__(self, filename, output):
        self.namespace = Namespace(filename)
        self.output = output

        self.imports = []
        self.direct_imports = []
        self.import_level = 0
        self.types = {}
        self.events = {}
        self.errors = {}
        self.all = []

        # Register some common types
        self.add_type('CARD8', '', ('uint8_t',), tcard8)
        self.add_type('CARD16', '', ('uint16_t',), tcard16)
        self.add_type('CARD32', '', ('uint32_t',), tcard32)
        self.add_type('CARD64', '', ('uint64_t',), tcard64)
        self.add_type('INT8', '', ('int8_t',), tint8)
        self.add_type('INT16', '', ('int16_t',), tint16)
        self.add_type('INT32', '', ('int32_t',), tint32)
        self.add_type('INT64', '', ('int64_t',), tint64)
        self.add_type('BYTE', '', ('uint8_t',), tcard8)
        self.add_type('BOOL', '', ('uint8_t',), tcard8)
        self.add_type('char', '', ('char',), tchar)
        self.add_type('float', '', ('float',), tfloat)
        self.add_type('double', '', ('double',), tdouble)
        self.add_type('void', '', ('void',), tcard8)

    # This goes out and parses the rest of the XML
    def register(self):
        matcher.execute(self, self.namespace)

    # Recursively resolve all types
    def resolve(self):
        for (name, item) in self.all:
            self.pads = 0
            item.resolve(self)

    # Call all the output methods
    def generate(self):
        self.open()

        for (name, item) in self.all:
            item.out(name)

        self.close()

    # Keeps track of what's been imported so far.
    def add_import(self, name, namespace):
        if self.import_level == 0:
            self.direct_imports.append((name, namespace.header))
        self.imports.append((name, namespace.header))

    def has_import(self, name):
        for (name_, header) in self.imports:
            if name_ == name:
                return True
        return False

    # Keeps track of non-request/event/error datatypes
    def add_type(self, id, ns, name, item):
        key = ns + id
        if key in self.types:
            return
        self.types[key] = (name, item)
        if name[:-1] == self.namespace.prefix:
            self.all.append((name, item))

    def get_type_impl(self, id, idx):
        key = id
        if key in self.types:
            return self.types[key][idx]

        key = self.namespace.ns + id
        if key in self.types:
            return self.types[key][idx]

        for key in self.types.keys():
            if key.rpartition(':')[2] == id:
                return self.types[key][idx]

        raise ResolveException('Type %s not found' % id)

    def get_type(self, id):
        return self.get_type_impl(id, 1)

    def get_type_name(self, id):
        return self.get_type_impl(id, 0)

    # Keeps track of request datatypes
    def add_request(self, id, name, item):
        if name[:-1] == self.namespace.prefix:
            self.all.append((name, item))

    # Keeps track of event datatypes
    def add_event(self, id, name, item):
        self.events[id] = (name, item)
        if name[:-1] == self.namespace.prefix:
            self.all.append((name, item))

    def get_event(self, id):
        return self.events[id][1]

    # Keeps track of error datatypes
    def add_error(self, id, name, item):
        self.errors[id] = (name, item)
        if name[:-1] == self.namespace.prefix:
            self.all.append((name, item))

    def get_error(self, id):
        return self.errors[id][1]
