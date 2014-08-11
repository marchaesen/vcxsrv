#!/usr/bin/env python
from xml.etree.cElementTree import *
from os.path import basename
from functools import reduce
import getopt
import os
import sys
import errno
import time
import re

# Jump to the bottom of this file for the main routine

# Some hacks to make the API more readable, and to keep backwards compability
_cname_re = re.compile('([A-Z0-9][a-z]+|[A-Z0-9]+(?![a-z])|[a-z]+)')
_cname_special_cases = {'DECnet':'decnet'}

_extension_special_cases = ['XPrint', 'XCMisc', 'BigRequests']

_cplusplus_annoyances = {'class' : '_class',
                         'new'   : '_new',
                         'delete': '_delete'}
_c_keywords = {'default' : '_default'}

_hlines = []
_hlevel = 0
_clines = []
_clevel = 0
_ns = None

# global variable to keep track of serializers and
# switch data types due to weird dependencies
finished_serializers = []
finished_sizeof = []
finished_switch = []

# keeps enum objects so that we can refer to them when generating manpages.
enums = {}

manpaths = False

def _h(fmt, *args):
    '''
    Writes the given line to the header file.
    '''
    _hlines[_hlevel].append(fmt % args)

def _c(fmt, *args):
    '''
    Writes the given line to the source file.
    '''
    _clines[_clevel].append(fmt % args)

def _hc(fmt, *args):
    '''
    Writes the given line to both the header and source files.
    '''
    _h(fmt, *args)
    _c(fmt, *args)

# XXX See if this level thing is really necessary.
def _h_setlevel(idx):
    '''
    Changes the array that header lines are written to.
    Supports writing different sections of the header file.
    '''
    global _hlevel
    while len(_hlines) <= idx:
        _hlines.append([])
    _hlevel = idx

def _c_setlevel(idx):
    '''
    Changes the array that source lines are written to.
    Supports writing to different sections of the source file.
    '''
    global _clevel
    while len(_clines) <= idx:
        _clines.append([])
    _clevel = idx

def _n_item(str):
    '''
    Does C-name conversion on a single string fragment.
    Uses a regexp with some hard-coded special cases.
    '''
    if str in _cname_special_cases:
        return _cname_special_cases[str]
    else:
        split = _cname_re.finditer(str)
        name_parts = [match.group(0) for match in split]
        return '_'.join(name_parts)

def _cpp(str):
    '''
    Checks for certain C++ reserved words and fixes them.
    '''
    if str in _cplusplus_annoyances:
        return _cplusplus_annoyances[str]
    elif str in _c_keywords:
        return  _c_keywords[str]
    else:
        return str

def _ext(str):
    '''
    Does C-name conversion on an extension name.
    Has some additional special cases on top of _n_item.
    '''
    if str in _extension_special_cases:
        return _n_item(str).lower()
    else:
        return str.lower()

def _n(list):
    '''
    Does C-name conversion on a tuple of strings.
    Different behavior depending on length of tuple, extension/not extension, etc.
    Basically C-name converts the individual pieces, then joins with underscores.
    '''
    if len(list) == 1:
        parts = list
    elif len(list) == 2:
        parts = [list[0], _n_item(list[1])]
    elif _ns.is_ext:
        parts = [list[0], _ext(list[1])] + [_n_item(i) for i in list[2:]]
    else:
        parts = [list[0]] + [_n_item(i) for i in list[1:]]
    return '_'.join(parts).lower()

def _t(list):
    '''
    Does C-name conversion on a tuple of strings representing a type.
    Same as _n but adds a "_t" on the end.
    '''
    if len(list) == 1:
        parts = list
    elif len(list) == 2:
        parts = [list[0], _n_item(list[1]), 't']
    elif _ns.is_ext:
        parts = [list[0], _ext(list[1])] + [_n_item(i) for i in list[2:]] + ['t']
    else:
        parts = [list[0]] + [_n_item(i) for i in list[1:]] + ['t']
    return '_'.join(parts).lower()


def c_open(self):
    '''
    Exported function that handles module open.
    Opens the files and writes out the auto-generated comment, header file includes, etc.
    '''
    global _ns
    _ns = self.namespace
    _ns.c_ext_global_name = _n(_ns.prefix + ('id',))

    # Build the type-name collision avoidance table used by c_enum
    build_collision_table()

    _h_setlevel(0)
    _c_setlevel(0)

    _hc('/*')
    _hc(' * This file generated automatically from %s by c_client.py.', _ns.file)
    _hc(' * Edit at your peril.')
    _hc(' */')
    _hc('')

    _h('/**')
    _h(' * @defgroup XCB_%s_API XCB %s API', _ns.ext_name, _ns.ext_name)
    _h(' * @brief %s XCB Protocol Implementation.', _ns.ext_name)
    _h(' * @{')
    _h(' **/')
    _h('')
    _h('#ifndef __%s_H', _ns.header.upper())
    _h('#define __%s_H', _ns.header.upper())
    _h('')
    _h('#include "xcb.h"')

    _c('#ifdef HAVE_CONFIG_H')
    _c('#include "config.h"')
    _c('#endif')
    _c('#include <stdlib.h>')
    _c('#include <string.h>')
    _c('#include <assert.h>')
    _c('#include <stddef.h>  /* for offsetof() */')
    _c('#include "xcbext.h"')
    _c('#include "%s.h"', _ns.header)

    _c('')
    _c('#define ALIGNOF(type) offsetof(struct { char dummy; type member; }, member)')

    if _ns.is_ext:
        for (n, h) in self.direct_imports:
            _hc('#include "%s.h"', h)

    _h('')
    _h('#ifdef __cplusplus')
    _h('extern "C" {')
    _h('#endif')

    if _ns.is_ext:
        _h('')
        _h('#define XCB_%s_MAJOR_VERSION %s', _ns.ext_name.upper(), _ns.major_version)
        _h('#define XCB_%s_MINOR_VERSION %s', _ns.ext_name.upper(), _ns.minor_version)
        _h('') #XXX
        _h('extern xcb_extension_t %s;', _ns.c_ext_global_name)

        _c('')
        _c('xcb_extension_t %s = { "%s", 0 };', _ns.c_ext_global_name, _ns.ext_xname)

def c_close(self):
    '''
    Exported function that handles module close.
    Writes out all the stored content lines, then closes the files.
    '''
    _h_setlevel(2)
    _c_setlevel(2)
    _hc('')

    _h('')
    _h('#ifdef __cplusplus')
    _h('}')
    _h('#endif')

    _h('')
    _h('#endif')
    _h('')
    _h('/**')
    _h(' * @}')
    _h(' */')

    # Write header file
    hfile = open('%s.h' % _ns.header, 'w')
    for list in _hlines:
        for line in list:
            hfile.write(line)
            hfile.write('\n')
    hfile.close()

    # Write source file
    cfile = open('%s.c' % _ns.header, 'w')
    for list in _clines:
        for line in list:
            cfile.write(line)
            cfile.write('\n')
    cfile.close()

def build_collision_table():
    global namecount
    namecount = {}

    for v in module.types.values():
        name = _t(v[0])
        namecount[name] = (namecount.get(name) or 0) + 1

def c_enum(self, name):
    '''
    Exported function that handles enum declarations.
    '''

    enums[name] = self

    tname = _t(name)
    if namecount[tname] > 1:
        tname = _t(name + ('enum',))

    _h_setlevel(0)
    _h('')
    _h('typedef enum %s {', tname)

    count = len(self.values)

    for (enam, eval) in self.values:
        count = count - 1
        equals = ' = ' if eval != '' else ''
        comma = ',' if count > 0 else ''
        doc = ''
        if hasattr(self, "doc") and self.doc and enam in self.doc.fields:
            doc = '\n/**< %s */\n' % self.doc.fields[enam]
        _h('    %s%s%s%s%s', _n(name + (enam,)).upper(), equals, eval, comma, doc)

    _h('} %s;', tname)

def _c_type_setup(self, name, postfix):
    '''
    Sets up all the C-related state by adding additional data fields to
    all Field and Type objects.  Here is where we figure out most of our
    variable and function names.

    Recurses into child fields and list member types.
    '''
    # Do all the various names in advance
    self.c_type = _t(name + postfix)
    self.c_wiretype = 'char' if self.c_type == 'void' else self.c_type

    self.c_iterator_type = _t(name + ('iterator',))
    self.c_next_name = _n(name + ('next',))
    self.c_end_name = _n(name + ('end',))

    self.c_request_name = _n(name)
    self.c_checked_name = _n(name + ('checked',))
    self.c_unchecked_name = _n(name + ('unchecked',))
    self.c_reply_name = _n(name + ('reply',))
    self.c_reply_type = _t(name + ('reply',))
    self.c_cookie_type = _t(name + ('cookie',))
    self.c_reply_fds_name = _n(name + ('reply_fds',))

    self.c_need_aux = False
    self.c_need_serialize = False
    self.c_need_sizeof = False

    self.c_aux_name = _n(name + ('aux',))
    self.c_aux_checked_name = _n(name + ('aux', 'checked'))
    self.c_aux_unchecked_name = _n(name + ('aux', 'unchecked'))
    self.c_serialize_name = _n(name + ('serialize',))
    self.c_unserialize_name = _n(name + ('unserialize',))
    self.c_unpack_name = _n(name + ('unpack',))
    self.c_sizeof_name = _n(name + ('sizeof',))

    # special case: structs where variable size fields are followed by fixed size fields
    self.c_var_followed_by_fixed_fields = False

    if self.is_switch:
        self.c_need_serialize = True
        self.c_container = 'struct'
        for bitcase in self.bitcases:
            bitcase.c_field_name = _cpp(bitcase.field_name)
            bitcase_name = bitcase.field_type if bitcase.type.has_name else name
            _c_type_setup(bitcase.type, bitcase_name, ())

    elif self.is_container:

        self.c_container = 'union' if self.is_union else 'struct'
        prev_varsized_field = None
        prev_varsized_offset = 0
        first_field_after_varsized = None

        for field in self.fields:
            _c_type_setup(field.type, field.field_type, ())
            if field.type.is_list:
                _c_type_setup(field.type.member, field.field_type, ())
                if (field.type.nmemb is None):
                    self.c_need_sizeof = True

            field.c_field_type = _t(field.field_type)
            field.c_field_const_type = ('' if field.type.nmemb == 1 else 'const ') + field.c_field_type
            field.c_field_name = _cpp(field.field_name)
            field.c_subscript = '[%d]' % field.type.nmemb if (field.type.nmemb and field.type.nmemb > 1) else ''
            field.c_pointer = ' ' if field.type.nmemb == 1 else '*'

            # correct the c_pointer field for variable size non-list types
            if not field.type.fixed_size() and field.c_pointer == ' ':
                field.c_pointer = '*'
            if field.type.is_list and not field.type.member.fixed_size():
                field.c_pointer = '*'

            if field.type.is_switch:
                field.c_pointer = '*'
                field.c_field_const_type = 'const ' + field.c_field_type
                self.c_need_aux = True
            elif not field.type.fixed_size() and not field.type.is_bitcase:
                self.c_need_sizeof = True

            field.c_iterator_type = _t(field.field_type + ('iterator',))      # xcb_fieldtype_iterator_t
            field.c_iterator_name = _n(name + (field.field_name, 'iterator')) # xcb_container_field_iterator
            field.c_accessor_name = _n(name + (field.field_name,))            # xcb_container_field
            field.c_length_name = _n(name + (field.field_name, 'length'))     # xcb_container_field_length
            field.c_end_name = _n(name + (field.field_name, 'end'))           # xcb_container_field_end

            field.prev_varsized_field = prev_varsized_field
            field.prev_varsized_offset = prev_varsized_offset

            if prev_varsized_offset == 0:
                first_field_after_varsized = field
            field.first_field_after_varsized = first_field_after_varsized

            if field.type.fixed_size():
                prev_varsized_offset += field.type.size
                # special case: intermixed fixed and variable size fields
                if prev_varsized_field is not None and not field.type.is_pad and field.wire:
                    if not self.is_union:
                        self.c_need_serialize = True
                        self.c_var_followed_by_fixed_fields = True
            else:
                self.last_varsized_field = field
                prev_varsized_field = field
                prev_varsized_offset = 0

            if self.c_var_followed_by_fixed_fields:
                if field.type.fixed_size():
                    field.prev_varsized_field = None

    if self.c_need_serialize:
        # when _unserialize() is wanted, create _sizeof() as well for consistency reasons
        self.c_need_sizeof = True

    # as switch does never appear at toplevel,
    # continue here with type construction
    if self.is_switch:
        if self.c_type not in finished_switch:
            finished_switch.append(self.c_type)
            # special: switch C structs get pointer fields for variable-sized members
            _c_complex(self)
            for bitcase in self.bitcases:
                bitcase_name = bitcase.type.name if bitcase.type.has_name else name
                _c_accessors(bitcase.type, bitcase_name, bitcase_name)
                # no list with switch as element, so no call to
                # _c_iterator(field.type, field_name) necessary

    if not self.is_bitcase:
        if self.c_need_serialize:
            if self.c_serialize_name not in finished_serializers:
                finished_serializers.append(self.c_serialize_name)
                _c_serialize('serialize', self)

                # _unpack() and _unserialize() are only needed for special cases:
                #   switch -> unpack
                #   special cases -> unserialize
                if self.is_switch or self.c_var_followed_by_fixed_fields:
                    _c_serialize('unserialize', self)

        if self.c_need_sizeof:
            if self.c_sizeof_name not in finished_sizeof:
                if not module.namespace.is_ext or self.name[:2] == module.namespace.prefix:
                    finished_sizeof.append(self.c_sizeof_name)
                    _c_serialize('sizeof', self)
# _c_type_setup()

