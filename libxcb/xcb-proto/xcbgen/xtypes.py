'''
This module contains the classes which represent XCB data types.
'''
from xcbgen.expr import Field, Expression
import __main__

class Type(object):
    '''
    Abstract base class for all XCB data types.
    Contains default fields, and some abstract methods.
    '''
    def __init__(self, name):
        '''
        Default structure initializer.  Sets up default fields.

        Public fields:
        name is a tuple of strings specifying the full type name.
        size is the size of the datatype in bytes, or None if variable-sized.
        nmemb is 1 for non-list types, None for variable-sized lists, otherwise number of elts.
        booleans for identifying subclasses, because I can't figure out isinstance().
        '''
        self.name = name
        self.size = None
        self.nmemb = None
        self.resolved = False

        # Screw isinstance().
        self.is_simple = False
        self.is_list = False
        self.is_expr = False
        self.is_container = False
        self.is_reply = False
        self.is_union = False
        self.is_pad = False
        self.is_switch = False
        self.is_bitcase = False

    def resolve(self, module):
        '''
        Abstract method for resolving a type.
        This should make sure any referenced types are already declared.
        '''
        raise Exception('abstract resolve method not overridden!')

    def out(self, name):
        '''
        Abstract method for outputting code.
        These are declared in the language-specific modules, and
        there must be a dictionary containing them declared when this module is imported!
        '''
        raise Exception('abstract out method not overridden!')

    def fixed_size(self):
        '''
        Abstract method for determining if the data type is fixed-size.
        '''
        raise Exception('abstract fixed_size method not overridden!')

    def make_member_of(self, module, complex_type, field_type, field_name, visible, wire, auto, enum=None):
        '''
        Default method for making a data type a member of a structure.
        Extend this if the data type needs to add an additional length field or something.

        module is the global module object.
        complex_type is the structure object.
        see Field for the meaning of the other parameters.
        '''
        new_field = Field(self, field_type, field_name, visible, wire, auto, enum)

        # We dump the _placeholder_byte if any fields are added.
        for (idx, field) in enumerate(complex_type.fields):
            if field == _placeholder_byte:
                complex_type.fields[idx] = new_field
                return

        complex_type.fields.append(new_field)

    def make_fd_of(self, module, complex_type, fd_name):
        '''
        Method for making a fd member of a structure.
        '''
        new_fd = Field(self, module.get_type_name('INT32'), fd_name, True, False, False, None, True)
        # We dump the _placeholder_byte if any fields are added.
        for (idx, field) in enumerate(complex_type.fields):
            if field == _placeholder_byte:
                complex_type.fields[idx] = new_fd
                return

        complex_type.fields.append(new_fd)

class SimpleType(Type):
    '''
    Derived class which represents a cardinal type like CARD32 or char.
    Any type which is typedef'ed to cardinal will be one of these.

    Public fields added:
    none
    '''
    def __init__(self, name, size):
        Type.__init__(self, name)
        self.is_simple = True
        self.size = size
        self.nmemb = 1

    def resolve(self, module):
        self.resolved = True

    def fixed_size(self):
        return True

    out = __main__.output['simple']


# Cardinal datatype globals.  See module __init__ method.
tcard8 = SimpleType(('uint8_t',), 1)
tcard16 = SimpleType(('uint16_t',), 2)
tcard32 = SimpleType(('uint32_t',), 4)
tcard64 = SimpleType(('uint64_t',), 8)
tint8 =  SimpleType(('int8_t',), 1)
tint16 = SimpleType(('int16_t',), 2)
tint32 = SimpleType(('int32_t',), 4)
tint64 = SimpleType(('int64_t',), 8)
tchar =  SimpleType(('char',), 1)
tfloat = SimpleType(('float',), 4)
tdouble = SimpleType(('double',), 8)


