# Copyright (C) 2017-2018 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the 'Software'),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source

import itertools
import os
import sys
from gen_common import *


def main(args=sys.argv[1:]):
    thisDir = os.path.dirname(os.path.realpath(__file__))
    parser = ArgumentParser('Generate files and initialization functions for all permutations of BackendPixelRate.')
    parser.add_argument('--dim', help='gBackendPixelRateTable array dimensions', nargs='+', type=int, required=True)
    parser.add_argument('--outdir', help='output directory', nargs='?', type=str, default=thisDir)
    parser.add_argument('--split', help='how many lines of initialization per file [0=no split]', nargs='?', type=int, default='512')
    parser.add_argument('--numfiles', help='how many output files to generate', nargs='?', type=int, default='0')
    parser.add_argument('--cpp', help='Generate cpp file(s)', action='store_true', default=False)
    parser.add_argument('--hpp', help='Generate hpp file', action='store_true', default=False)
    parser.add_argument('--cmake', help='Generate cmake file', action='store_true', default=False)
    parser.add_argument('--rast', help='Generate rasterizer functions instead of normal backend', action='store_true', default=False)

    args = parser.parse_args(args)


    class backendStrs :
        def __init__(self) :
            self.outFileName = 'gen_BackendPixelRate%s.cpp'
            self.outHeaderName = 'gen_BackendPixelRate.hpp'
            self.functionTableName = 'gBackendPixelRateTable'
            self.funcInstanceHeader = ' = BackendPixelRate<SwrBackendTraits<'
            self.template = 'gen_backend.cpp'
            self.hpp_template = 'gen_header_init.hpp'
            self.cmakeFileName = 'gen_backends.cmake'
            self.cmakeSrcVar = 'GEN_BACKEND_SOURCES'
            self.tableName = 'BackendPixelRate'

            if args.rast:
                self.outFileName = 'gen_rasterizer%s.cpp'
                self.outHeaderName = 'gen_rasterizer.hpp'
                self.functionTableName = 'gRasterizerFuncs'
                self.funcInstanceHeader = ' = RasterizeTriangle<RasterizerTraits<'
                self.template = 'gen_rasterizer.cpp'
                self.cmakeFileName = 'gen_rasterizer.cmake'
                self.cmakeSrcVar = 'GEN_RASTERIZER_SOURCES'
                self.tableName = 'RasterizerFuncs'


    backend = backendStrs()

    output_list = []
    for x in args.dim:
        output_list.append(list(range(x)))

    # generate all permutations possible for template parameter inputs
    output_combinations = list(itertools.product(*output_list))
    output_list = []

    # for each permutation
    for x in range(len(output_combinations)):
        # separate each template peram into its own list member
        new_list = [output_combinations[x][i] for i in range(len(output_combinations[x]))]
        tempStr = backend.functionTableName
        #print each list member as an index in the multidimensional array
        for i in new_list:
            tempStr += '[' + str(i) + ']'
        #map each entry in the permutation as its own string member, store as the template instantiation string
        tempStr += backend.funcInstanceHeader + ','.join(map(str, output_combinations[x])) + '>>;'
        #append the line of c++ code in the list of output lines
        output_list.append(tempStr)

    # how many files should we split the global template initialization into?
    if (args.split == 0):
        numFiles = 1
    else:
        numFiles = (len(output_list) + args.split - 1) // args.split
    if (args.numfiles != 0):
        numFiles = args.numfiles
    linesPerFile = (len(output_list) + numFiles - 1) // numFiles
    chunkedList = [output_list[x:x+linesPerFile] for x in range(0, len(output_list), linesPerFile)]

    tmp_output_dir = MakeTmpDir('_codegen')

    if not os.path.exists(args.outdir):
        try:
            os.makedirs(args.outdir)
        except OSError as err:
            if err.errno != errno.EEXIST:
                print('ERROR: Could not create directory:', args.outdir, file=sys.stderr)
                return 1

    rval = 0

    # generate .cpp files
    try:
        if args.cpp:
            baseCppName = os.path.join(tmp_output_dir, backend.outFileName)
            templateCpp = os.path.join(thisDir, 'templates', backend.template)

            for fileNum in range(numFiles):
                filename = baseCppName % str(fileNum)
                MakoTemplateWriter.to_file(
                    templateCpp,
                    baseCppName % str(fileNum),
                    cmdline=sys.argv,
                    fileNum=fileNum,
                    funcList=chunkedList[fileNum])

        if args.hpp:
            baseHppName = os.path.join(tmp_output_dir, backend.outHeaderName)
            templateHpp = os.path.join(thisDir, 'templates', backend.hpp_template)

            MakoTemplateWriter.to_file(
                templateHpp,
                baseHppName,
                cmdline=sys.argv,
                numFiles=numFiles,
                filename=backend.outHeaderName,
                tableName=backend.tableName)

        # generate gen_backend.cmake file
        if args.cmake:
            templateCmake = os.path.join(thisDir, 'templates', 'gen_backend.cmake')
            cmakeFile = os.path.join(tmp_output_dir, backend.cmakeFileName)

            MakoTemplateWriter.to_file(
                templateCmake,
                cmakeFile,
                cmdline=sys.argv,
                srcVar=backend.cmakeSrcVar,
                numFiles=numFiles,
                baseCppName='${RASTY_GEN_SRC_DIR}/backends/' + os.path.basename(baseCppName))

        rval = CopyDirFilesIfDifferent(tmp_output_dir, args.outdir)

    except:
        rval = 1

    finally:
        DeleteDirTree(tmp_output_dir)

    return rval

if __name__ == '__main__':
    sys.exit(main())