def _c_helper_absolute_name(prefix, field=None):
    """
    turn prefix, which is a list of tuples (name, separator, Type obj) into a string
    representing a valid name in C (based on the context)
    if field is not None, append the field name as well
    """
    prefix_str = ''
    for name, sep, obj in prefix:
        prefix_str += name
        if '' == sep:
            sep = '->'
            if ((obj.is_bitcase and obj.has_name) or     # named bitcase
                (obj.is_switch and len(obj.parents)>1)):
                sep = '.'
        prefix_str += sep
    if field is not None:
        prefix_str += _cpp(field.field_name)
    return prefix_str
# _c_absolute_name

def _c_helper_field_mapping(complex_type, prefix, flat=False):
    """
    generate absolute names, based on prefix, for all fields starting from complex_type
    if flat == True, nested complex types are not taken into account
    """
    all_fields = {}
    if complex_type.is_switch:
        for b in complex_type.bitcases:
            if b.type.has_name:
                switch_name, switch_sep, switch_type = prefix[-1]
                bitcase_prefix = prefix + [(b.type.name[-1], '.', b.type)]
            else:
                bitcase_prefix = prefix

            if (True==flat and not b.type.has_name) or False==flat:
                all_fields.update(_c_helper_field_mapping(b.type, bitcase_prefix, flat))
    else:
        for f in complex_type.fields:
            fname = _c_helper_absolute_name(prefix, f)
            if f.field_name in all_fields:
                raise Exception("field name %s has been registered before" % f.field_name)

            all_fields[f.field_name] = (fname, f)
            if f.type.is_container and flat==False:
                if f.type.is_bitcase and not f.type.has_name:
                    new_prefix = prefix
                elif f.type.is_switch and len(f.type.parents)>1:
                    # nested switch gets another separator
                    new_prefix = prefix+[(f.c_field_name, '.', f.type)]
                else:
                    new_prefix = prefix+[(f.c_field_name, '->', f.type)]
                all_fields.update(_c_helper_field_mapping(f.type, new_prefix, flat))

    return all_fields
# _c_field_mapping()

def _c_helper_resolve_field_names (prefix):
    """
    get field names for all objects in the prefix array
    """
    all_fields = {}
    tmp_prefix = []
    # look for fields in the remaining containers
    for idx, p in enumerate(prefix):
        name, sep, obj = p
        if ''==sep:
            # sep can be preset in prefix, if not, make a sensible guess
            sep = '.' if (obj.is_switch or obj.is_bitcase) else '->'
            # exception: 'toplevel' object (switch as well!) always have sep '->'
            sep = '->' if idx<1 else sep
        if not obj.is_bitcase or (obj.is_bitcase and obj.has_name):
            tmp_prefix.append((name, sep, obj))
        all_fields.update(_c_helper_field_mapping(obj, tmp_prefix, flat=True))

    return all_fields
# _c_helper_resolve_field_names

def get_expr_fields(self):
    """
    get the Fields referenced by switch or list expression
    """
    def get_expr_field_names(expr):
        if expr.op is None:
            if expr.lenfield_name is not None:
                return [expr.lenfield_name]
            else:
                # constant value expr
                return []
        else:
            if expr.op == '~':
                return get_expr_field_names(expr.rhs)
            elif expr.op == 'popcount':
                return get_expr_field_names(expr.rhs)
            elif expr.op == 'sumof':
                # sumof expr references another list,
                # we need that list's length field here
                field = None
                for f in expr.lenfield_parent.fields:
                    if f.field_name == expr.lenfield_name:
                        field = f
                        break
                if field is None:
                    raise Exception("list field '%s' referenced by sumof not found" % expr.lenfield_name)
                # referenced list + its length field
                return [expr.lenfield_name] + get_expr_field_names(field.type.expr)
            elif expr.op == 'enumref':
                return []
            else:
                return get_expr_field_names(expr.lhs) + get_expr_field_names(expr.rhs)
    # get_expr_field_names()

    # resolve the field names with the parent structure(s)
    unresolved_fields_names = get_expr_field_names(self.expr)

    # construct prefix from self
    prefix = [('', '', p) for p in self.parents]
    if self.is_container:
        prefix.append(('', '', self))

    all_fields = _c_helper_resolve_field_names (prefix)
    resolved_fields_names = list(filter(lambda x: x in all_fields.keys(), unresolved_fields_names))
    if len(unresolved_fields_names) != len(resolved_fields_names):
        raise Exception("could not resolve all fields for %s" % self.name)

    resolved_fields = [all_fields[n][1] for n in resolved_fields_names]
    return resolved_fields
# get_expr_fields()

def resolve_expr_fields(complex_obj):
    """
    find expr fields appearing in complex_obj and descendents that cannot be resolved within complex_obj
    these are normally fields that need to be given as function parameters
    """
    all_fields = []
    expr_fields = []
    unresolved = []

    for field in complex_obj.fields:
        all_fields.append(field)
        if field.type.is_switch or field.type.is_list:
            expr_fields += get_expr_fields(field.type)
        if field.type.is_container:
            expr_fields += resolve_expr_fields(field.type)

    # try to resolve expr fields
    for e in expr_fields:
        if e not in all_fields and e not in unresolved:
            unresolved.append(e)
    return unresolved
# resolve_expr_fields()

def get_serialize_params(context, self, buffer_var='_buffer', aux_var='_aux'):
    """
    functions like _serialize(), _unserialize(), and _unpack() sometimes need additional parameters:
    E.g. in order to unpack switch, extra parameters might be needed to evaluate the switch
    expression. This function tries to resolve all fields within a structure, and returns the
    unresolved fields as the list of external parameters.
    """
    def add_param(params, param):
        if param not in params:
            params.append(param)

    # collect all fields into param_fields
    param_fields = []
    wire_fields = []

    for field in self.fields:
        if field.visible:
            # the field should appear as a parameter in the function call
            param_fields.append(field)
        if field.wire and not field.auto:
            if field.type.fixed_size() and not self.is_switch:
                # field in the xcb_out structure
                wire_fields.append(field)
        # fields like 'pad0' are skipped!

    # in case of switch, parameters always contain any fields referenced in the switch expr
    # we do not need any variable size fields here, as the switch data type contains both
    # fixed and variable size fields
    if self.is_switch:
        param_fields = get_expr_fields(self)

    # _serialize()/_unserialize()/_unpack() function parameters
    # note: don't use set() for params, it is unsorted
    params = []

    # 1. the parameter for the void * buffer
    if  'serialize' == context:
        params.append(('void', '**', buffer_var))
    elif context in ('unserialize', 'unpack', 'sizeof'):
        params.append(('const void', '*', buffer_var))

    # 2. any expr fields that cannot be resolved within self and descendants
    unresolved_fields = resolve_expr_fields(self)
    for f in unresolved_fields:
        add_param(params, (f.c_field_type, '', f.c_field_name))

    # 3. param_fields contain the fields necessary to evaluate the switch expr or any other fields
    #    that do not appear in the data type struct
    for p in param_fields:
        if self.is_switch:
            typespec = p.c_field_const_type
            pointerspec = p.c_pointer
            add_param(params, (typespec, pointerspec, p.c_field_name))
        else:
            if p.visible and not p.wire and not p.auto:
                typespec = p.c_field_type
                pointerspec = ''
                add_param(params, (typespec, pointerspec, p.c_field_name))

    # 4. aux argument
    if 'serialize' == context:
        add_param(params, ('const %s' % self.c_type, '*', aux_var))
    elif 'unserialize' == context:
        add_param(params, ('%s' % self.c_type, '**', aux_var))
    elif 'unpack' == context:
        add_param(params, ('%s' % self.c_type, '*', aux_var))

    # 5. switch contains all variable size fields as struct members
    #    for other data types though, these have to be supplied separately
    #    this is important for the special case of intermixed fixed and
    #    variable size fields
    if not self.is_switch and 'serialize' == context:
        for p in param_fields:
            if not p.type.fixed_size():
                add_param(params, (p.c_field_const_type, '*', p.c_field_name))

    return (param_fields, wire_fields, params)
# get_serialize_params()

def _c_serialize_helper_insert_padding(context, code_lines, space, postpone):
    code_lines.append('%s    /* insert padding */' % space)
    code_lines.append('%s    xcb_pad = -xcb_block_len & (xcb_align_to - 1);' % space)
#    code_lines.append('%s    printf("automatically inserting padding: %%%%d\\n", xcb_pad);' % space)
    code_lines.append('%s    xcb_buffer_len += xcb_block_len + xcb_pad;' % space)

    if not postpone:
        code_lines.append('%s    if (0 != xcb_pad) {' % space)

        if 'serialize' == context:
            code_lines.append('%s        xcb_parts[xcb_parts_idx].iov_base = xcb_pad0;' % space)
            code_lines.append('%s        xcb_parts[xcb_parts_idx].iov_len = xcb_pad;' % space)
            code_lines.append('%s        xcb_parts_idx++;' % space)
        elif context in ('unserialize', 'unpack', 'sizeof'):
            code_lines.append('%s        xcb_tmp += xcb_pad;' % space)

        code_lines.append('%s        xcb_pad = 0;' % space)
        code_lines.append('%s    }' % space)

    code_lines.append('%s    xcb_block_len = 0;' % space)

    # keep tracking of xcb_parts entries for serialize
    return 1
# _c_serialize_helper_insert_padding()

def _c_serialize_helper_switch(context, self, complex_name,
                               code_lines, temp_vars,
                               space, prefix):
    count = 0
    switch_expr = _c_accessor_get_expr(self.expr, None)

    for b in self.bitcases:
        len_expr = len(b.type.expr)
        for n, expr in enumerate(b.type.expr):
            bitcase_expr = _c_accessor_get_expr(expr, None)
            # only one <enumref> in the <bitcase>
            if len_expr == 1:
                code_lines.append('    if(%s & %s) {' % (switch_expr, bitcase_expr))
            # multiple <enumref> in the <bitcase>
            elif n == 0: # first
                code_lines.append('    if((%s & %s) ||' % (switch_expr, bitcase_expr))
            elif len_expr == (n + 1): # last
                code_lines.append('       (%s & %s)) {' % (switch_expr, bitcase_expr))
            else: # between first and last
                code_lines.append('       (%s & %s) ||' % (switch_expr, bitcase_expr))

        b_prefix = prefix
        if b.type.has_name:
            b_prefix = prefix + [(b.c_field_name, '.', b.type)]

        count += _c_serialize_helper_fields(context, b.type,
                                            code_lines, temp_vars,
                                            "%s    " % space,
                                            b_prefix,
                                            is_bitcase = True)
        code_lines.append('    }')

#    if 'serialize' == context:
#        count += _c_serialize_helper_insert_padding(context, code_lines, space, False)
#    elif context in ('unserialize', 'unpack', 'sizeof'):
#        # padding
#        code_lines.append('%s    xcb_pad = -xcb_block_len & 3;' % space)
#        code_lines.append('%s    xcb_buffer_len += xcb_block_len + xcb_pad;' % space)

    return count
# _c_serialize_helper_switch

def _c_serialize_helper_switch_field(context, self, field, c_switch_variable, prefix):
    """
    handle switch by calling _serialize() or _unpack(), depending on context
    """
    # switch is handled by this function as a special case
    param_fields, wire_fields, params = get_serialize_params(context, self)
    field_mapping = _c_helper_field_mapping(self, prefix)
    prefix_str = _c_helper_absolute_name(prefix)

    # find the parameters that need to be passed to _serialize()/_unpack():
    # all switch expr fields must be given as parameters
    args = get_expr_fields(field.type)
    # length fields for variable size types in switch, normally only some of need
    # need to be passed as parameters
    switch_len_fields = resolve_expr_fields(field.type)

    # a switch field at this point _must_ be a bitcase field
    # we require that bitcases are "self-contiguous"
    bitcase_unresolved = resolve_expr_fields(self)
    if len(bitcase_unresolved) != 0:
        raise Exception('unresolved fields within bitcase is not supported at this point')

    # get the C names for the parameters
    c_field_names = ''
    for a in switch_len_fields:
        c_field_names += "%s, " % field_mapping[a.c_field_name][0]
    for a in args:
        c_field_names += "%s, " % field_mapping[a.c_field_name][0]

    # call _serialize()/_unpack() to determine the actual size
    if 'serialize' == context:
        length = "%s(&%s, %s&%s%s)" % (field.type.c_serialize_name, c_switch_variable,
                                       c_field_names, prefix_str, field.c_field_name)
    elif context in ('unserialize', 'unpack'):
        length = "%s(xcb_tmp, %s&%s%s)" % (field.type.c_unpack_name,
                                           c_field_names, prefix_str, field.c_field_name)

    return length
# _c_serialize_helper_switch_field()

