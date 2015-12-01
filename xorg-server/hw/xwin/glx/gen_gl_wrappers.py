#!/usr/bin/python3
#
# Copyright (c) 2013 The Khronos Group Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and/or associated documentation files (the
# "Materials"), to deal in the Materials without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Materials, and to
# permit persons to whom the Materials are furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Materials.
#
# THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

import sys, time, pdb, string, cProfile
from reg import *

# debug - start header generation in debugger
# dump - dump registry after loading
# profile - enable Python profiling
# protect - whether to use #ifndef protections
# registry <filename> - use specified XML registry instead of gl.xml
# timeit - time length of registry loading & header generation
# validate - validate return & parameter group tags against <group>
debug   = False
dump    = False
profile = False
protect = True
timeit  = False
validate= False
# Default input / log files
errFilename = None
diagFilename = 'diag.txt'
regFilename = 'gl.xml'
outFilename = 'gen_gl_wrappers.c'
dispatchheader=None
prefix="gl"
preresolve=False
staticwrappers=False
nodebugcallcounting=False

# list of WGL extension functions we use
used_wgl_ext_fns = {key: 1 for key in [
    "wglSwapIntervalEXT",
    "wglGetExtensionsStringARB",
    "wglDestroyPbufferARB",
    "wglGetPbufferDCARB",
    "wglReleasePbufferDCARB",
    "wglCreatePbufferARB",
    "wglMakeContextCurrentARB",
    "wglChoosePixelFormatARB",
    "wglGetPixelFormatAttribivARB",
    "wglGetPixelFormatAttribivARB"
]}

if __name__ == '__main__':
    i = 1
    while (i < len(sys.argv)):
        arg = sys.argv[i]
        i = i + 1
        if (arg == '-debug'):
            print('Enabling debug (-debug)', file=sys.stderr)
            debug = True
        elif (arg == '-dump'):
            print('Enabling dump (-dump)', file=sys.stderr)
            dump = True
        elif (arg == '-noprotect'):
            print('Disabling inclusion protection in output headers', file=sys.stderr)
            protect = False
        elif (arg == '-profile'):
            print('Enabling profiling (-profile)', file=sys.stderr)
            profile = True
        elif (arg == '-registry'):
            regFilename = sys.argv[i]
            i = i+1
            print('Using registry ', regFilename, file=sys.stderr)
        elif (arg == '-time'):
            print('Enabling timing (-time)', file=sys.stderr)
            timeit = True
        elif (arg == '-validate'):
            print('Enabling group validation (-validate)', file=sys.stderr)
            validate = True
        elif (arg == '-outfile'):
            outFilename = sys.argv[i]
            i = i+1
        elif (arg == '-preresolve'):
            preresolve=True
        elif (arg == '-staticwrappers'):
            staticwrappers=True
        elif (arg == '-dispatchheader'):
            dispatchheader = sys.argv[i]
            i = i+1
        elif (arg == '-prefix'):
            prefix = sys.argv[i]
            i = i+1
        elif (arg == '-nodbgcount'):
            nodebugcallcounting = True
        elif (arg[0:1] == '-'):
            print('Unrecognized argument:', arg, file=sys.stderr)
            exit(1)
print('Generating ', outFilename, file=sys.stderr)

# Simple timer functions
startTime = None
def startTimer():
    global startTime
    startTime = time.clock()
def endTimer(msg):
    global startTime
    endTime = time.clock()
    if (timeit):
        print(msg, endTime - startTime)
        startTime = None

# Load & parse registry
reg = Registry()

startTimer()
tree = etree.parse(regFilename)
endTimer('Time to make ElementTree =')

startTimer()
reg.loadElementTree(tree)
endTimer('Time to parse ElementTree =')

if (validate):
    reg.validateGroups()

if (dump):
    print('***************************************')
    print('Performing Registry dump to regdump.txt')
    print('***************************************')
    reg.dumpReg(filehandle = open('regdump.txt','w'))

# Turn a list of strings into a regexp string matching exactly those strings
def makeREstring(list):
    return '^(' + '|'.join(list) + ')$'

# These are "mandatory" OpenGL ES 1 extensions, to
# be included in the core GLES/gl.h header.
es1CoreList = [
    'GL_OES_read_format',
    'GL_OES_compressed_paletted_texture',
    'GL_OES_point_size_array',
    'GL_OES_point_sprite'
]

# Descriptive names for various regexp patterns used to select
# versions and extensions

