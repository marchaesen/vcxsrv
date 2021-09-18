# Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
import os
import errno
import sys
import argparse
import tempfile
import filecmp
import shutil
from mako.template import Template
from mako.exceptions import RichTraceback

#==============================================================================
def ConcatLists(list_of_lists):
    output = []
    for l in list_of_lists: output += l
    return output

#==============================================================================
def MakeTmpDir(suffix=''):
    '''
        Create temporary directory for use in codegen scripts.
    '''
    return tempfile.mkdtemp(suffix)

#==============================================================================
def MakeDir(dir_path):
    '''
        Create a directory if it doesn't exist

        returns 0 on success, non-zero on failure
    '''
    dir_path = os.path.abspath(dir_path)

    if not os.path.exists(dir_path):
        try:
            os.makedirs(dir_path)
        except OSError as err:
            if err.errno != errno.EEXIST:
                return 1
    else:
        if not os.path.isdir(dir_path):
            return 1

    return 0

#==============================================================================
def DeleteDirTree(dir_path):
    '''
        Delete directory tree.

        returns 0 on success, non-zero on failure
    '''
    rval = 0
    try:
        shutil.rmtree(dir_path, False)
    except:
        rval = 1
    return rval

#==============================================================================
def CopyFileIfDifferent(src, dst, verbose = False):
    '''
        Copy <src> file to <dst> file if the <dst>
        file either doesn't contain the file or the file
        contents are different.

        returns 0 on success, non-zero on failure
    '''

    assert os.path.isfile(src)
    assert (False == os.path.exists(dst) or os.path.isfile(dst))

    need_copy = not os.path.exists(dst)
    if not need_copy:
        need_copy = not filecmp.cmp(src, dst)

    if need_copy:
        try:
            shutil.copy2(src, dst)
        except:
            print('ERROR: Could not copy %s to %s' % (src, dst), file=sys.stderr)
            return 1

        if verbose:
            print(src, '-->', dst)

    return 0

#==============================================================================
def CopyDirFilesIfDifferent(src, dst, recurse = True, verbose = False, orig_dst = None):
    '''
        Copy files <src> directory to <dst> directory if the <dst>
        directory either doesn't contain the file or the file
        contents are different.

        Optionally recurses into subdirectories

        returns 0 on success, non-zero on failure
    '''

    assert os.path.isdir(src)
    assert os.path.isdir(dst)

    src = os.path.abspath(src)
    dst = os.path.abspath(dst)

    if not orig_dst:
        orig_dst = dst

    for f in os.listdir(src):
        src_path = os.path.join(src, f)
        dst_path = os.path.join(dst, f)

        # prevent recursion
        if src_path == orig_dst:
            continue

        if os.path.isdir(src_path):
            if recurse:
                if MakeDir(dst_path):
                    print('ERROR: Could not create directory:', dst_path, file=sys.stderr)
                    return 1

                if verbose:
                    print('mkdir', dst_path)
                rval = CopyDirFilesIfDifferent(src_path, dst_path, recurse, verbose, orig_dst)
        else:
            rval = CopyFileIfDifferent(src_path, dst_path, verbose)

        if rval:
            return rval

    return 0

#==============================================================================
class MakoTemplateWriter:
    '''
        MakoTemplateWriter - Class (namespace) for functions to generate strings
        or files using the Mako template module.

        See http://docs.makotemplates.org/en/latest/ for
        mako documentation.
   '''
    
    @staticmethod
    def to_string(template_filename, **kwargs):
        '''
            Write template data to a string object and return the string
        '''
        from mako.template      import Template
        from mako.exceptions    import RichTraceback

        try:
            template = Template(filename=template_filename)
            # Split + Join fixes line-endings for whatever platform you are using
            return '\n'.join(template.render(**kwargs).splitlines())
        except:
            traceback = RichTraceback()
            for (filename, lineno, function, line) in traceback.traceback:
                print('File %s, line %s, in %s' % (filename, lineno, function))
                print(line, '\n')
            print('%s: %s' % (str(traceback.error.__class__.__name__), traceback.error))
            raise

    @staticmethod
    def to_file(template_filename, output_filename, **kwargs):
        '''
            Write template data to a file
        '''
        if MakeDir(os.path.dirname(output_filename)):
            return 1
        with open(output_filename, 'w') as outfile:
            print(MakoTemplateWriter.to_string(template_filename, **kwargs), file=outfile)
        return 0


#==============================================================================
class ArgumentParser(argparse.ArgumentParser):
    '''
    Subclass of argparse.ArgumentParser

    Allow parsing from command files that start with @
    Example:
      >bt run @myargs.txt
    
    Contents of myargs.txt:
      -m <machine>
      --target cdv_win7
    
    The below function allows multiple args to be placed on the same text-file line.
    The default is one token per line, which is a little cumbersome.
    
    Also allow all characters after a '#' character to be ignored.
    '''
    
    #==============================================================================
    class _HelpFormatter(argparse.RawTextHelpFormatter):
        ''' Better help formatter for argument parser '''

        def _split_lines(self, text, width):
            ''' optimized split lines algorithm, indents split lines '''
            lines = text.splitlines()
            out_lines = []
            if len(lines):
                out_lines.append(lines[0])
                for line in lines[1:]:
                    out_lines.append('  ' + line)
            return out_lines

    #==============================================================================
    def __init__(self, *args, **kwargs):
        ''' Constructor.  Compatible with argparse.ArgumentParser(),
            but with some modifications for better usage and help display.
        '''
        super(ArgumentParser, self).__init__(
                *args,
                fromfile_prefix_chars='@',
                formatter_class=ArgumentParser._HelpFormatter,
                **kwargs)

    #==========================================================================
    def convert_arg_line_to_args(self, arg_line):
        ''' convert one line of parsed file to arguments '''
        arg_line = arg_line.split('#', 1)[0]
        if sys.platform == 'win32':
            arg_line = arg_line.replace('\\', '\\\\')
        for arg in shlex.split(arg_line):
            if not arg.strip():
                continue
            yield arg

    #==========================================================================
    def _read_args_from_files(self, arg_strings):
        ''' read arguments from files '''
        # expand arguments referencing files
        new_arg_strings = []
        for arg_string in arg_strings:

            # for regular arguments, just add them back into the list
            if arg_string[0] not in self.fromfile_prefix_chars:
                new_arg_strings.append(arg_string)

            # replace arguments referencing files with the file content
            else:
                filename = arg_string[1:]

                # Search in sys.path
                if not os.path.exists(filename):
                    for path in sys.path:
                        filename = os.path.join(path, arg_string[1:])
                        if os.path.exists(filename):
                            break

                try:
                    args_file = open(filename)
                    try:
                        arg_strings = []
                        for arg_line in args_file.read().splitlines():
                            for arg in self.convert_arg_line_to_args(arg_line):
                                arg_strings.append(arg)
                        arg_strings = self._read_args_from_files(arg_strings)
                        new_arg_strings.extend(arg_strings)
                    finally:
                        args_file.close()
                except IOError:
                    err = sys.exc_info()[1]
                    self.error(str(err))

        # return the modified argument list
        return new_arg_strings