def _c_serialize_helper_list_field(context, self, field,
                                   code_lines, temp_vars,
                                   space, prefix):
    """
    helper function to cope with lists of variable length
    """
    expr = field.type.expr
    prefix_str = _c_helper_absolute_name(prefix)
    param_fields, wire_fields, params = get_serialize_params('sizeof', self)
    param_names = [p[2] for p in params]

    expr_fields_names = [f.field_name for f in get_expr_fields(field.type)]
    resolved = list(filter(lambda x: x in param_names, expr_fields_names))
    unresolved = list(filter(lambda x: x not in param_names, expr_fields_names))

    field_mapping = {}
    for r in resolved:
        field_mapping[r] = (r, None)

    if len(unresolved)>0:
        tmp_prefix = prefix
        if len(tmp_prefix)==0:
            raise Exception("found an empty prefix while resolving expr field names for list %s",
                            field.c_field_name)

        field_mapping.update(_c_helper_resolve_field_names(prefix))
        resolved += list(filter(lambda x: x in field_mapping, unresolved))
        unresolved = list(filter(lambda x: x not in field_mapping, unresolved))
        if len(unresolved)>0:
            raise Exception('could not resolve the length fields required for list %s' % field.c_field_name)

    list_length = _c_accessor_get_expr(expr, field_mapping)

    # default: list with fixed size elements
    length = '%s * sizeof(%s)' % (list_length, field.type.member.c_wiretype)

    # list with variable-sized elements
    if not field.type.member.fixed_size():
        length = ''
        if context in ('unserialize', 'sizeof', 'unpack'):
            int_i = '    unsigned int i;'
            xcb_tmp_len = '    unsigned int xcb_tmp_len;'
            if int_i not in temp_vars:
                temp_vars.append(int_i)
            if xcb_tmp_len not in temp_vars:
                temp_vars.append(xcb_tmp_len)
            # loop over all list elements and call sizeof repeatedly
            # this should be a bit faster than using the iterators
            code_lines.append("%s    for(i=0; i<%s; i++) {" % (space, list_length))
            code_lines.append("%s        xcb_tmp_len = %s(xcb_tmp);" %
                              (space, field.type.c_sizeof_name))
            code_lines.append("%s        xcb_block_len += xcb_tmp_len;" % space)
            code_lines.append("%s        xcb_tmp += xcb_tmp_len;" % space)
            code_lines.append("%s    }" % space)

        elif 'serialize' == context:
            code_lines.append('%s    xcb_parts[xcb_parts_idx].iov_len = 0;' % space)
            code_lines.append('%s    xcb_tmp = (char *) %s%s;' % (space, prefix_str, field.c_field_name))
            code_lines.append('%s    for(i=0; i<%s; i++) { ' % (space, list_length))
            code_lines.append('%s        xcb_block_len = %s(xcb_tmp);' % (space, field.type.c_sizeof_name))
            code_lines.append('%s        xcb_parts[xcb_parts_idx].iov_len += xcb_block_len;' % space)
            code_lines.append('%s    }' % space)
            code_lines.append('%s    xcb_block_len = xcb_parts[xcb_parts_idx].iov_len;' % space)

    return length
# _c_serialize_helper_list_field()

def _c_serialize_helper_fields_fixed_size(context, self, field,
                                          code_lines, temp_vars,
                                          space, prefix):
    # keep the C code a bit more readable by giving the field name
    if not self.is_bitcase:
        code_lines.append('%s    /* %s.%s */' % (space, self.c_type, field.c_field_name))
    else:
        scoped_name = [p[2].c_type if idx==0 else p[0] for idx, p in enumerate(prefix)]
        typename = reduce(lambda x,y: "%s.%s" % (x, y), scoped_name)
        code_lines.append('%s    /* %s.%s */' % (space, typename, field.c_field_name))

    abs_field_name = _c_helper_absolute_name(prefix, field)
    # default for simple cases: call sizeof()
    length = "sizeof(%s)" % field.c_field_type

    if context in ('unserialize', 'unpack', 'sizeof'):
        # default: simple cast
        value = '    %s = *(%s *)xcb_tmp;' % (abs_field_name, field.c_field_type)

        # padding - we could probably just ignore it
        if field.type.is_pad and field.type.nmemb > 1:
            value = ''
            for i in range(field.type.nmemb):
                code_lines.append('%s    %s[%d] = *(%s *)xcb_tmp;' %
                                  (space, abs_field_name, i, field.c_field_type))
            # total padding = sizeof(pad0) * nmemb
            length += " * %d" % field.type.nmemb

        if field.type.is_list:
            # no such case in the protocol, cannot be tested and therefore ignored for now
            raise Exception('list with fixed number of elemens unhandled in _unserialize()')

    elif 'serialize' == context:
        value = '    xcb_parts[xcb_parts_idx].iov_base = (char *) '

        if field.type.is_expr:
            # need to register a temporary variable for the expression in case we know its type
            if field.type.c_type is None:
                raise Exception("type for field '%s' (expression '%s') unkown" %
                                (field.field_name, _c_accessor_get_expr(field.type.expr)))

            temp_vars.append('    %s xcb_expr_%s = %s;' % (field.type.c_type, _cpp(field.field_name),
                                                           _c_accessor_get_expr(field.type.expr, prefix)))
            value += "&xcb_expr_%s;" % _cpp(field.field_name)

        elif field.type.is_pad:
            if field.type.nmemb == 1:
                value += "&xcb_pad;"
            else:
                # we could also set it to 0, see definition of xcb_send_request()
                value = '    xcb_parts[xcb_parts_idx].iov_base = xcb_pad0;'
                length += "*%d" % field.type.nmemb

        else:
            # non-list type with fixed size
            if field.type.nmemb == 1:
                value += "&%s;" % (abs_field_name)

            # list with nmemb (fixed size) elements
            else:
                value += '%s;' % (abs_field_name)
                length = '%d' % field.type.nmemb

    return (value, length)
# _c_serialize_helper_fields_fixed_size()

def _c_serialize_helper_fields_variable_size(context, self, field,
                                             code_lines, temp_vars,
                                             space, prefix):
    prefix_str = _c_helper_absolute_name(prefix)

    if context in ('unserialize', 'unpack', 'sizeof'):
        value = ''
        var_field_name = 'xcb_tmp'

        # special case: intermixed fixed and variable size fields
        if self.c_var_followed_by_fixed_fields and 'unserialize' == context:
            value = '    %s = (%s *)xcb_tmp;' % (field.c_field_name, field.c_field_type)
            temp_vars.append('    %s *%s;' % (field.type.c_type, field.c_field_name))
        # special case: switch
        if 'unpack' == context:
            value = '    %s%s = (%s *)xcb_tmp;' % (prefix_str, field.c_field_name, field.c_field_type)

    elif 'serialize' == context:
        # variable size fields appear as parameters to _serialize() if the
        # 'toplevel' container is not a switch
        prefix_string = prefix_str if prefix[0][2].is_switch else ''
        var_field_name = "%s%s" % (prefix_string, field.c_field_name)
        value = '    xcb_parts[xcb_parts_idx].iov_base = (char *) %s;' % var_field_name

    length = ''

    code_lines.append('%s    /* %s */' % (space, field.c_field_name))

    if field.type.is_list:
        if value != '':
            # in any context, list is already a pointer, so the default assignment is ok
            code_lines.append("%s%s" % (space, value))
            value = ''
        length = _c_serialize_helper_list_field(context, self, field,
                                                code_lines, temp_vars,
                                                space, prefix)

    elif field.type.is_switch:
        value = ''
        if context == 'serialize':
            # the _serialize() function allocates the correct amount memory if given a NULL pointer
            value = '    xcb_parts[xcb_parts_idx].iov_base = (char *)0;'
        length = _c_serialize_helper_switch_field(context, self, field,
                                                  'xcb_parts[xcb_parts_idx].iov_base',
                                                  prefix)

    else:
        # in all remaining special cases - call _sizeof()
        length = "%s(%s)" % (field.type.c_sizeof_name, var_field_name)

    return (value, length)
# _c_serialize_helper_fields_variable_size

def _c_serialize_helper_fields(context, self,
                               code_lines, temp_vars,
                               space, prefix, is_bitcase):
    count = 0
    need_padding = False
    prev_field_was_variable = False

    for field in self.fields:
        if not field.visible:
            if not ((field.wire and not field.auto) or 'unserialize' == context):
                continue

        # switch/bitcase: fixed size fields must be considered explicitly
        if field.type.fixed_size():
            if self.is_bitcase or self.c_var_followed_by_fixed_fields:
                if prev_field_was_variable and need_padding:
                    # insert padding
#                    count += _c_serialize_helper_insert_padding(context, code_lines, space,
#                                                                self.c_var_followed_by_fixed_fields)
                    prev_field_was_variable = False

                # prefix for fixed size fields
                fixed_prefix = prefix

                value, length = _c_serialize_helper_fields_fixed_size(context, self, field,
                                                                      code_lines, temp_vars,
                                                                      space, fixed_prefix)
            else:
                continue

        # fields with variable size
        else:
            if field.type.is_pad:
                # Variable length pad is <pad align= />
                code_lines.append('%s    xcb_align_to = %d;' % (space, field.type.align))
                count += _c_serialize_helper_insert_padding(context, code_lines, space,
                                                        self.c_var_followed_by_fixed_fields)
                continue
            else:
                # switch/bitcase: always calculate padding before and after variable sized fields
                if need_padding or is_bitcase:
                    count += _c_serialize_helper_insert_padding(context, code_lines, space,
                                                            self.c_var_followed_by_fixed_fields)

                value, length = _c_serialize_helper_fields_variable_size(context, self, field,
                                                                         code_lines, temp_vars,
                                                                         space, prefix)
                prev_field_was_variable = True

        # save (un)serialization C code
        if '' != value:
            code_lines.append('%s%s' % (space, value))

        if field.type.fixed_size():
            if is_bitcase or self.c_var_followed_by_fixed_fields:
                # keep track of (un)serialized object's size
                code_lines.append('%s    xcb_block_len += %s;' % (space, length))
                if context in ('unserialize', 'unpack', 'sizeof'):
                    code_lines.append('%s    xcb_tmp += %s;' % (space, length))
        else:
            # variable size objects or bitcase:
            #   value & length might have been inserted earlier for special cases
            if '' != length:
                # special case: intermixed fixed and variable size fields
                if (not field.type.fixed_size() and
                    self.c_var_followed_by_fixed_fields and 'unserialize' == context):
                    temp_vars.append('    int %s_len;' % field.c_field_name)
                    code_lines.append('%s    %s_len = %s;' % (space, field.c_field_name, length))
                    code_lines.append('%s    xcb_block_len += %s_len;' % (space, field.c_field_name))
                    code_lines.append('%s    xcb_tmp += %s_len;' % (space, field.c_field_name))
                else:
                    code_lines.append('%s    xcb_block_len += %s;' % (space, length))
                    # increase pointer into the byte stream accordingly
                    if context in ('unserialize', 'sizeof', 'unpack'):
                        code_lines.append('%s    xcb_tmp += xcb_block_len;' % space)

        if 'serialize' == context:
            if '' != length:
                code_lines.append('%s    xcb_parts[xcb_parts_idx].iov_len = %s;' % (space, length))
            code_lines.append('%s    xcb_parts_idx++;' % space)
            count += 1

        code_lines.append('%s    xcb_align_to = ALIGNOF(%s);' % (space, 'char' if field.c_field_type == 'void' else field.c_field_type))

        need_padding = True
        if self.c_var_followed_by_fixed_fields:
            need_padding = False

    return count
# _c_serialize_helper_fields()

def _c_serialize_helper(context, complex_type,
                        code_lines, temp_vars,
                        space='', prefix=[]):
    # count tracks the number of fields to serialize
    count = 0

    if hasattr(complex_type, 'type'):
        self = complex_type.type
        complex_name = complex_type.name
    else:
        self = complex_type
        if self.c_var_followed_by_fixed_fields and 'unserialize' == context:
            complex_name = 'xcb_out'
        else:
            complex_name = '_aux'

    # special case: switch is serialized by evaluating each bitcase separately
    if self.is_switch:
        count += _c_serialize_helper_switch(context, self, complex_name,
                                            code_lines, temp_vars,
                                            space, prefix)

    # all other data types can be evaluated one field a time
    else:
        # unserialize & fixed size fields: simply cast the buffer to the respective xcb_out type
        if context in ('unserialize', 'unpack', 'sizeof') and not self.c_var_followed_by_fixed_fields:
            code_lines.append('%s    xcb_block_len += sizeof(%s);' % (space, self.c_type))
            code_lines.append('%s    xcb_tmp += xcb_block_len;' % space)
            code_lines.append('%s    xcb_buffer_len += xcb_block_len;' % space)
            code_lines.append('%s    xcb_block_len = 0;' % space)

        count += _c_serialize_helper_fields(context, self,
                                            code_lines, temp_vars,
                                            space, prefix, False)
    # "final padding"
    count += _c_serialize_helper_insert_padding(context, code_lines, space, False)

    return count
# _c_serialize_helper()