class Enum(SimpleType):
    '''
    Derived class which represents an enum.  Fixed-size.

    Public fields added:
    values contains a list of (name, value) tuples.  value is empty, or a number.
    bits contains a list of (name, bitnum) tuples.  items only appear if specified as a bit. bitnum is a number.
    '''
    def __init__(self, name, elt):
        SimpleType.__init__(self, name, 4)
        self.values = []
        self.bits = []
        self.doc = None
        for item in list(elt):
            if item.tag == 'doc':
                self.doc = Doc(name, item)

            # First check if we're using a default value
            if len(list(item)) == 0:
                self.values.append((item.get('name'), ''))
                continue

            # An explicit value or bit was specified.
            value = list(item)[0]
            if value.tag == 'value':
                self.values.append((item.get('name'), value.text))
            elif value.tag == 'bit':
                self.values.append((item.get('name'), '%u' % (1 << int(value.text, 0))))
                self.bits.append((item.get('name'), value.text))

    def resolve(self, module):
        self.resolved = True

    def fixed_size(self):
        return True

    out = __main__.output['enum']


class ListType(Type):
    '''
    Derived class which represents a list of some other datatype.  Fixed- or variable-sized.

    Public fields added:
    member is the datatype of the list elements.
    parent is the structure type containing the list.
    expr is an Expression object containing the length information, for variable-sized lists.
    '''
    def __init__(self, elt, member, *parent):
        Type.__init__(self, member.name)
        self.is_list = True
        self.member = member
        self.parents = list(parent)

        if elt.tag == 'list':
            elts = list(elt)
            self.expr = Expression(elts[0] if len(elts) else elt, self)
        elif elt.tag == 'valueparam':
            self.expr = Expression(elt, self)

        self.size = member.size if member.fixed_size() else None
        self.nmemb = self.expr.nmemb if self.expr.fixed_size() else None

    def make_member_of(self, module, complex_type, field_type, field_name, visible, wire, auto, enum=None):
        if not self.fixed_size():
            # We need a length field.
            # Ask our Expression object for it's name, type, and whether it's on the wire.
            lenfid = self.expr.lenfield_type
            lenfield_name = self.expr.lenfield_name
            lenwire = self.expr.lenwire
            needlen = True

            # See if the length field is already in the structure.
            for parent in self.parents:
                for field in parent.fields:
                    if field.field_name == lenfield_name:
                        needlen = False

            # It isn't, so we need to add it to the structure ourself.
            if needlen:
                type = module.get_type(lenfid)
                lenfield_type = module.get_type_name(lenfid)
                type.make_member_of(module, complex_type, lenfield_type, lenfield_name, True, lenwire, False, enum)

        # Add ourself to the structure by calling our original method.
        Type.make_member_of(self, module, complex_type, field_type, field_name, visible, wire, auto, enum)

    def resolve(self, module):
        if self.resolved:
            return
        self.member.resolve(module)
        self.expr.resolve(module, self.parents)

        # Find my length field again.  We need the actual Field object in the expr.
        # This is needed because we might have added it ourself above.
        if not self.fixed_size():
            for parent in self.parents:
                for field in parent.fields:
                    if field.field_name == self.expr.lenfield_name and field.wire:
                        self.expr.lenfield = field
                        break

        self.resolved = True

    def fixed_size(self):
        return self.member.fixed_size() and self.expr.fixed_size()

class ExprType(Type):
    '''
    Derived class which represents an exprfield.  Fixed size.

    Public fields added:
    expr is an Expression object containing the value of the field.
    '''
    def __init__(self, elt, member, *parents):
        Type.__init__(self, member.name)
        self.is_expr = True
        self.member = member
        self.parents = parents

        self.expr = Expression(list(elt)[0], self)

        self.size = member.size
        self.nmemb = 1

    def resolve(self, module):
        if self.resolved:
            return
        self.member.resolve(module)
        self.resolved = True

    def fixed_size(self):
        return True

class PadType(Type):
    '''
    Derived class which represents a padding field.
    '''
    def __init__(self, elt):
        Type.__init__(self, tcard8.name)
        self.is_pad = True
        self.size = 1
        self.nmemb = 1
        self.align = 1
        if elt != None:
            self.nmemb = int(elt.get('bytes', "1"), 0)
            self.align = int(elt.get('align', "1"), 0)

    def resolve(self, module):
        self.resolved = True

    def fixed_size(self):
        return self.align <= 1

    