allVersions     = allExtensions = '.*'
noVersions      = noExtensions = None
gl12andLaterPat = '1\.[2-9]|[234]\.[0-9]'
gles2onlyPat    = '2\.[0-9]'
gles2and3Pat    = '[23]\.[0-9]'
es1CorePat      = makeREstring(es1CoreList)
# Extensions in old glcorearb.h but not yet tagged accordingly in gl.xml
glCoreARBPat    = None
glx13andLaterPat = '1\.[3-9]'

# Defaults for generating re-inclusion protection wrappers (or not)
protectFile = protect
protectFeature = protect
protectProto = protect

genOpts = CGeneratorOptions(
        apiname           = prefix,
        profile           = 'compatibility',
        versions          = allVersions,
        emitversions      = allVersions,
        defaultExtensions = prefix,                   # Default extensions for GL
#        addExtensions     = None,
#        removeExtensions  = None,
#        prefixText        = prefixStrings + glExtPlatformStrings + glextVersionStrings,
#        genFuncPointers   = True,
#        protectFile       = protectFile,
#        protectFeature    = protectFeature,
#        protectProto      = protectProto,
#        apicall           = 'GLAPI ',
#        apientry          = 'APIENTRY ',
#        apientryp         = 'APIENTRYP '),
        )

# create error/warning & diagnostic files
if (errFilename):
    errWarn = open(errFilename,'w')
else:
    errWarn = sys.stderr
diag = open(diagFilename, 'w')

#
# look for all the SET_ macros in dispatch.h, this is the set of functions
# we need to generate
#

dispatch = {}

if dispatchheader :
    fh = open(dispatchheader)
    dispatchh = fh.readlines()

    dispatch_regex = re.compile(r'(?:#define|static\s+INLINE\s+void)\s+SET_([^\()]+)\(')

    for line in dispatchh :
        line = line.strip()
        m1 = dispatch_regex.search(line)

        if m1 :
            dispatch[prefix+m1.group(1)] = 1

    del dispatch['glby_offset']

def ParseCmdRettype(cmd):
    proto=noneStr(cmd.elem.find('proto'))
    rettype=noneStr(proto.text)
    if rettype.lower()!="void ":
        plist = ([t for t in proto.itertext()])
        rettype = ''.join(plist[:-1])
    rettype=rettype.strip()
    return rettype

def ParseCmdParams(cmd):
    params = cmd.elem.findall('param')
    plist=[]
    for param in params:
        # construct the formal parameter definition from ptype and name
        # elements, also using any text found around these in the
        # param element, in the order it appears in the document
        paramtype = ''
        # also extract the formal parameter name from the name element
        paramname = ''
        for t in param.iter():
            if t.tag == 'ptype' or t.tag == 'param':
                paramtype = paramtype + noneStr(t.text)
            if t.tag == 'name':
                paramname = t.text + '_'
                paramtype = paramtype + ' ' + paramname
            if t.tail is not None:
                paramtype = paramtype + t.tail.strip()
        plist.append((paramtype, paramname))
    return plist

class PreResolveOutputGenerator(OutputGenerator):
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.wrappers={}
    def beginFile(self, genOpts):
        pass
    def endFile(self):
        self.outFile.write('\nvoid ' + prefix + 'ResolveExtensionProcs(void)\n{\n')
        for funcname in self.wrappers.keys():
            self.outFile.write( '  PRERESOLVE(PFN' + funcname.upper() + 'PROC, "' + funcname + '");\n')
        self.outFile.write('}\n\n')
    def beginFeature(self, interface, emit):
        OutputGenerator.beginFeature(self, interface, emit)
        self.OldVersion = self.featureName.startswith('GL_VERSION_1_0') or self.featureName.startswith('GL_VERSION_1_1')
    def endFeature(self):
        OutputGenerator.endFeature(self)
    def genType(self, typeinfo, name):
        OutputGenerator.genType(self, typeinfo, name)
    def genEnum(self, enuminfo, name):
        OutputGenerator.genEnum(self, enuminfo, name)
    def genCmd(self, cmd, name):
        OutputGenerator.genCmd(self, cmd, name)
        if prefix == 'wgl' and not name in used_wgl_ext_fns:
            return

        self.outFile.write('RESOLVE_DECL(PFN' + name.upper() + 'PROC);\n')
        self.wrappers[name]=1