def _c_serialize(context, self):
    """
    depending on the context variable, generate _serialize(), _unserialize(), _unpack(), or _sizeof()
    for the ComplexType variable self
    """
    _h_setlevel(1)
    _c_setlevel(1)

    _hc('')
    # _serialize() returns the buffer size
    _hc('int')

    if self.is_switch and 'unserialize' == context:
        context = 'unpack'

    cases = { 'serialize'   : self.c_serialize_name,
              'unserialize' : self.c_unserialize_name,
              'unpack'      : self.c_unpack_name,
              'sizeof'      : self.c_sizeof_name }
    func_name = cases[context]

    param_fields, wire_fields, params = get_serialize_params(context, self)
    variable_size_fields = 0
    # maximum space required for type definition of function arguments
    maxtypelen = 0

    # determine N(variable_fields)
    for field in param_fields:
        # if self.is_switch, treat all fields as if they are variable sized
        if not field.type.fixed_size() or self.is_switch:
            variable_size_fields += 1
    # determine maxtypelen
    for p in params:
        maxtypelen = max(maxtypelen, len(p[0]) + len(p[1]))

    # write to .c/.h
    indent = ' '*(len(func_name)+2)
    param_str = []
    for p in params:
        typespec, pointerspec, field_name = p
        spacing = ' '*(maxtypelen-len(typespec)-len(pointerspec))
        param_str.append("%s%s%s  %s%s  /**< */" % (indent, typespec, spacing, pointerspec, field_name))
    # insert function name
    param_str[0] = "%s (%s" % (func_name, param_str[0].strip())
    param_str = list(map(lambda x: "%s," % x, param_str))
    for s in param_str[:-1]:
        _hc(s)
    _h("%s);" % param_str[-1].rstrip(','))
    _c("%s)" % param_str[-1].rstrip(','))
    _c('{')

    code_lines = []
    temp_vars = []
    prefix = []

    if 'serialize' == context:
        if not self.is_switch and not self.c_var_followed_by_fixed_fields:
            _c('    %s *xcb_out = *_buffer;', self.c_type)
            _c('    unsigned int xcb_out_pad = -sizeof(%s) & 3;', self.c_type)
            _c('    unsigned int xcb_buffer_len = sizeof(%s) + xcb_out_pad;', self.c_type)
            _c('    unsigned int xcb_align_to = 0;')
        else:
            _c('    char *xcb_out = *_buffer;')
            _c('    unsigned int xcb_buffer_len = 0;')
            _c('    unsigned int xcb_align_to = 0;')
        prefix = [('_aux', '->', self)]
        aux_ptr = 'xcb_out'

    elif context in ('unserialize', 'unpack'):
        _c('    char *xcb_tmp = (char *)_buffer;')
        if not self.is_switch:
            if not self.c_var_followed_by_fixed_fields:
                _c('    const %s *_aux = (%s *)_buffer;', self.c_type, self.c_type)
                prefix = [('_aux', '->', self)]
            else:
                _c('    %s xcb_out;', self.c_type)
                prefix = [('xcb_out', '.', self)]
        else:
            aux_var = '_aux' # default for unpack: single pointer
            # note: unserialize not generated for switch
            if 'unserialize' == context:
                aux_var = '(*_aux)' # unserialize: double pointer (!)
            prefix = [(aux_var, '->', self)]
        aux_ptr = '*_aux'
        _c('    unsigned int xcb_buffer_len = 0;')
        _c('    unsigned int xcb_block_len = 0;')
        _c('    unsigned int xcb_pad = 0;')
        _c('    unsigned int xcb_align_to = 0;')

    elif 'sizeof' == context:
        param_names = [p[2] for p in params]
        if self.is_switch:
            # switch: call _unpack()
            _c('    %s _aux;', self.c_type)
            _c('    return %s(%s, &_aux);', self.c_unpack_name, reduce(lambda x,y: "%s, %s" % (x, y), param_names))
            _c('}')
            return
        elif self.c_var_followed_by_fixed_fields:
            # special case: call _unserialize()
            _c('    return %s(%s, NULL);', self.c_unserialize_name, reduce(lambda x,y: "%s, %s" % (x, y), param_names))
            _c('}')
            return
        else:
            _c('    char *xcb_tmp = (char *)_buffer;')
            prefix = [('_aux', '->', self)]

    count = _c_serialize_helper(context, self, code_lines, temp_vars, prefix=prefix)
    # update variable size fields (only important for context=='serialize'
    variable_size_fields = count
    if 'serialize' == context:
        temp_vars.append('    unsigned int xcb_pad = 0;')
        temp_vars.append('    char xcb_pad0[3] = {0, 0, 0};')
        temp_vars.append('    struct iovec xcb_parts[%d];' % count)
        temp_vars.append('    unsigned int xcb_parts_idx = 0;')
        temp_vars.append('    unsigned int xcb_block_len = 0;')
        temp_vars.append('    unsigned int i;')
        temp_vars.append('    char *xcb_tmp;')
    elif 'sizeof' == context:
        # neither switch nor intermixed fixed and variable size fields:
        # evaluate parameters directly
        if not (self.is_switch or self.c_var_followed_by_fixed_fields):

            # look if we have to declare an '_aux' variable at all
            if len(list(filter(lambda x: x.find('_aux')!=-1, code_lines)))>0:
                if not self.c_var_followed_by_fixed_fields:
                    _c('    const %s *_aux = (%s *)_buffer;', self.c_type, self.c_type)
                else:
                    _c('    %s *_aux = malloc(sizeof(%s));', self.c_type, self.c_type)

            _c('    unsigned int xcb_buffer_len = 0;')
            _c('    unsigned int xcb_block_len = 0;')
            _c('    unsigned int xcb_pad = 0;')
            _c('    unsigned int xcb_align_to = 0;')

    _c('')
    for t in temp_vars:
        _c(t)
    _c('')
    for l in code_lines:
        _c(l)

    # variable sized fields have been collected, now
    # allocate memory and copy everything into a continuous memory area
    # note: this is not necessary in case of unpack
    if context in ('serialize', 'unserialize'):
        # unserialize: check for sizeof-only invocation
        if 'unserialize' == context:
            _c('')
            _c('    if (NULL == _aux)')
            _c('        return xcb_buffer_len;')

        _c('')
        _c('    if (NULL == %s) {', aux_ptr)
        _c('        /* allocate memory */')
        _c('        %s = malloc(xcb_buffer_len);', aux_ptr)
        if 'serialize' == context:
            _c('        *_buffer = xcb_out;')
        _c('    }')
        _c('')

        # serialize: handle variable size fields in a loop
        if 'serialize' == context:
            if not self.is_switch and not self.c_var_followed_by_fixed_fields:
                if len(wire_fields)>0:
                    _c('    *xcb_out = *_aux;')
            # copy variable size fields into the buffer
            if variable_size_fields > 0:
                # xcb_out padding
                if not self.is_switch and not self.c_var_followed_by_fixed_fields:
                    _c('    xcb_tmp = (char*)++xcb_out;')
                    _c('    xcb_tmp += xcb_out_pad;')
                else:
                    _c('    xcb_tmp = xcb_out;')

                # variable sized fields
                _c('    for(i=0; i<xcb_parts_idx; i++) {')
                _c('        if (0 != xcb_parts[i].iov_base && 0 != xcb_parts[i].iov_len)')
                _c('            memcpy(xcb_tmp, xcb_parts[i].iov_base, xcb_parts[i].iov_len);')
                _c('        if (0 != xcb_parts[i].iov_len)')
                _c('            xcb_tmp += xcb_parts[i].iov_len;')
                _c('    }')

        # unserialize: assign variable size fields individually
        if 'unserialize' == context:
            _c('    xcb_tmp = ((char *)*_aux)+xcb_buffer_len;')
            param_fields.reverse()
            for field in param_fields:
                if not field.type.fixed_size():
                    _c('    xcb_tmp -= %s_len;', field.c_field_name)
                    _c('    memmove(xcb_tmp, %s, %s_len);', field.c_field_name, field.c_field_name)
            _c('    *%s = xcb_out;', aux_ptr)

    _c('')
    _c('    return xcb_buffer_len;')
    _c('}')
# _c_serialize()

def _c_iterator_get_end(field, accum):
    '''
    Figures out what C code is needed to find the end of a variable-length structure field.
    For nested structures, recurses into its last variable-sized field.
    For lists, calls the end function
    '''
    if field.type.is_container:
        accum = field.c_accessor_name + '(' + accum + ')'
        return _c_iterator_get_end(field.type.last_varsized_field, accum)
    if field.type.is_list:
        # XXX we can always use the first way
        if field.type.member.is_simple:
            return field.c_end_name + '(' + accum + ')'
        else:
            return field.type.member.c_end_name + '(' + field.c_iterator_name + '(' + accum + '))'

def _c_iterator(self, name):
    '''
    Declares the iterator structure and next/end functions for a given type.
    '''
    _h_setlevel(0)
    _h('')
    _h('/**')
    _h(' * @brief %s', self.c_iterator_type)
    _h(' **/')
    _h('typedef struct %s {', self.c_iterator_type)
    _h('    %s *data; /**<  */', self.c_type)
    _h('    int%s rem; /**<  */', ' ' * (len(self.c_type) - 2))
    _h('    int%s index; /**<  */', ' ' * (len(self.c_type) - 2))
    _h('} %s;', self.c_iterator_type)

    _h_setlevel(1)
    _c_setlevel(1)
    _h('')
    _h('/**')
    _h(' * Get the next element of the iterator')
    _h(' * @param i Pointer to a %s', self.c_iterator_type)
    _h(' *')
    _h(' * Get the next element in the iterator. The member rem is')
    _h(' * decreased by one. The member data points to the next')
    _h(' * element. The member index is increased by sizeof(%s)', self.c_type)
    _h(' */')
    _c('')
    _hc('void')
    _h('%s (%s *i  /**< */);', self.c_next_name, self.c_iterator_type)
    _c('%s (%s *i  /**< */)', self.c_next_name, self.c_iterator_type)
    _c('{')

    if not self.fixed_size():
        _c('    %s *R = i->data;', self.c_type)

        if self.is_union:
            # FIXME - how to determine the size of a variable size union??
            _c('    /* FIXME - determine the size of the union %s */', self.c_type)
        else:
            if self.c_need_sizeof:
                _c('    xcb_generic_iterator_t child;')
                _c('    child.data = (%s *)(((char *)R) + %s(R));',
                   self.c_type, self.c_sizeof_name)
                _c('    i->index = (char *) child.data - (char *) i->data;')
            else:
                _c('    xcb_generic_iterator_t child = %s;', _c_iterator_get_end(self.last_varsized_field, 'R'))
                _c('    i->index = child.index;')
            _c('    --i->rem;')
            _c('    i->data = (%s *) child.data;', self.c_type)

    else:
        _c('    --i->rem;')
        _c('    ++i->data;')
        _c('    i->index += sizeof(%s);', self.c_type)

    _c('}')

    _h('')
    _h('/**')
    _h(' * Return the iterator pointing to the last element')
    _h(' * @param i An %s', self.c_iterator_type)
    _h(' * @return  The iterator pointing to the last element')
    _h(' *')
    _h(' * Set the current element in the iterator to the last element.')
    _h(' * The member rem is set to 0. The member data points to the')
    _h(' * last element.')
    _h(' */')
    _c('')
    _hc('xcb_generic_iterator_t')
    _h('%s (%s i  /**< */);', self.c_end_name, self.c_iterator_type)
    _c('%s (%s i  /**< */)', self.c_end_name, self.c_iterator_type)
    _c('{')
    _c('    xcb_generic_iterator_t ret;')

    if self.fixed_size():
        _c('    ret.data = i.data + i.rem;')
        _c('    ret.index = i.index + ((char *) ret.data - (char *) i.data);')
        _c('    ret.rem = 0;')
    else:
        _c('    while(i.rem > 0)')
        _c('        %s(&i);', self.c_next_name)
        _c('    ret.data = i.data;')
        _c('    ret.rem = i.rem;')
        _c('    ret.index = i.index;')

    _c('    return ret;')
    _c('}')

def _c_accessor_get_length(expr, field_mapping=None):
    '''
    Figures out what C code is needed to get a length field.
    The field_mapping parameter can be used to change the absolute name of a length field.
    For fields that follow a variable-length field, use the accessor.
    Otherwise, just reference the structure field directly.
    '''

    lenfield_name = expr.lenfield_name
    if lenfield_name is not None:
        if field_mapping is not None:
            lenfield_name = field_mapping[lenfield_name][0]

    if expr.lenfield is not None and expr.lenfield.prev_varsized_field is not None:
        # special case: variable and fixed size fields are intermixed
        # if the lenfield is among the fixed size fields, there is no need
        # to call a special accessor function like <expr.lenfield.c_accessor_name + '(' + prefix + ')'>
        return field_mapping(expr.lenfield_name)
    elif expr.lenfield_name is not None:
        return lenfield_name
    else:
        return str(expr.nmemb)

def _c_accessor_get_expr(expr, field_mapping):
    '''
    Figures out what C code is needed to get the length of a list field.
    The field_mapping parameter can be used to change the absolute name of a length field.
    Recurses for math operations.
    Returns bitcount for value-mask fields.
    Otherwise, uses the value of the length field.
    '''
    lenexp = _c_accessor_get_length(expr, field_mapping)

    if expr.op == '~':
        return '(' + '~' + _c_accessor_get_expr(expr.rhs, field_mapping) + ')'
    elif expr.op == 'popcount':
        return 'xcb_popcount(' + _c_accessor_get_expr(expr.rhs, field_mapping) + ')'
    elif expr.op == 'enumref':
        enum_name = expr.lenfield_type.name
        constant_name = expr.lenfield_name
        c_name = _n(enum_name + (constant_name,)).upper()
        return c_name
    elif expr.op == 'sumof':
        # locate the referenced list object
        list_obj = expr.lenfield_type
        field = None
        for f in expr.lenfield_parent.fields:
            if f.field_name == expr.lenfield_name:
                field = f
                break

        if field is None:
            raise Exception("list field '%s' referenced by sumof not found" % expr.lenfield_name)
        list_name = field_mapping[field.c_field_name][0]
        c_length_func = "%s(%s)" % (field.c_length_name, list_name)
        # note: xcb_sumof() has only been defined for integers
        c_length_func = _c_accessor_get_expr(field.type.expr, field_mapping)
        return 'xcb_sumof(%s, %s)' % (list_name, c_length_func)
    elif expr.op != None:
        return ('(' + _c_accessor_get_expr(expr.lhs, field_mapping) +
                ' ' + expr.op + ' ' +
                _c_accessor_get_expr(expr.rhs, field_mapping) + ')')
    elif expr.bitfield:
        return 'xcb_popcount(' + lenexp + ')'
    else:
        return lenexp

def type_pad_type(type):
    if type == 'void':
        return 'char'
    return type

def _c_accessors_field(self, field):
    '''
    Declares the accessor functions for a non-list field that follows a variable-length field.
    '''
    c_type = self.c_type

    # special case: switch
    switch_obj = self if self.is_switch else None
    if self.is_bitcase:
        switch_obj = self.parents[-1]
    if switch_obj is not None:
        c_type = switch_obj.c_type

    if field.type.is_simple:
        _hc('')
        _hc('%s', field.c_field_type)
        _h('%s (const %s *R  /**< */);', field.c_accessor_name, c_type)
        _c('%s (const %s *R  /**< */)', field.c_accessor_name, c_type)
        _c('{')
        if field.prev_varsized_field is None:
            _c('    return (%s *) (R + 1);', field.c_field_type)
        else:
            _c('    xcb_generic_iterator_t prev = %s;', _c_iterator_get_end(field.prev_varsized_field, 'R'))
            _c('    return * (%s *) ((char *) prev.data + XCB_TYPE_PAD(%s, prev.index) + %d);',
               field.c_field_type, type_pad_type(field.first_field_after_varsized.type.c_type), field.prev_varsized_offset)
        _c('}')
    else:
        _hc('')
        if field.type.is_switch and switch_obj is None:
            return_type = 'void *'
        else:
            return_type = '%s *' % field.c_field_type

        _hc(return_type)
        _h('%s (const %s *R  /**< */);', field.c_accessor_name, c_type)
        _c('%s (const %s *R  /**< */)', field.c_accessor_name, c_type)
        _c('{')
        if field.prev_varsized_field is None:
            _c('    return (%s) (R + 1);', return_type)
            # note: the special case 'variable fields followed by fixed size fields'
            #       is not of any consequence here, since the ordering gets
            #       'corrected' in the reply function
        else:
            _c('    xcb_generic_iterator_t prev = %s;', _c_iterator_get_end(field.prev_varsized_field, 'R'))
            _c('    return (%s) ((char *) prev.data + XCB_TYPE_PAD(%s, prev.index) + %d);',
               return_type, type_pad_type(field.first_field_after_varsized.type.c_type), field.prev_varsized_offset)
        _c('}')