class ComplexType(Type):
    '''
    Derived class which represents a structure.  Base type for all structure types.

    Public fields added:
    fields is an array of Field objects describing the structure fields.
    '''
    def __init__(self, name, elt):
        Type.__init__(self, name)
        self.is_container = True
        self.elt = elt
        self.fields = []
        self.nmemb = 1
        self.size = 0
        self.lenfield_parent = [self]
        self.fds = []

    def resolve(self, module):
        if self.resolved:
            return
        enum = None

        # Resolve all of our field datatypes.
        for child in list(self.elt):
            if child.tag == 'pad':
                field_name = 'pad' + str(module.pads)
                fkey = 'CARD8'
                type = PadType(child)
                module.pads = module.pads + 1
                visible = False
            elif child.tag == 'field':
                field_name = child.get('name')
                enum = child.get('enum')
                fkey = child.get('type')
                type = module.get_type(fkey)
                visible = True
            elif child.tag == 'exprfield':
                field_name = child.get('name')
                fkey = child.get('type')
                type = ExprType(child, module.get_type(fkey), *self.lenfield_parent)
                visible = False
            elif child.tag == 'list':
                field_name = child.get('name')
                fkey = child.get('type')
                type = ListType(child, module.get_type(fkey), *self.lenfield_parent)
                visible = True
            elif child.tag == 'valueparam':
                field_name = child.get('value-list-name')
                fkey = 'CARD32'
                type = ListType(child, module.get_type(fkey), *self.lenfield_parent)
                visible = True
            elif child.tag == 'switch':
                field_name = child.get('name')
                # construct the switch type name from the parent type and the field name
                field_type = self.name + (field_name,)
                type = SwitchType(field_type, child, *self.lenfield_parent)
                visible = True
                type.make_member_of(module, self, field_type, field_name, visible, True, False)
                type.resolve(module)
                continue
            elif child.tag == 'fd':
                fd_name = child.get('name')
                type = module.get_type('INT32')
                type.make_fd_of(module, self, fd_name)
                continue
            else:
                # Hit this on Reply
                continue

            # Get the full type name for the field
            field_type = module.get_type_name(fkey)
            # Add the field to ourself
            type.make_member_of(module, self, field_type, field_name, visible, True, False, enum)
            # Recursively resolve the type (could be another structure, list)
            type.resolve(module)

        self.calc_size() # Figure out how big we are
        self.resolved = True

    def calc_size(self):
        self.size = 0
        for m in self.fields:
            if not m.wire:
                continue
            if m.type.fixed_size():
                self.size = self.size + (m.type.size * m.type.nmemb)
            else:
                self.size = None
                break

    def fixed_size(self):
        for m in self.fields:
            if not m.type.fixed_size():
                return False
        return True

class SwitchType(ComplexType):
    '''
    Derived class which represents a List of Items.  

    Public fields added:
    bitcases is an array of Bitcase objects describing the list items
    '''

    def __init__(self, name, elt, *parents):
        ComplexType.__init__(self, name, elt)
        self.parents = parents
        # FIXME: switch cannot store lenfields, so it should just delegate the parents
        self.lenfield_parent = list(parents) + [self]
        # self.fields contains all possible fields collected from the Bitcase objects, 
        # whereas self.items contains the Bitcase objects themselves
        self.bitcases = []

        self.is_switch = True
        elts = list(elt)
        self.expr = Expression(elts[0] if len(elts) else elt, self)

    def resolve(self, module):
        if self.resolved:
            return

        parents = list(self.parents) + [self]

        # Resolve all of our field datatypes.
        for index, child in enumerate(list(self.elt)):
            if child.tag == 'bitcase':
                field_name = child.get('name')
                if field_name is None:
                    field_type = self.name + ('bitcase%d' % index,)
                else:
                    field_type = self.name + (field_name,)

                # use self.parent to indicate anchestor, 
                # as switch does not contain named fields itself
                type = BitcaseType(index, field_type, child, *parents)
                # construct the switch type name from the parent type and the field name
                if field_name is None:
                    type.has_name = False
                    # Get the full type name for the field
                    field_type = type.name               
                visible = True

                # add the field to ourself
                type.make_member_of(module, self, field_type, field_name, visible, True, False)

                # recursively resolve the type (could be another structure, list)
                type.resolve(module)
                inserted = False
                for new_field in type.fields:
                    # We dump the _placeholder_byte if any fields are added.
                    for (idx, field) in enumerate(self.fields):
                        if field == _placeholder_byte:
                            self.fields[idx] = new_field
                            inserted = True
                            break
                    if False == inserted:
                        self.fields.append(new_field)

        self.calc_size() # Figure out how big we are
        self.resolved = True

    def make_member_of(self, module, complex_type, field_type, field_name, visible, wire, auto, enum=None):
        if not self.fixed_size():
            # We need a length field.
            # Ask our Expression object for it's name, type, and whether it's on the wire.
            lenfid = self.expr.lenfield_type
            lenfield_name = self.expr.lenfield_name
            lenwire = self.expr.lenwire
            needlen = True

            # See if the length field is already in the structure.
            for parent in self.parents:
                for field in parent.fields:
                    if field.field_name == lenfield_name:
                        needlen = False

            # It isn't, so we need to add it to the structure ourself.
            if needlen:
                type = module.get_type(lenfid)
                lenfield_type = module.get_type_name(lenfid)
                type.make_member_of(module, complex_type, lenfield_type, lenfield_name, True, lenwire, False, enum)

        # Add ourself to the structure by calling our original method.
        Type.make_member_of(self, module, complex_type, field_type, field_name, visible, wire, auto, enum)

    # size for switch can only be calculated at runtime
    def calc_size(self):
        pass

    # note: switch is _always_ of variable size, but we indicate here wether 
    # it contains elements that are variable-sized themselves
    def fixed_size(self):
        return False