class MyOutputGenerator(OutputGenerator):
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.wrappers={}
    def beginFile(self, genOpts):
        pass
    def endFile(self):
        pass
    def beginFeature(self, interface, emit):
        OutputGenerator.beginFeature(self, interface, emit)
        self.OldVersion = self.featureName.startswith('GL_VERSION_1_0') or self.featureName.startswith('GL_VERSION_1_1')
    def endFeature(self):
        OutputGenerator.endFeature(self)
    def genType(self, typeinfo, name):
        OutputGenerator.genType(self, typeinfo, name)
    def genEnum(self, enuminfo, name):
        OutputGenerator.genEnum(self, enuminfo, name)
    def genCmd(self, cmd, name):
        OutputGenerator.genCmd(self, cmd, name)
        # Avoid generating wrappers which aren't referenced by the dispatch table
        if dispatchheader and not name in dispatch :
            self.outFile.write('/* No wrapper for ' + name + ', not in dispatch table */\n')
            return

        if prefix == 'wgl' and not name in used_wgl_ext_fns:
            return

        self.wrappers[name]=1
        rettype=ParseCmdRettype(cmd)

        if staticwrappers: self.outFile.write("static ")
        self.outFile.write("%s __stdcall %sWrapper("%(rettype, name))
        plist=ParseCmdParams(cmd)
        Comma=""
        if len(plist):
            for ptype, pname in plist:
                self.outFile.write("%s%s"%(Comma, ptype))
                Comma=", "
        else:
            self.outFile.write("void")
        if self.OldVersion:
            if nodebugcallcounting:
                self.outFile.write(")\n{\n")
            else:
                self.outFile.write( """)
{
#ifdef _DEBUG
  if (glxWinDebugSettings.enable%scallTrace) ErrorF("%s\\n");
  glWinDirectProcCalls++;
#endif
"""%(prefix.upper(), name))
            if rettype.lower()=="void":
                self.outFile.write("  %s( "%(name))
            else:
                self.outFile.write("  return %s( "%(name))
            Comma=""
            for ptype, pname in plist:
                self.outFile.write("%s%s"%(Comma, pname))
                Comma=", "
        else:
            if rettype.lower()=="void":
                self.outFile.write(""")
{
  RESOLVE(PFN%sPROC, "%s");"""%(name.upper(), name))
                if not nodebugcallcounting: self.outFile.write("""
#ifdef _DEBUG
  if (glxWinDebugSettings.enable%scallTrace) ErrorF("%s\\n");
#endif"""%(prefix.upper(), name))
                self.outFile.write("""
  RESOLVED_PROC(PFN%sPROC)( """%(name.upper()))
            else:
                self.outFile.write(""")
{
  RESOLVE_RET(PFN%sPROC, "%s", FALSE);"""%(name.upper(), name))
                if not nodebugcallcounting: self.outFile.write("""
#ifdef _DEBUG
  if (glxWinDebugSettings.enable%scallTrace) ErrorF("%s\\n");
#endif"""%(prefix.upper(), name))
                self.outFile.write("""
  return RESOLVED_PROC(PFN%sPROC)( """%(name.upper()))
            Comma=""
            for ptype, pname in plist:
                self.outFile.write("%s%s"%(Comma, pname))
                Comma=", "
        self.outFile.write(" );\n}\n\n")

def genHeaders():
    startTimer()
    outFile = open(outFilename,"w")
    if preresolve:
        gen = PreResolveOutputGenerator(errFile=errWarn,
                                        warnFile=errWarn,
                                        diagFile=diag)
        gen.outFile=outFile
        reg.setGenerator(gen)
        reg.apiGen(genOpts)
    gen = MyOutputGenerator(errFile=errWarn,
                            warnFile=errWarn,
                            diagFile=diag)
    gen.outFile=outFile
    reg.setGenerator(gen)
    reg.apiGen(genOpts)

    # generate function to setup the dispatch table, which sets each
    # dispatch table entry to point to it's wrapper function
    # (assuming we were able to make one)

    if dispatchheader :
        outFile.write( 'void glWinSetupDispatchTable(void)\n')
        outFile.write( '{\n')
        outFile.write( '  struct _glapi_table *disp = _glapi_get_dispatch();\n')

        for d in sorted(dispatch.keys()) :
                if d in gen.wrappers :
                        outFile.write('  SET_'+ d[len(prefix):] + '(disp, (void *)' + d + 'Wrapper);\n')
#enable this if you want to see this warning messages                else :
#                        outFile.write('#pragma message("No wrapper for ' + d + ' !")\n')

        outFile.write('}\n')



    outFile.close()

if (debug):
    pdb.run('genHeaders()')
elif (profile):
    import cProfile, pstats
    cProfile.run('genHeaders()', 'profile.txt')
    p = pstats.Stats('profile.txt')
    p.strip_dirs().sort_stats('time').print_stats(50)
else:
    genHeaders()