def _c_accessors_list(self, field):
    '''
    Declares the accessor functions for a list field.
    Declares a direct-accessor function only if the list members are fixed size.
    Declares length and get-iterator functions always.
    '''

    def get_align_pad(field):
            prev = field.prev_varsized_field
            prev_prev = field.prev_varsized_field.prev_varsized_field

            if (prev.type.is_pad and prev.type.align > 0 and prev_prev is not None):
                return (prev_prev, '((-prev.index) & (%d - 1))' % prev.type.align)
            else:
                return (prev, None)


    list = field.type
    c_type = self.c_type

    # special case: switch
    # in case of switch, 2 params have to be supplied to certain accessor functions:
    #   1. the anchestor object (request or reply)
    #   2. the (anchestor) switch object
    # the reason is that switch is either a child of a request/reply or nested in another switch,
    # so whenever we need to access a length field, we might need to refer to some anchestor type
    switch_obj = self if self.is_switch else None
    if self.is_bitcase:
        switch_obj = self.parents[-1]
    if switch_obj is not None:
        c_type = switch_obj.c_type

    params = []
    fields = {}
    parents = self.parents if hasattr(self, 'parents') else [self]
    # 'R': parents[0] is always the 'toplevel' container type
    params.append(('const %s *R' % parents[0].c_type, parents[0]))
    fields.update(_c_helper_field_mapping(parents[0], [('R', '->', parents[0])], flat=True))
    # auxiliary object for 'R' parameters
    R_obj = parents[0]

    if switch_obj is not None:
        # now look where the fields are defined that are needed to evaluate
        # the switch expr, and store the parent objects in accessor_params and
        # the fields in switch_fields

        # 'S': name for the 'toplevel' switch
        toplevel_switch = parents[1]
        params.append(('const %s *S' % toplevel_switch.c_type, toplevel_switch))
        fields.update(_c_helper_field_mapping(toplevel_switch, [('S', '->', toplevel_switch)], flat=True))

        # initialize prefix for everything "below" S
        prefix_str = '/* %s */ S' % toplevel_switch.name[-1]
        prefix = [(prefix_str, '->', toplevel_switch)]

        # look for fields in the remaining containers
        for p in parents[2:] + [self]:
            # the separator between parent and child is always '.' here,
            # because of nested switch statements
            if not p.is_bitcase or (p.is_bitcase and p.has_name):
                prefix.append((p.name[-1], '.', p))
            fields.update(_c_helper_field_mapping(p, prefix, flat=True))

        # auxiliary object for 'S' parameter
        S_obj = parents[1]

    _h_setlevel(1)
    _c_setlevel(1)
    if list.member.fixed_size():
        idx = 1 if switch_obj is not None else 0
        _hc('')
        _hc('%s *', field.c_field_type)

        _h('%s (%s  /**< */);', field.c_accessor_name, params[idx][0])
        _c('%s (%s  /**< */)', field.c_accessor_name, params[idx][0])

        _c('{')
        if switch_obj is not None:
            _c('    return %s;', fields[field.c_field_name][0])
        elif field.prev_varsized_field is None:
            _c('    return (%s *) (R + 1);', field.c_field_type)
        else:
            (prev_varsized_field, align_pad) = get_align_pad(field)

            if align_pad is None:
                align_pad = ('XCB_TYPE_PAD(%s, prev.index)' %
                    type_pad_type(field.first_field_after_varsized.type.c_type))

            _c('    xcb_generic_iterator_t prev = %s;',
                _c_iterator_get_end(prev_varsized_field, 'R'))
            _c('    return (%s *) ((char *) prev.data + %s + %d);',
               field.c_field_type, align_pad, field.prev_varsized_offset)
        _c('}')

    _hc('')
    _hc('int')
    if switch_obj is not None:
        _hc('%s (const %s *R  /**< */,', field.c_length_name, R_obj.c_type)
        spacing = ' '*(len(field.c_length_name)+2)
        _h('%sconst %s *S /**< */);', spacing, S_obj.c_type)
        _c('%sconst %s *S  /**< */)', spacing, S_obj.c_type)
        length = _c_accessor_get_expr(field.type.expr, fields)
    else:
        _h('%s (const %s *R  /**< */);', field.c_length_name, c_type)
        _c('%s (const %s *R  /**< */)', field.c_length_name, c_type)
        length = _c_accessor_get_expr(field.type.expr, fields)
    _c('{')
    _c('    return %s;', length)
    _c('}')

    if field.type.member.is_simple:
        _hc('')
        _hc('xcb_generic_iterator_t')
        if switch_obj is not None:
            _hc('%s (const %s *R  /**< */,', field.c_end_name, R_obj.c_type)
            spacing = ' '*(len(field.c_end_name)+2)
            _h('%sconst %s *S /**< */);', spacing, S_obj.c_type)
            _c('%sconst %s *S  /**< */)', spacing, S_obj.c_type)
        else:
            _h('%s (const %s *R  /**< */);', field.c_end_name, c_type)
            _c('%s (const %s *R  /**< */)', field.c_end_name, c_type)
        _c('{')
        _c('    xcb_generic_iterator_t i;')

        param = 'R' if switch_obj is None else 'S'
        if switch_obj is not None:
            _c('    i.data = %s + %s;', fields[field.c_field_name][0],
               _c_accessor_get_expr(field.type.expr, fields))
        elif field.prev_varsized_field == None:
            _c('    i.data = ((%s *) (R + 1)) + (%s);', field.type.c_wiretype,
               _c_accessor_get_expr(field.type.expr, fields))
        else:
            _c('    xcb_generic_iterator_t child = %s;',
               _c_iterator_get_end(field.prev_varsized_field, 'R'))
            _c('    i.data = ((%s *) child.data) + (%s);', field.type.c_wiretype,
               _c_accessor_get_expr(field.type.expr, fields))

        _c('    i.rem = 0;')
        _c('    i.index = (char *) i.data - (char *) %s;', param)
        _c('    return i;')
        _c('}')

    else:
        _hc('')
        _hc('%s', field.c_iterator_type)
        if switch_obj is not None:
            _hc('%s (const %s *R  /**< */,', field.c_iterator_name, R_obj.c_type)
            spacing = ' '*(len(field.c_iterator_name)+2)
            _h('%sconst %s *S /**< */);', spacing, S_obj.c_type)
            _c('%sconst %s *S  /**< */)', spacing, S_obj.c_type)
        else:
            _h('%s (const %s *R  /**< */);', field.c_iterator_name, c_type)
            _c('%s (const %s *R  /**< */)', field.c_iterator_name, c_type)
        _c('{')
        _c('    %s i;', field.c_iterator_type)

        if switch_obj is not None:
            _c('    i.data = %s;', fields[field.c_field_name][0])
            _c('    i.rem = %s;', _c_accessor_get_expr(field.type.expr, fields))
        elif field.prev_varsized_field == None:
            _c('    i.data = (%s *) (R + 1);', field.c_field_type)
        else:
            (prev_varsized_field, align_pad) = get_align_pad(field)

            if align_pad is None:
                align_pad = ('XCB_TYPE_PAD(%s, prev.index)' %
                    type_pad_type(field.c_field_type))

            _c('    xcb_generic_iterator_t prev = %s;',
                _c_iterator_get_end(prev_varsized_field, 'R'))
            _c('    i.data = (%s *) ((char *) prev.data + %s);',
                field.c_field_type, align_pad)

        if switch_obj is None:
            _c('    i.rem = %s;', _c_accessor_get_expr(field.type.expr, fields))
        _c('    i.index = (char *) i.data - (char *) %s;', 'R' if switch_obj is None else 'S' )
        _c('    return i;')
        _c('}')

def _c_accessors(self, name, base):
    '''
    Declares the accessor functions for the fields of a structure.
    '''
    # no accessors for switch itself -
    # switch always needs to be unpacked explicitly
#    if self.is_switch:
#        pass
#    else:
    if True:
        for field in self.fields:
            if not field.type.is_pad:
                if field.type.is_list and not field.type.fixed_size():
                    _c_accessors_list(self, field)
                elif field.prev_varsized_field is not None or not field.type.fixed_size():
                    _c_accessors_field(self, field)

def c_simple(self, name):
    '''
    Exported function that handles cardinal type declarations.
    These are types which are typedef'd to one of the CARDx's, char, float, etc.
    '''
    _c_type_setup(self, name, ())

    if (self.name != name):
        # Typedef
        _h_setlevel(0)
        my_name = _t(name)
        _h('')
        _h('typedef %s %s;', _t(self.name), my_name)

        # Iterator
        _c_iterator(self, name)

def _c_complex(self, force_packed = False):
    '''
    Helper function for handling all structure types.
    Called for all structs, requests, replies, events, errors.
    '''
    _h_setlevel(0)
    _h('')
    _h('/**')
    _h(' * @brief %s', self.c_type)
    _h(' **/')
    _h('typedef %s %s {', self.c_container, self.c_type)

    struct_fields = []
    maxtypelen = 0

    varfield = None
    for field in self.fields:
        if not field.type.fixed_size() and not self.is_switch and not self.is_union:
            varfield = field.c_field_name
            continue
        if field.wire:
            struct_fields.append(field)

    for field in struct_fields:
        length = len(field.c_field_type)
        # account for '*' pointer_spec
        if not field.type.fixed_size() and not self.is_union:
            length += 1
        maxtypelen = max(maxtypelen, length)

    def _c_complex_field(self, field, space=''):
        if (field.type.fixed_size() or self.is_union or
            # in case of switch with switch children, don't make the field a pointer
            # necessary for unserialize to work
            (self.is_switch and field.type.is_switch)):
            spacing = ' ' * (maxtypelen - len(field.c_field_type))
            _h('%s    %s%s %s%s; /**<  */', space, field.c_field_type, spacing, field.c_field_name, field.c_subscript)
        else:
            spacing = ' ' * (maxtypelen - (len(field.c_field_type) + 1))
            _h('%s    %s%s *%s%s; /**<  */', space, field.c_field_type, spacing, field.c_field_name, field.c_subscript)

    if not self.is_switch:
        for field in struct_fields:
            _c_complex_field(self, field)
    else:
        for b in self.bitcases:
            space = ''
            if b.type.has_name:
                _h('    struct _%s {', b.c_field_name)
                space = '    '
            for field in b.type.fields:
                _c_complex_field(self, field, space)
            if b.type.has_name:
                _h('    } %s;', b.c_field_name)

    _h('} %s%s;', 'XCB_PACKED ' if force_packed else '', self.c_type)

def c_struct(self, name):
    '''
    Exported function that handles structure declarations.
    '''
    _c_type_setup(self, name, ())
    _c_complex(self)
    _c_accessors(self, name, name)
    _c_iterator(self, name)

def c_union(self, name):
    '''
    Exported function that handles union declarations.
    '''
    _c_type_setup(self, name, ())
    _c_complex(self)
    _c_iterator(self, name)