#        for m in self.fields:
#            if not m.type.fixed_size():
#                return False
#        return True


class Struct(ComplexType):
    '''
    Derived class representing a struct data type.
    '''
    out = __main__.output['struct']


class Union(ComplexType):
    '''
    Derived class representing a union data type.
    '''
    def __init__(self, name, elt):
        ComplexType.__init__(self, name, elt)
        self.is_union = True

    out = __main__.output['union']


class BitcaseType(ComplexType):
    '''
    Derived class representing a struct data type.
    '''
    def __init__(self, index, name, elt, *parent):
        elts = list(elt)
        self.expr = []
        fields = []
        for elt in elts:
            if elt.tag == 'enumref':
                self.expr.append(Expression(elt, self))
            else:
                fields.append(elt)
        ComplexType.__init__(self, name, fields)
        self.has_name = True
        self.index = 1
        self.lenfield_parent = list(parent) + [self]
        self.parents = list(parent)
        self.is_bitcase = True

    def make_member_of(self, module, switch_type, field_type, field_name, visible, wire, auto, enum=None):
        '''
        register BitcaseType with the corresponding SwitchType

        module is the global module object.
        complex_type is the structure object.
        see Field for the meaning of the other parameters.
        '''
        new_field = Field(self, field_type, field_name, visible, wire, auto, enum)

        # We dump the _placeholder_byte if any bitcases are added.
        for (idx, field) in enumerate(switch_type.bitcases):
            if field == _placeholder_byte:
                switch_type.bitcases[idx] = new_field
                return

        switch_type.bitcases.append(new_field)

    def resolve(self, module):
        if self.resolved:
            return

        for e in self.expr:
            e.resolve(module, self.parents+[self])

        # Resolve the bitcase expression
        ComplexType.resolve(self, module)


class Reply(ComplexType):
    '''
    Derived class representing a reply.  Only found as a field of Request.
    '''
    def __init__(self, name, elt):
        ComplexType.__init__(self, name, elt)
        self.is_reply = True
        self.doc = None

        for child in list(elt):
            if child.tag == 'doc':
                self.doc = Doc(name, child)

    def resolve(self, module):
        if self.resolved:
            return
        # Reset pads count
        module.pads = 0
        # Add the automatic protocol fields
        self.fields.append(Field(tcard8, tcard8.name, 'response_type', False, True, True))
        self.fields.append(_placeholder_byte)
        self.fields.append(Field(tcard16, tcard16.name, 'sequence', False, True, True))
        self.fields.append(Field(tcard32, tcard32.name, 'length', False, True, True))
        ComplexType.resolve(self, module)
        

