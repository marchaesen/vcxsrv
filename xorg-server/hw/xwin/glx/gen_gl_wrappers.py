#!/usr/bin/python
#
# Comedy python script to generate cdecl to stdcall wrappers for GL functions
#
# This is designed to operate on OpenGL spec files from
# http://www.opengl.org/registry/api/
#
#
# Copyright (c) Jon TURNEY 2009
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# Except as contained in this notice, the name(s) of the above copyright
# holders shall not be used in advertising or otherwise to promote the sale,
# use or other dealings in this Software without prior written authorization.
#

import sys
import re
import getopt

dispatchheader = ''
prefix = 'gl'
preresolve = False
staticwrappers = False

opts, args = getopt.getopt(sys.argv[1:], "", ['spec=', 'typemap=', 'dispatch-header=', 'prefix=', 'preresolve', 'staticwrappers' ])

for o,a in opts:
        if o == '--typemap' :
                typemapfile = a
        elif o == '--dispatch-header' :
                dispatchheader = a
        elif o == '--spec' :
                specfile = a
        elif o == '--prefix' :
                prefix = a
        elif o == '--preresolve' :
                preresolve = True
        elif o == '--staticwrappers' :
                staticwrappers = True

#
# look for all the SET_ macros in dispatch.h, this is the set of functions
# we need to generate
#

dispatch = {}

if dispatchheader :
        fh = open(dispatchheader)
        dispatchh = fh.readlines()

        dispatch_regex = re.compile(r'^SET_(\S*)\(')

        for line in dispatchh :
                line = line.strip()
                m1 = dispatch_regex.search(line)

                if m1 :
                        dispatch[m1.group(1)] = 1

        del dispatch['by_offset']

#
# read the typemap .tm file
#

typemap = {}

fh = open(typemapfile)
tm = fh.readlines()

typemap_regex = re.compile(r'#define\sSET_(\S*)\(')

for line in tm :
        # ignore everything after a '#' as a comment
        hash = line.find('#')
        if hash != -1 :
                line = line[:hash-1]

        # ignore blank lines
        if line.startswith('#') or len(line) == 0 :
                continue

        l = line.split(',')
        typemap[l[0]] = l[3].strip()

# interestingly, * is not a C type
if typemap['void'] == '*' :
        typemap['void'] = 'void'

#
# crudely parse the .spec file
#

r1 = re.compile(r'\t(\S*)\s+(\S*.*)')
r2 = re.compile(r'(.*)\((.*)\)')
r3 = re.compile(r'glWindowPos.*MESA')
r4 = re.compile(r'gl.*Program(s|)NV')
r5 = re.compile(r'glGetVertexAttribfvNV')

wrappers = {}

fh = open(specfile)
glspec = fh.readlines()
param_count = 0

for line in glspec :
        line = line.rstrip()

        # ignore everything after a '#' as a comment
        hash = line.find('#')
        if hash != -1 :
                line = line[:hash-1]

        # ignore blank lines
        if line.startswith('#') or len(line) == 0 :
                continue

        # lines containing ':' aren't intersting to us
        if line.count(':') != 0 :
                continue

        # attributes of each function follow the name, indented by a tab
        if not line.startswith('\t') :
                m1 = r2.search(line)
                if m1 :
                        function = m1.group(1)
                        arglist_use = m1.group(2)
                        wrappers[function] = {}

                        # near and far might be reserved words or macros so can't be used as formal parameter names
                        arglist_use = arglist_use.replace('near','zNear')
                        arglist_use = arglist_use.replace('far','zFar')

                        wrappers[function]['arglist_use'] = arglist_use
                        param_count = 0
        else :
                m1 = r1.search(line)
                if m1 :
                        attribute = m1.group(1)
                        value = m1.group(2)

                        # make param attributes unique and ordered
                        if attribute == 'param' :
                                attribute = 'param' + '%02d' % param_count
                                param_count += 1

                        wrappers[function][attribute] = value

#
# now emit code
#

print '/* Automatically generated by ' + sys.argv[0] + ' DO NOT EDIT */'
print '/* from ' + specfile + ' and typemap ' + typemapfile + ' */'
print ''

#
# if required, emit code for non-lazy function resolving
#

if preresolve :
        for w in sorted(wrappers.keys()) :
                funcname = prefix + w
                print 'RESOLVE_DECL(PFN' + funcname.upper() + 'PROC);'

        print ''
        print 'void ' + prefix + 'ResolveExtensionProcs(void)'
        print '{'

        for w in sorted(wrappers.keys()) :
                funcname = prefix + w
                print '  PRERESOLVE(PFN' + funcname.upper() + 'PROC, "' + funcname + '");'

        print '}\n'