def _c_request_helper(self, name, cookie_type, void, regular, aux=False, reply_fds=False):
    '''
    Declares a request function.
    '''

    # Four stunningly confusing possibilities here:
    #
    #   Void            Non-void
    # ------------------------------
    # "req"            "req"
    # 0 flag           CHECKED flag   Normal Mode
    # void_cookie      req_cookie
    # ------------------------------
    # "req_checked"    "req_unchecked"
    # CHECKED flag     0 flag         Abnormal Mode
    # void_cookie      req_cookie
    # ------------------------------


    # Whether we are _checked or _unchecked
    checked = void and not regular
    unchecked = not void and not regular

    # What kind of cookie we return
    func_cookie = 'xcb_void_cookie_t' if void else self.c_cookie_type

    # What flag is passed to xcb_request
    func_flags = '0' if (void and regular) or (not void and not regular) else 'XCB_REQUEST_CHECKED'

    if reply_fds:
        if func_flags == '0':
            func_flags = 'XCB_REQUEST_REPLY_FDS'
        else:
            func_flags = func_flags + '|XCB_REQUEST_REPLY_FDS'

    # Global extension id variable or NULL for xproto
    func_ext_global = '&' + _ns.c_ext_global_name if _ns.is_ext else '0'

    # What our function name is
    func_name = self.c_request_name if not aux else self.c_aux_name
    if checked:
        func_name = self.c_checked_name if not aux else self.c_aux_checked_name
    if unchecked:
        func_name = self.c_unchecked_name if not aux else self.c_aux_unchecked_name

    param_fields = []
    wire_fields = []
    maxtypelen = len('xcb_connection_t')
    serial_fields = []
    # special case: list with variable size elements
    list_with_var_size_elems = False

    for field in self.fields:
        if field.visible:
            # The field should appear as a call parameter
            param_fields.append(field)
        if field.wire and not field.auto:
            # We need to set the field up in the structure
            wire_fields.append(field)
        if field.type.c_need_serialize or field.type.c_need_sizeof:
            serial_fields.append(field)

    for field in param_fields:
        c_field_const_type = field.c_field_const_type
        if field.type.c_need_serialize and not aux:
            c_field_const_type = "const void"
        if len(c_field_const_type) > maxtypelen:
            maxtypelen = len(c_field_const_type)
        if field.type.is_list and not field.type.member.fixed_size():
            list_with_var_size_elems = True

    _h_setlevel(1)
    _c_setlevel(1)
    _h('')
    _h('/**')
    if hasattr(self, "doc") and self.doc:
        if self.doc.brief:
            _h(' * @brief ' + self.doc.brief)
        else:
            _h(' * No brief doc yet')

    _h(' *')
    _h(' * @param c The connection')
    param_names = [f.c_field_name for f in param_fields]
    if hasattr(self, "doc") and self.doc:
        for field in param_fields:
            # XXX: hard-coded until we fix xproto.xml
            base_func_name = self.c_request_name if not aux else self.c_aux_name
            if base_func_name == 'xcb_change_gc' and field.c_field_name == 'value_mask':
                field.enum = 'GC'
            elif base_func_name == 'xcb_change_window_attributes' and field.c_field_name == 'value_mask':
                field.enum = 'CW'
            elif base_func_name == 'xcb_create_window' and field.c_field_name == 'value_mask':
                field.enum = 'CW'
            if field.enum:
                # XXX: why the 'xcb' prefix?
                key = ('xcb', field.enum)

                tname = _t(key)
                if namecount[tname] > 1:
                    tname = _t(key + ('enum',))
                _h(' * @param %s A bitmask of #%s values.' % (field.c_field_name, tname))

            if self.doc and field.field_name in self.doc.fields:
                desc = self.doc.fields[field.field_name]
                for name in param_names:
                    desc = desc.replace('`%s`' % name, '\\a %s' % (name))
                desc = desc.split("\n")
                desc = [line if line != '' else '\\n' for line in desc]
                _h(' * @param %s %s' % (field.c_field_name, "\n * ".join(desc)))
            # If there is no documentation yet, we simply don't generate an
            # @param tag. Doxygen will then warn about missing documentation.

    _h(' * @return A cookie')
    _h(' *')

    if hasattr(self, "doc") and self.doc:
        if self.doc.description:
            desc = self.doc.description
            for name in param_names:
                desc = desc.replace('`%s`' % name, '\\a %s' % (name))
            desc = desc.split("\n")
            _h(' * ' + "\n * ".join(desc))
        else:
            _h(' * No description yet')
    else:
        _h(' * Delivers a request to the X server.')
    _h(' *')
    if checked:
        _h(' * This form can be used only if the request will not cause')
        _h(' * a reply to be generated. Any returned error will be')
        _h(' * saved for handling by xcb_request_check().')
    if unchecked:
        _h(' * This form can be used only if the request will cause')
        _h(' * a reply to be generated. Any returned error will be')
        _h(' * placed in the event queue.')
    _h(' */')
    _c('')
    _hc('%s', cookie_type)

    spacing = ' ' * (maxtypelen - len('xcb_connection_t'))
    comma = ',' if len(param_fields) else ');'
    _h('%s (xcb_connection_t%s *c  /**< */%s', func_name, spacing, comma)
    comma = ',' if len(param_fields) else ')'
    _c('%s (xcb_connection_t%s *c  /**< */%s', func_name, spacing, comma)

    func_spacing = ' ' * (len(func_name) + 2)
    count = len(param_fields)
    for field in param_fields:
        count = count - 1
        c_field_const_type = field.c_field_const_type
        c_pointer = field.c_pointer
        if field.type.c_need_serialize and not aux:
            c_field_const_type = "const void"
            c_pointer = '*'
        spacing = ' ' * (maxtypelen - len(c_field_const_type))
        comma = ',' if count else ');'
        _h('%s%s%s %s%s  /**< */%s', func_spacing, c_field_const_type,
           spacing, c_pointer, field.c_field_name, comma)
        comma = ',' if count else ')'
        _c('%s%s%s %s%s  /**< */%s', func_spacing, c_field_const_type,
           spacing, c_pointer, field.c_field_name, comma)

    count = 2
    if not self.c_var_followed_by_fixed_fields:
        for field in param_fields:
            if not field.type.fixed_size():
                count = count + 2
                if field.type.c_need_serialize:
                    # _serialize() keeps track of padding automatically
                    count -= 1
    dimension = count + 2

    _c('{')
    _c('    static const xcb_protocol_request_t xcb_req = {')
    _c('        /* count */ %d,', count)
    _c('        /* ext */ %s,', func_ext_global)
    _c('        /* opcode */ %s,', self.c_request_name.upper())
    _c('        /* isvoid */ %d', 1 if void else 0)
    _c('    };')
    _c('')

    _c('    struct iovec xcb_parts[%d];', dimension)
    _c('    %s xcb_ret;', func_cookie)
    _c('    %s xcb_out;', self.c_type)
    if self.c_var_followed_by_fixed_fields:
        _c('    /* in the protocol description, variable size fields are followed by fixed size fields */')
        _c('    void *xcb_aux = 0;')


    for idx, f in enumerate(serial_fields):
        if aux:
            _c('    void *xcb_aux%d = 0;' % (idx))
    if list_with_var_size_elems:
        _c('    unsigned int i;')
        _c('    unsigned int xcb_tmp_len;')
        _c('    char *xcb_tmp;')
    _c('')
    # simple request call tracing
#    _c('    printf("in function %s\\n");' % func_name)

    # fixed size fields
    for field in wire_fields:
        if field.type.fixed_size():
            if field.type.is_expr:
                _c('    xcb_out.%s = %s;', field.c_field_name, _c_accessor_get_expr(field.type.expr, None))
            elif field.type.is_pad:
                if field.type.nmemb == 1:
                    _c('    xcb_out.%s = 0;', field.c_field_name)
                else:
                    _c('    memset(xcb_out.%s, 0, %d);', field.c_field_name, field.type.nmemb)
            else:
                if field.type.nmemb == 1:
                    _c('    xcb_out.%s = %s;', field.c_field_name, field.c_field_name)
                else:
                    _c('    memcpy(xcb_out.%s, %s, %d);', field.c_field_name, field.c_field_name, field.type.nmemb)

    def get_serialize_args(type_obj, c_field_name, aux_var, context='serialize'):
        serialize_args = get_serialize_params(context, type_obj,
                                              c_field_name,
                                              aux_var)[2]
        return reduce(lambda x,y: "%s, %s" % (x,y), [a[2] for a in serialize_args])

    # calls in order to free dyn. all. memory
    free_calls = []

    _c('')
    if not self.c_var_followed_by_fixed_fields:
        _c('    xcb_parts[2].iov_base = (char *) &xcb_out;')
        _c('    xcb_parts[2].iov_len = sizeof(xcb_out);')
        _c('    xcb_parts[3].iov_base = 0;')
        _c('    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;')

        count = 4

        for field in param_fields:
            if not field.type.fixed_size():
                _c('    /* %s %s */', field.type.c_type, field.c_field_name)
                # default: simple cast to char *
                if not field.type.c_need_serialize and not field.type.c_need_sizeof:
                    _c('    xcb_parts[%d].iov_base = (char *) %s;', count, field.c_field_name)
                    if field.type.is_list:
                        if field.type.member.fixed_size():
                            _c('    xcb_parts[%d].iov_len = %s * sizeof(%s);', count,
                               _c_accessor_get_expr(field.type.expr, None),
                               field.type.member.c_wiretype)
                        else:
                            list_length = _c_accessor_get_expr(field.type.expr, None)

                            length = ''
                            _c("    xcb_parts[%d].iov_len = 0;" % count)
                            _c("    xcb_tmp = (char *)%s;", field.c_field_name)
                            _c("    for(i=0; i<%s; i++) {" % list_length)
                            _c("        xcb_tmp_len = %s(xcb_tmp);" %
                                              (field.type.c_sizeof_name))
                            _c("        xcb_parts[%d].iov_len += xcb_tmp_len;" % count)
                            _c("        xcb_tmp += xcb_tmp_len;")
                            _c("    }")
                    else:
                        # not supposed to happen
                        raise Exception("unhandled variable size field %s" % field.c_field_name)
                else:
                    if not aux:
                        _c('    xcb_parts[%d].iov_base = (char *) %s;', count, field.c_field_name)
                    idx = serial_fields.index(field)
                    aux_var = '&xcb_aux%d' % idx
                    context = 'serialize' if aux else 'sizeof'
                    _c('    xcb_parts[%d].iov_len =', count)
                    if aux:
                        serialize_args = get_serialize_args(field.type, aux_var, field.c_field_name, context)
                        _c('      %s (%s);', field.type.c_serialize_name, serialize_args)
                        _c('    xcb_parts[%d].iov_base = xcb_aux%d;' % (count, idx))
                        free_calls.append('    free(xcb_aux%d);' % idx)
                    else:
                        serialize_args = get_serialize_args(field.type, field.c_field_name, aux_var, context)
                        func_name = field.type.c_sizeof_name
                        _c('      %s (%s);', func_name, serialize_args)

                count += 1
                if not (field.type.c_need_serialize or field.type.c_need_sizeof):
                    # the _serialize() function keeps track of padding automatically
                    _c('    xcb_parts[%d].iov_base = 0;', count)
                    _c('    xcb_parts[%d].iov_len = -xcb_parts[%d].iov_len & 3;', count, count-1)
                    count += 1

    # elif self.c_var_followed_by_fixed_fields:
    else:
        _c('    xcb_parts[2].iov_base = (char *) &xcb_out;')
        # request header: opcodes + length
        _c('    xcb_parts[2].iov_len = 2*sizeof(uint8_t) + sizeof(uint16_t);')
        count += 1
        # call _serialize()
        buffer_var = '&xcb_aux'
        serialize_args = get_serialize_args(self, buffer_var, '&xcb_out', 'serialize')
        _c('    xcb_parts[%d].iov_len = %s (%s);', count, self.c_serialize_name, serialize_args)
        _c('    xcb_parts[%d].iov_base = (char *) xcb_aux;', count)
        free_calls.append('    free(xcb_aux);')
        # no padding necessary - _serialize() keeps track of padding automatically

    _c('')
    for field in param_fields:
        if field.isfd:
            _c('    xcb_send_fd(c, %s);', field.c_field_name)

    _c('    xcb_ret.sequence = xcb_send_request(c, %s, xcb_parts + 2, &xcb_req);', func_flags)

    # free dyn. all. data, if any
    for f in free_calls:
        _c(f)
    _c('    return xcb_ret;')
    _c('}')

def _c_reply(self, name):
    '''
    Declares the function that returns the reply structure.
    '''
    spacing1 = ' ' * (len(self.c_cookie_type) - len('xcb_connection_t'))
    spacing2 = ' ' * (len(self.c_cookie_type) - len('xcb_generic_error_t'))
    spacing3 = ' ' * (len(self.c_reply_name) + 2)

    # check if _unserialize() has to be called for any field
    def look_for_special_cases(complex_obj):
        unserialize_fields = []
        # no unserialize call in case of switch
        if not complex_obj.is_switch:
            for field in complex_obj.fields:
                # three cases: 1. field with special case
                #              2. container that contains special case field
                #              3. list with special case elements
                if field.type.c_var_followed_by_fixed_fields:
                    unserialize_fields.append(field)
                elif field.type.is_container:
                    unserialize_fields += look_for_special_cases(field.type)
                elif field.type.is_list:
                    if field.type.member.c_var_followed_by_fixed_fields:
                        unserialize_fields.append(field)
                    if field.type.member.is_container:
                        unserialize_fields += look_for_special_cases(field.type.member)
        return unserialize_fields

    unserialize_fields = look_for_special_cases(self.reply)

    _h('')
    _h('/**')
    _h(' * Return the reply')
    _h(' * @param c      The connection')
    _h(' * @param cookie The cookie')
    _h(' * @param e      The xcb_generic_error_t supplied')
    _h(' *')
    _h(' * Returns the reply of the request asked by')
    _h(' *')
    _h(' * The parameter @p e supplied to this function must be NULL if')
    _h(' * %s(). is used.', self.c_unchecked_name)
    _h(' * Otherwise, it stores the error if any.')
    _h(' *')
    _h(' * The returned value must be freed by the caller using free().')
    _h(' */')
    _c('')
    _hc('%s *', self.c_reply_type)
    _hc('%s (xcb_connection_t%s  *c  /**< */,', self.c_reply_name, spacing1)
    _hc('%s%s   cookie  /**< */,', spacing3, self.c_cookie_type)
    _h('%sxcb_generic_error_t%s **e  /**< */);', spacing3, spacing2)
    _c('%sxcb_generic_error_t%s **e  /**< */)', spacing3, spacing2)
    _c('{')

    if len(unserialize_fields)>0:
        # certain variable size fields need to be unserialized explicitly
        _c('    %s *reply = (%s *) xcb_wait_for_reply(c, cookie.sequence, e);',
           self.c_reply_type, self.c_reply_type)
        _c('    int i;')
        for field in unserialize_fields:
            if field.type.is_list:
                _c('    %s %s_iter = %s(reply);', field.c_iterator_type, field.c_field_name, field.c_iterator_name)
                _c('    int %s_len = %s(reply);', field.c_field_name, field.c_length_name)
                _c('    %s *%s_data;', field.c_field_type, field.c_field_name)
            else:
                raise Exception('not implemented: call _unserialize() in reply for non-list type %s', field.c_field_type)
        # call _unserialize(), using the reply as source and target buffer
        _c('    /* special cases: transform parts of the reply to match XCB data structures */')
        for field in unserialize_fields:
            if field.type.is_list:
                _c('    for(i=0; i<%s_len; i++) {', field.c_field_name)
                _c('        %s_data = %s_iter.data;', field.c_field_name, field.c_field_name)
                _c('        %s((const void *)%s_data, &%s_data);', field.type.c_unserialize_name,
                   field.c_field_name, field.c_field_name)
                _c('        %s(&%s_iter);', field.type.c_next_name, field.c_field_name)
                _c('    }')
        # return the transformed reply
        _c('    return reply;')

    else:
        _c('    return (%s *) xcb_wait_for_reply(c, cookie.sequence, e);', self.c_reply_type)

    _c('}')

def _c_reply_has_fds(self):
    for field in self.fields:
        if field.isfd:
            return True
    return False