class Request(ComplexType):
    '''
    Derived class representing a request.

    Public fields added:
    reply contains the reply datatype or None for void requests.
    opcode contains the request number.
    '''
    def __init__(self, name, elt):
        ComplexType.__init__(self, name, elt)
        self.reply = None
        self.doc = None
        self.opcode = elt.get('opcode')

        for child in list(elt):
            if child.tag == 'reply':
                self.reply = Reply(name, child)
            if child.tag == 'doc':
                self.doc = Doc(name, child)

    def resolve(self, module):
        if self.resolved:
            return
        # Add the automatic protocol fields
        if module.namespace.is_ext:
            self.fields.append(Field(tcard8, tcard8.name, 'major_opcode', False, True, True))
            self.fields.append(Field(tcard8, tcard8.name, 'minor_opcode', False, True, True))
            self.fields.append(Field(tcard16, tcard16.name, 'length', False, True, True))
            ComplexType.resolve(self, module)
        else:
            self.fields.append(Field(tcard8, tcard8.name, 'major_opcode', False, True, True))
            self.fields.append(_placeholder_byte)
            self.fields.append(Field(tcard16, tcard16.name, 'length', False, True, True))
            ComplexType.resolve(self, module)

        if self.reply:
            self.reply.resolve(module)

    out = __main__.output['request']


class Event(ComplexType):
    '''
    Derived class representing an event data type.

    Public fields added:
    opcodes is a dictionary of name -> opcode number, for eventcopies.
    '''
    def __init__(self, name, elt):
        ComplexType.__init__(self, name, elt)
        self.opcodes = {}

        self.has_seq = not bool(elt.get('no-sequence-number'))

        self.is_ge_event = bool(elt.get('xge'))

        self.doc = None
        for item in list(elt):
            if item.tag == 'doc':
                self.doc = Doc(name, item)

    def add_opcode(self, opcode, name, main):
        self.opcodes[name] = opcode
        if main:
            self.name = name

    def resolve(self, module):
        def add_event_header():
            self.fields.append(Field(tcard8, tcard8.name, 'response_type', False, True, True))
            if self.has_seq:
                self.fields.append(_placeholder_byte)
                self.fields.append(Field(tcard16, tcard16.name, 'sequence', False, True, True))

        def add_ge_event_header():
            self.fields.append(Field(tcard8,  tcard8.name,  'response_type', False, True, True))
            self.fields.append(Field(tcard8,  tcard8.name,  'extension', False, True, True))
            self.fields.append(Field(tcard16, tcard16.name, 'sequence', False, True, True))
            self.fields.append(Field(tcard32, tcard32.name, 'length', False, True, True))
            self.fields.append(Field(tcard16, tcard16.name, 'event_type', False, True, True))

        if self.resolved:
            return

        # Add the automatic protocol fields
        if self.is_ge_event:
            add_ge_event_header()
        else:
            add_event_header()

        ComplexType.resolve(self, module)

    out = __main__.output['event']


class Error(ComplexType):
    '''
    Derived class representing an error data type.

    Public fields added:
    opcodes is a dictionary of name -> opcode number, for errorcopies.
    '''
    def __init__(self, name, elt):
        ComplexType.__init__(self, name, elt)
        self.opcodes = {}

    def add_opcode(self, opcode, name, main):
        self.opcodes[name] = opcode
        if main:
            self.name = name

    def resolve(self, module):
        if self.resolved:
            return

        # Add the automatic protocol fields
        self.fields.append(Field(tcard8, tcard8.name, 'response_type', False, True, True))
        self.fields.append(Field(tcard8, tcard8.name, 'error_code', False, True, True))
        self.fields.append(Field(tcard16, tcard16.name, 'sequence', False, True, True))
        ComplexType.resolve(self, module)

    out = __main__.output['error']


class Doc(object):
    '''
    Class representing a <doc> tag.
    '''
    def __init__(self, name, elt):
        self.name = name
        self.description = None
        self.brief = 'BRIEF DESCRIPTION MISSING'
        self.fields = {}
        self.errors = {}
        self.see = {}
        self.example = None

        for child in list(elt):
            text = child.text if child.text else ''
            if child.tag == 'description':
                self.description = text.strip()
            if child.tag == 'brief':
                self.brief = text.strip()
            if child.tag == 'field':
                self.fields[child.get('name')] = text.strip()
            if child.tag == 'error':
                self.errors[child.get('type')] = text.strip()
            if child.tag == 'see':
                self.see[child.get('name')] = child.get('type')
            if child.tag == 'example':
                self.example = text.strip()



_placeholder_byte = Field(PadType(None), tcard8.name, 'pad0', False, True, False)