#
# now emit the wrappers
# for GL 1.0 and 1.1 functions, generate stdcall wrappers which call the function directly
# for GL 1.2+ functions, generate wrappers which use wglGetProcAddress()
#

for w in sorted(wrappers.keys()) :

        funcname = prefix + w
        returntype = wrappers[w]['return']
        if returntype != 'void' :
                returntype = typemap[returntype]

        # Avoid generating wrappers which aren't referenced by the dispatch table
        if dispatchheader and not dispatch.has_key(w) :
                print '/* No wrapper for ' + funcname + ', not in dispatch table */'
                continue

        # manufacture arglist
        # if no param attributes were found, it should be 'void'
        al = []
        for k in sorted(wrappers[w].keys()) :
                if k.startswith('param') :
                        l = wrappers[w][k].split()

                        # near and far might be reserved words or macros so can't be used as formal parameter names
                        l[0] = l[0].replace('near','zNear')
                        l[0] = l[0].replace('far','zFar')

                        if l[2] == 'in' :
                                if l[3] == 'array' :
                                        arg = 'const ' + typemap[l[1]] + ' *' + l[0]
                                else :
                                        arg = typemap[l[1]] + ' ' + l[0]
                        elif l[2] == 'out' :
                                arg = typemap[l[1]] + ' *' + l[0]

                        al.append(arg)

        if len(al) == 0 :
                arglist = 'void'
        else:
                arglist  = ', '.join(al)

        if wrappers[w]['category'].startswith('VERSION_1_0') or wrappers[w]['category'].startswith('VERSION_1_1') :
                if staticwrappers :
                        print 'static',
                print returntype + ' ' + funcname + 'Wrapper(' + arglist + ')'
                print '{'
                print '  if (glxWinDebugSettings.enable' + prefix.upper() + 'callTrace) ErrorF("'+ funcname + '\\n");'
                print '  glWinDirectProcCalls++;'
                if returntype.lower() == 'void' :
                        print '  ' +  funcname + '(',
                else :
                        print ' /* returntype was ' + returntype.lower() + '*/'
                        print '  return ' +  funcname + '(',

                if arglist != 'void' :
                        print wrappers[w]['arglist_use'],

                print ');'
                print "}\n"
        else:
                if staticwrappers :
                        print 'static',
                print returntype + ' ' + funcname + 'Wrapper(' + arglist + ')'
                print '{'

                stringname = funcname

#
# special case: Windows OpenGL implementations are far more likely to have GL_ARB_window_pos than GL_MESA_window_pos,
# so arrange for the wrapper to use the ARB strings to find functions...
#

                m2 = r3.search(funcname)
                if m2 :
                        stringname = stringname.replace('MESA','ARB')

#
# special case: likewise, implementations are more likely to have GL_ARB_vertex_program than GL_NV_vertex_program,
# especially if they are not NV implementations, so arrange for the wrapper to use ARB strings to find functions
#

                m3 = r4.search(funcname)
                if m3 :
                        stringname = stringname.replace('NV','ARB')
                m4 = r5.search(funcname)
                if m4 :
                        stringname = stringname.replace('NV','ARB')

                pfntypename = 'PFN' + funcname.upper() + 'PROC'

                if returntype.lower() == 'void' :
                        print '  RESOLVE(' + pfntypename + ', "' + stringname + '");'
                        print '  if (glxWinDebugSettings.enable' + prefix.upper() + 'callTrace) ErrorF("'+ funcname + '\\n");'
                        print '  RESOLVED_PROC(' + pfntypename + ')(',
                else :
                        print '  RESOLVE_RET(' + pfntypename + ', "' + stringname + '", FALSE);'
                        print '  if (glxWinDebugSettings.enable' + prefix.upper() + 'callTrace) ErrorF("'+ funcname + '\\n");'
                        print '  return RESOLVED_PROC(' + pfntypename + ')(',

                if arglist != 'void' :
                        print wrappers[w]['arglist_use'],

                print ');'
                print "}\n"


# generate function to setup the dispatch table, which sets each
# dispatch table entry to point to it's wrapper function
# (assuming we were able to make one)

if dispatchheader :
        print 'void glWinSetupDispatchTable(void)'
        print '{'
        print '  static struct _glapi_table *disp = NULL;'
        print ''
        print '  if (!disp)'
        print '    {'
        print '      disp = calloc(sizeof(void *), _glapi_get_dispatch_table_size());'
        print '      assert(disp);'

        for d in sorted(dispatch.keys()) :
                if wrappers.has_key(d) :
                        print '      SET_'+ d + '(disp, (void *)' + prefix + d + 'Wrapper);'
                else :
                        print '#warning  No wrapper for ' + prefix + d + ' !'

        print '    }'
        print ''
        print '  _glapi_set_dispatch(disp);'
        print '}'