def _c_reply_fds(self, name):
    '''
    Declares the function that returns fds related to the reply.
    '''
    spacing1 = ' ' * (len(self.c_reply_type) - len('xcb_connection_t'))
    spacing3 = ' ' * (len(self.c_reply_fds_name) + 2)
    _h('')
    _h('/**')
    _h(' * Return the reply fds')
    _h(' * @param c      The connection')
    _h(' * @param reply  The reply')
    _h(' *')
    _h(' * Returns the array of reply fds of the request asked by')
    _h(' *')
    _h(' * The returned value must be freed by the caller using free().')
    _h(' */')
    _c('')
    _hc('int *')
    _hc('%s (xcb_connection_t%s  *c  /**< */,', self.c_reply_fds_name, spacing1)
    _h('%s%s  *reply  /**< */);', spacing3, self.c_reply_type)
    _c('%s%s  *reply  /**< */)', spacing3, self.c_reply_type)
    _c('{')

    _c('    return xcb_get_reply_fds(c, reply, sizeof(%s) + 4 * reply->length);', self.c_reply_type)

    _c('}')


def _c_opcode(name, opcode):
    '''
    Declares the opcode define for requests, events, and errors.
    '''
    _h_setlevel(0)
    _h('')
    _h('/** Opcode for %s. */', _n(name))
    _h('#define %s %s', _n(name).upper(), opcode)

def _c_cookie(self, name):
    '''
    Declares the cookie type for a non-void request.
    '''
    _h_setlevel(0)
    _h('')
    _h('/**')
    _h(' * @brief %s', self.c_cookie_type)
    _h(' **/')
    _h('typedef struct %s {', self.c_cookie_type)
    _h('    unsigned int sequence; /**<  */')
    _h('} %s;', self.c_cookie_type)

def _man_request(self, name, cookie_type, void, aux):
    param_fields = [f for f in self.fields if f.visible]

    func_name = self.c_request_name if not aux else self.c_aux_name

    def create_link(linkname):
        name = 'man/%s.%s' % (linkname, section)
        if manpaths:
            sys.stdout.write(name)
        f = open(name, 'w')
        f.write('.so man%s/%s.%s' % (section, func_name, section))
        f.close()

    if manpaths:
        sys.stdout.write('man/%s.%s ' % (func_name, section))
    # Our CWD is src/, so this will end up in src/man/
    f = open('man/%s.%s' % (func_name, section), 'w')
    f.write('.TH %s %s  "%s" "%s" "XCB Requests"\n' % (func_name, section, center_footer, left_footer))
    # Left-adjust instead of adjusting to both sides
    f.write('.ad l\n')
    f.write('.SH NAME\n')
    brief = self.doc.brief if hasattr(self, "doc") and self.doc else ''
    f.write('%s \\- %s\n' % (func_name, brief))
    f.write('.SH SYNOPSIS\n')
    # Don't split words (hyphenate)
    f.write('.hy 0\n')
    f.write('.B #include <xcb/%s.h>\n' % _ns.header)

    # function prototypes
    prototype = ''
    count = len(param_fields)
    for field in param_fields:
        count = count - 1
        c_field_const_type = field.c_field_const_type
        c_pointer = field.c_pointer
        if c_pointer == ' ':
            c_pointer = ''
        if field.type.c_need_serialize and not aux:
            c_field_const_type = "const void"
            c_pointer = '*'
        comma = ', ' if count else ');'
        prototype += '%s\\ %s\\fI%s\\fP%s' % (c_field_const_type, c_pointer, field.c_field_name, comma)

    f.write('.SS Request function\n')
    f.write('.HP\n')
    base_func_name = self.c_request_name if not aux else self.c_aux_name
    f.write('%s \\fB%s\\fP(xcb_connection_t\\ *\\fIconn\\fP, %s\n' % (cookie_type, base_func_name, prototype))
    create_link('%s_%s' % (base_func_name, ('checked' if void else 'unchecked')))
    if not void:
        f.write('.PP\n')
        f.write('.SS Reply datastructure\n')
        f.write('.nf\n')
        f.write('.sp\n')
        f.write('typedef %s %s {\n' % (self.reply.c_container, self.reply.c_type))
        struct_fields = []
        maxtypelen = 0

        for field in self.reply.fields:
            if not field.type.fixed_size() and not self.is_switch and not self.is_union:
                continue
            if field.wire:
                struct_fields.append(field)

        for field in struct_fields:
            length = len(field.c_field_type)
            # account for '*' pointer_spec
            if not field.type.fixed_size():
                length += 1
            maxtypelen = max(maxtypelen, length)

        def _c_complex_field(self, field, space=''):
            if (field.type.fixed_size() or
                # in case of switch with switch children, don't make the field a pointer
                # necessary for unserialize to work
                (self.is_switch and field.type.is_switch)):
                spacing = ' ' * (maxtypelen - len(field.c_field_type))
                f.write('%s    %s%s \\fI%s\\fP%s;\n' % (space, field.c_field_type, spacing, field.c_field_name, field.c_subscript))
            else:
                spacing = ' ' * (maxtypelen - (len(field.c_field_type) + 1))
                f.write('ELSE %s = %s\n' % (field.c_field_type, field.c_field_name))
                #_h('%s    %s%s *%s%s; /**<  */', space, field.c_field_type, spacing, field.c_field_name, field.c_subscript)

        if not self.is_switch:
            for field in struct_fields:
                _c_complex_field(self, field)
        else:
            for b in self.bitcases:
                space = ''
                if b.type.has_name:
                    space = '    '
                for field in b.type.fields:
                    _c_complex_field(self, field, space)
                if b.type.has_name:
                    print >> sys.stderr, 'ERROR: New unhandled documentation case'
                    pass

        f.write('} \\fB%s\\fP;\n' % self.reply.c_type)
        f.write('.fi\n')

        f.write('.SS Reply function\n')
        f.write('.HP\n')
        f.write(('%s *\\fB%s\\fP(xcb_connection_t\\ *\\fIconn\\fP, %s\\ '
                 '\\fIcookie\\fP, xcb_generic_error_t\\ **\\fIe\\fP);\n') %
                (self.c_reply_type, self.c_reply_name, self.c_cookie_type))
        create_link('%s' % self.c_reply_name)

        has_accessors = False
        for field in self.reply.fields:
            if field.type.is_list and not field.type.fixed_size():
                has_accessors = True
            elif field.prev_varsized_field is not None or not field.type.fixed_size():
                has_accessors = True

        if has_accessors:
            f.write('.SS Reply accessors\n')

        def _c_accessors_field(self, field):
            '''
            Declares the accessor functions for a non-list field that follows a variable-length field.
            '''
            c_type = self.c_type

            # special case: switch
            switch_obj = self if self.is_switch else None
            if self.is_bitcase:
                switch_obj = self.parents[-1]
            if switch_obj is not None:
                c_type = switch_obj.c_type

            if field.type.is_simple:
                f.write('%s %s (const %s *reply)\n' % (field.c_field_type, field.c_accessor_name, c_type))
                create_link('%s' % field.c_accessor_name)
            else:
                f.write('%s *%s (const %s *reply)\n' % (field.c_field_type, field.c_accessor_name, c_type))
                create_link('%s' % field.c_accessor_name)

        def _c_accessors_list(self, field):
            '''
            Declares the accessor functions for a list field.
            Declares a direct-accessor function only if the list members are fixed size.
            Declares length and get-iterator functions always.
            '''
            list = field.type
            c_type = self.reply.c_type

            # special case: switch
            # in case of switch, 2 params have to be supplied to certain accessor functions:
            #   1. the anchestor object (request or reply)
            #   2. the (anchestor) switch object
            # the reason is that switch is either a child of a request/reply or nested in another switch,
            # so whenever we need to access a length field, we might need to refer to some anchestor type
            switch_obj = self if self.is_switch else None
            if self.is_bitcase:
                switch_obj = self.parents[-1]
            if switch_obj is not None:
                c_type = switch_obj.c_type

            params = []
            fields = {}
            parents = self.parents if hasattr(self, 'parents') else [self]
            # 'R': parents[0] is always the 'toplevel' container type
            params.append(('const %s *\\fIreply\\fP' % parents[0].c_type, parents[0]))
            fields.update(_c_helper_field_mapping(parents[0], [('R', '->', parents[0])], flat=True))
            # auxiliary object for 'R' parameters
            R_obj = parents[0]

            if switch_obj is not None:
                # now look where the fields are defined that are needed to evaluate
                # the switch expr, and store the parent objects in accessor_params and
                # the fields in switch_fields

                # 'S': name for the 'toplevel' switch
                toplevel_switch = parents[1]
                params.append(('const %s *S' % toplevel_switch.c_type, toplevel_switch))
                fields.update(_c_helper_field_mapping(toplevel_switch, [('S', '->', toplevel_switch)], flat=True))

                # initialize prefix for everything "below" S
                prefix_str = '/* %s */ S' % toplevel_switch.name[-1]
                prefix = [(prefix_str, '->', toplevel_switch)]

                # look for fields in the remaining containers
                for p in parents[2:] + [self]:
                    # the separator between parent and child is always '.' here,
                    # because of nested switch statements
                    if not p.is_bitcase or (p.is_bitcase and p.has_name):
                        prefix.append((p.name[-1], '.', p))
                    fields.update(_c_helper_field_mapping(p, prefix, flat=True))

                # auxiliary object for 'S' parameter
                S_obj = parents[1]

            if list.member.fixed_size():
                idx = 1 if switch_obj is not None else 0
                f.write('.HP\n')
                f.write('%s *\\fB%s\\fP(%s);\n' %
                        (field.c_field_type, field.c_accessor_name, params[idx][0]))
                create_link('%s' % field.c_accessor_name)

            f.write('.HP\n')
            f.write('int \\fB%s\\fP(const %s *\\fIreply\\fP);\n' %
                    (field.c_length_name, c_type))
            create_link('%s' % field.c_length_name)

            if field.type.member.is_simple:
                f.write('.HP\n')
                f.write('xcb_generic_iterator_t \\fB%s\\fP(const %s *\\fIreply\\fP);\n' %
                        (field.c_end_name, c_type))
                create_link('%s' % field.c_end_name)
            else:
                f.write('.HP\n')
                f.write('%s \\fB%s\\fP(const %s *\\fIreply\\fP);\n' %
                        (field.c_iterator_type, field.c_iterator_name,
                         c_type))
                create_link('%s' % field.c_iterator_name)

        for field in self.reply.fields:
            if field.type.is_list and not field.type.fixed_size():
                _c_accessors_list(self, field)
            elif field.prev_varsized_field is not None or not field.type.fixed_size():
                _c_accessors_field(self, field)


    f.write('.br\n')
    # Re-enable hyphenation and adjusting to both sides
    f.write('.hy 1\n')

    # argument reference
    f.write('.SH REQUEST ARGUMENTS\n')
    f.write('.IP \\fI%s\\fP 1i\n' % 'conn')
    f.write('The XCB connection to X11.\n')
    for field in param_fields:
        f.write('.IP \\fI%s\\fP 1i\n' % (field.c_field_name))
        printed_enum = False
        # XXX: hard-coded until we fix xproto.xml
        if base_func_name == 'xcb_change_gc' and field.c_field_name == 'value_mask':
            field.enum = 'GC'
        elif base_func_name == 'xcb_change_window_attributes' and field.c_field_name == 'value_mask':
            field.enum = 'CW'
        elif base_func_name == 'xcb_create_window' and field.c_field_name == 'value_mask':
            field.enum = 'CW'
        if hasattr(field, "enum") and field.enum:
            # XXX: why the 'xcb' prefix?
            key = ('xcb', field.enum)
            if key in enums:
                f.write('One of the following values:\n')
                f.write('.RS 1i\n')
                enum = enums[key]
                count = len(enum.values)
                for (enam, eval) in enum.values:
                    count = count - 1
                    f.write('.IP \\fI%s\\fP 1i\n' % (_n(key + (enam,)).upper()))
                    if hasattr(enum, "doc") and enum.doc and enam in enum.doc.fields:
                        desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', enum.doc.fields[enam])
                        f.write('%s\n' % desc)
                    else:
                        f.write('TODO: NOT YET DOCUMENTED.\n')
                f.write('.RE\n')
                f.write('.RS 1i\n')
                printed_enum = True

        if hasattr(self, "doc") and self.doc and field.field_name in self.doc.fields:
            desc = self.doc.fields[field.field_name]
            desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', desc)
            if printed_enum:
                f.write('\n')
            f.write('%s\n' % desc)
        else:
            f.write('TODO: NOT YET DOCUMENTED.\n')
        if printed_enum:
            f.write('.RE\n')

    # Reply reference
    if not void:
        f.write('.SH REPLY FIELDS\n')
        # These fields are present in every reply:
        f.write('.IP \\fI%s\\fP 1i\n' % 'response_type')
        f.write(('The type of this reply, in this case \\fI%s\\fP. This field '
                 'is also present in the \\fIxcb_generic_reply_t\\fP and can '
                 'be used to tell replies apart from each other.\n') %
                 _n(self.reply.name).upper())
        f.write('.IP \\fI%s\\fP 1i\n' % 'sequence')
        f.write('The sequence number of the last request processed by the X11 server.\n')
        f.write('.IP \\fI%s\\fP 1i\n' % 'length')
        f.write('The length of the reply, in words (a word is 4 bytes).\n')
        for field in self.reply.fields:
            if (field.c_field_name in frozenset(['response_type', 'sequence', 'length']) or
                field.c_field_name.startswith('pad')):
                continue

            if field.type.is_list and not field.type.fixed_size():
                continue
            elif field.prev_varsized_field is not None or not field.type.fixed_size():
                continue
            f.write('.IP \\fI%s\\fP 1i\n' % (field.c_field_name))
            printed_enum = False
            if hasattr(field, "enum") and field.enum:
                # XXX: why the 'xcb' prefix?
                key = ('xcb', field.enum)
                if key in enums:
                    f.write('One of the following values:\n')
                    f.write('.RS 1i\n')
                    enum = enums[key]
                    count = len(enum.values)
                    for (enam, eval) in enum.values:
                        count = count - 1
                        f.write('.IP \\fI%s\\fP 1i\n' % (_n(key + (enam,)).upper()))
                        if enum.doc and enam in enum.doc.fields:
                            desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', enum.doc.fields[enam])
                            f.write('%s\n' % desc)
                        else:
                            f.write('TODO: NOT YET DOCUMENTED.\n')
                    f.write('.RE\n')
                    f.write('.RS 1i\n')
                    printed_enum = True

            if hasattr(self.reply, "doc") and self.reply.doc and field.field_name in self.reply.doc.fields:
                desc = self.reply.doc.fields[field.field_name]
                desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', desc)
                if printed_enum:
                    f.write('\n')
                f.write('%s\n' % desc)
            else:
                f.write('TODO: NOT YET DOCUMENTED.\n')
            if printed_enum:
                f.write('.RE\n')



    # text description
    f.write('.SH DESCRIPTION\n')
    if hasattr(self, "doc") and self.doc and self.doc.description:
        desc = self.doc.description
        desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', desc)
        lines = desc.split('\n')
        f.write('\n'.join(lines) + '\n')

    f.write('.SH RETURN VALUE\n')
    if void:
        f.write(('Returns an \\fIxcb_void_cookie_t\\fP. Errors (if any) '
                 'have to be handled in the event loop.\n\nIf you want to '
                 'handle errors directly with \\fIxcb_request_check\\fP '
                 'instead, use \\fI%s_checked\\fP. See '
                 '\\fBxcb-requests(%s)\\fP for details.\n') % (base_func_name, section))
    else:
        f.write(('Returns an \\fI%s\\fP. Errors have to be handled when '
                 'calling the reply function \\fI%s\\fP.\n\nIf you want to '
                 'handle errors in the event loop instead, use '
                 '\\fI%s_unchecked\\fP. See \\fBxcb-requests(%s)\\fP for '
                 'details.\n') %
                (cookie_type, self.c_reply_name, base_func_name, section))
    f.write('.SH ERRORS\n')
    if hasattr(self, "doc") and self.doc:
        for errtype, errtext in sorted(self.doc.errors.items()):
            f.write('.IP \\fI%s\\fP 1i\n' % (_t(('xcb', errtype, 'error'))))
            errtext = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', errtext)
            f.write('%s\n' % (errtext))
    if not hasattr(self, "doc") or not self.doc or len(self.doc.errors) == 0:
        f.write('This request does never generate any errors.\n')
    if hasattr(self, "doc") and self.doc and self.doc.example:
        f.write('.SH EXAMPLE\n')
        f.write('.nf\n')
        f.write('.sp\n')
        lines = self.doc.example.split('\n')
        f.write('\n'.join(lines) + '\n')
        f.write('.fi\n')
    f.write('.SH SEE ALSO\n')
    if hasattr(self, "doc") and self.doc:
        see = ['.BR %s (%s)' % ('xcb-requests', section)]
        if self.doc.example:
            see.append('.BR %s (%s)' % ('xcb-examples', section))
        for seename, seetype in sorted(self.doc.see.items()):
            if seetype == 'program':
                see.append('.BR %s (1)' % seename)
            elif seetype == 'event':
                see.append('.BR %s (%s)' % (_t(('xcb', seename, 'event')), section))
            elif seetype == 'request':
                see.append('.BR %s (%s)' % (_n(('xcb', seename)), section))
            elif seetype == 'function':
                see.append('.BR %s (%s)' % (seename, section))
            else:
                see.append('TODO: %s (type %s)' % (seename, seetype))
        f.write(',\n'.join(see) + '\n')
    f.write('.SH AUTHOR\n')
    f.write('Generated from %s.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.\n' % _ns.header)
    f.close()

def _man_event(self, name):
    if manpaths:
        sys.stdout.write('man/%s.%s ' % (self.c_type, section))
    # Our CWD is src/, so this will end up in src/man/
    f = open('man/%s.%s' % (self.c_type, section), 'w')
    f.write('.TH %s %s  "%s" "%s" "XCB Events"\n' % (self.c_type, section, center_footer, left_footer))
    # Left-adjust instead of adjusting to both sides
    f.write('.ad l\n')
    f.write('.SH NAME\n')
    brief = self.doc.brief if hasattr(self, "doc") and self.doc else ''
    f.write('%s \\- %s\n' % (self.c_type, brief))
    f.write('.SH SYNOPSIS\n')
    # Don't split words (hyphenate)
    f.write('.hy 0\n')
    f.write('.B #include <xcb/%s.h>\n' % _ns.header)

    f.write('.PP\n')
    f.write('.SS Event datastructure\n')
    f.write('.nf\n')
    f.write('.sp\n')
    f.write('typedef %s %s {\n' % (self.c_container, self.c_type))
    struct_fields = []
    maxtypelen = 0

    for field in self.fields:
        if not field.type.fixed_size() and not self.is_switch and not self.is_union:
            continue
        if field.wire:
            struct_fields.append(field)

    for field in struct_fields:
        length = len(field.c_field_type)
        # account for '*' pointer_spec
        if not field.type.fixed_size():
            length += 1
        maxtypelen = max(maxtypelen, length)

    def _c_complex_field(self, field, space=''):
        if (field.type.fixed_size() or
            # in case of switch with switch children, don't make the field a pointer
            # necessary for unserialize to work
            (self.is_switch and field.type.is_switch)):
            spacing = ' ' * (maxtypelen - len(field.c_field_type))
            f.write('%s    %s%s \\fI%s\\fP%s;\n' % (space, field.c_field_type, spacing, field.c_field_name, field.c_subscript))
        else:
            print >> sys.stderr, 'ERROR: New unhandled documentation case'

    if not self.is_switch:
        for field in struct_fields:
            _c_complex_field(self, field)
    else:
        for b in self.bitcases:
            space = ''
            if b.type.has_name:
                space = '    '
            for field in b.type.fields:
                _c_complex_field(self, field, space)
            if b.type.has_name:
                print >> sys.stderr, 'ERROR: New unhandled documentation case'
                pass

    f.write('} \\fB%s\\fP;\n' % self.c_type)
    f.write('.fi\n')


    f.write('.br\n')
    # Re-enable hyphenation and adjusting to both sides
    f.write('.hy 1\n')

    # argument reference
    f.write('.SH EVENT FIELDS\n')
    f.write('.IP \\fI%s\\fP 1i\n' % 'response_type')
    f.write(('The type of this event, in this case \\fI%s\\fP. This field is '
             'also present in the \\fIxcb_generic_event_t\\fP and can be used '
             'to tell events apart from each other.\n') % _n(name).upper())
    f.write('.IP \\fI%s\\fP 1i\n' % 'sequence')
    f.write('The sequence number of the last request processed by the X11 server.\n')

    if not self.is_switch:
        for field in struct_fields:
            # Skip the fields which every event has, we already documented
            # them (see above).
            if field.c_field_name in ('response_type', 'sequence'):
                continue
            if isinstance(field.type, PadType):
                continue
            f.write('.IP \\fI%s\\fP 1i\n' % (field.c_field_name))
            if hasattr(self, "doc") and self.doc and field.field_name in self.doc.fields:
                desc = self.doc.fields[field.field_name]
                desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', desc)
                f.write('%s\n' % desc)
            else:
                f.write('NOT YET DOCUMENTED.\n')

    # text description
    f.write('.SH DESCRIPTION\n')
    if hasattr(self, "doc") and self.doc and self.doc.description:
        desc = self.doc.description
        desc = re.sub(r'`([^`]+)`', r'\\fI\1\\fP', desc)
        lines = desc.split('\n')
        f.write('\n'.join(lines) + '\n')

    if hasattr(self, "doc") and self.doc and self.doc.example:
        f.write('.SH EXAMPLE\n')
        f.write('.nf\n')
        f.write('.sp\n')
        lines = self.doc.example.split('\n')
        f.write('\n'.join(lines) + '\n')
        f.write('.fi\n')
    f.write('.SH SEE ALSO\n')
    if hasattr(self, "doc") and self.doc:
        see = ['.BR %s (%s)' % ('xcb_generic_event_t', section)]
        if self.doc.example:
            see.append('.BR %s (%s)' % ('xcb-examples', section))
        for seename, seetype in sorted(self.doc.see.items()):
            if seetype == 'program':
                see.append('.BR %s (1)' % seename)
            elif seetype == 'event':
                see.append('.BR %s (%s)' % (_t(('xcb', seename, 'event')), section))
            elif seetype == 'request':
                see.append('.BR %s (%s)' % (_n(('xcb', seename)), section))
            elif seetype == 'function':
                see.append('.BR %s (%s)' % (seename, section))
            else:
                see.append('TODO: %s (type %s)' % (seename, seetype))
        f.write(',\n'.join(see) + '\n')
    f.write('.SH AUTHOR\n')
    f.write('Generated from %s.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.\n' % _ns.header)
    f.close()


def c_request(self, name):
    '''
    Exported function that handles request declarations.
    '''
    _c_type_setup(self, name, ('request',))

    if self.reply:
        # Cookie type declaration
        _c_cookie(self, name)

    # Opcode define
    _c_opcode(name, self.opcode)

    # Request structure declaration
    _c_complex(self)

    if self.reply:
        _c_type_setup(self.reply, name, ('reply',))
        # Reply structure definition
        _c_complex(self.reply)
        # Request prototypes
        has_fds = _c_reply_has_fds(self.reply)
        _c_request_helper(self, name, self.c_cookie_type, False, True, False, has_fds)
        _c_request_helper(self, name, self.c_cookie_type, False, False, False, has_fds)
        if self.c_need_aux:
            _c_request_helper(self, name, self.c_cookie_type, False, True, True, has_fds)
            _c_request_helper(self, name, self.c_cookie_type, False, False, True, has_fds)
        # Reply accessors
        _c_accessors(self.reply, name + ('reply',), name)
        _c_reply(self, name)
        if has_fds:
            _c_reply_fds(self, name)
    else:
        # Request prototypes
        _c_request_helper(self, name, 'xcb_void_cookie_t', True, False)
        _c_request_helper(self, name, 'xcb_void_cookie_t', True, True)
        if self.c_need_aux:
            _c_request_helper(self, name, 'xcb_void_cookie_t', True, False, True)
            _c_request_helper(self, name, 'xcb_void_cookie_t', True, True, True)

    # We generate the manpage afterwards because _c_type_setup has been called.
    # TODO: what about aux helpers?
    cookie_type = self.c_cookie_type if self.reply else 'xcb_void_cookie_t'
    _man_request(self, name, cookie_type, not self.reply, False)

def c_event(self, name):
    '''
    Exported function that handles event declarations.
    '''

    # The generic event structure xcb_ge_event_t has the full_sequence field
    # at the 32byte boundary. That's why we've to inject this field into GE
    # events while generating the structure for them. Otherwise we would read
    # garbage (the internal full_sequence) when accessing normal event fields
    # there.
    force_packed = False
    if hasattr(self, 'is_ge_event') and self.is_ge_event and self.name == name:
        event_size = 0
        for field in self.fields:
            if field.type.size != None and field.type.nmemb != None:
                event_size += field.type.size * field.type.nmemb
            if event_size == 32:
                full_sequence = Field(tcard32, tcard32.name, 'full_sequence', False, True, True)
                idx = self.fields.index(field)
                self.fields.insert(idx + 1, full_sequence)

                # If the event contains any 64-bit extended fields, they need
                # to remain aligned on a 64-bit boundary.  Adding full_sequence
                # would normally break that; force the struct to be packed.
                force_packed = any(f.type.size == 8 and f.type.is_simple for f in self.fields[(idx+1):])
                break

    _c_type_setup(self, name, ('event',))

    # Opcode define
    _c_opcode(name, self.opcodes[name])

    if self.name == name:
        # Structure definition
        _c_complex(self, force_packed)
    else:
        # Typedef
        _h('')
        _h('typedef %s %s;', _t(self.name + ('event',)), _t(name + ('event',)))

    _man_event(self, name)

def c_error(self, name):
    '''
    Exported function that handles error declarations.
    '''
    _c_type_setup(self, name, ('error',))

    # Opcode define
    _c_opcode(name, self.opcodes[name])

    if self.name == name:
        # Structure definition
        _c_complex(self)
    else:
        # Typedef
        _h('')
        _h('typedef %s %s;', _t(self.name + ('error',)), _t(name + ('error',)))


# Main routine starts here

# Must create an "output" dictionary before any xcbgen imports.
output = {'open'    : c_open,
          'close'   : c_close,
          'simple'  : c_simple,
          'enum'    : c_enum,
          'struct'  : c_struct,
          'union'   : c_union,
          'request' : c_request,
          'event'   : c_event,
          'error'   : c_error,
          }

# Boilerplate below this point

# Check for the argument that specifies path to the xcbgen python package.
try:
    opts, args = getopt.getopt(sys.argv[1:], 'c:l:s:p:m')
except getopt.GetoptError as err:
    print(err)
    print('Usage: c_client.py -c center_footer -l left_footer -s section [-p path] file.xml')
    sys.exit(1)

for (opt, arg) in opts:
    if opt == '-c':
        center_footer=arg
    if opt == '-l':
        left_footer=arg
    if opt == '-s':
        section=arg
    if opt == '-p':
        sys.path.insert(1, arg)
    elif opt == '-m':
        manpaths = True
        sys.stdout.write('man_MANS = ')

# Import the module class
try:
    from xcbgen.state import Module
    from xcbgen.xtypes import *
except ImportError:
    print('''
Failed to load the xcbgen Python package!
Make sure that xcb/proto installed it on your Python path.
If not, you will need to create a .pth file or define $PYTHONPATH
to extend the path.
Refer to the README file in xcb/proto for more info.
''')
    raise

# Ensure the man subdirectory exists
try:
    os.mkdir('man')
except OSError as e:
    if e.errno != errno.EEXIST:
        raise

# Parse the xml header
module = Module(args[0], output)

# Build type-registry and resolve type dependencies
module.register()
module.resolve()

# Output the code
module.generate()
