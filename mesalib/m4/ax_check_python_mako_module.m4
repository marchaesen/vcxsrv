# ===========================================================================
#
# SYNOPSIS
#
#   AX_CHECK_PYTHON_MAKO_MODULE(MIN_VERSION_NUMBER)
#
# DESCRIPTION
#
#   Check whether Python mako module is installed and its version higher than
#   minimum requested.
#
#   Example of its use:
#
#   For example, the minimum mako version would be 0.7.3. Then configure.ac
#   would contain:
#
#   AX_CHECK_PYTHON_MAKO_MODULE(0.7.3)
#
# LICENSE
#
#   Copyright (c) 2014 Intel Corporation.
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to
#   deal in the Software without restriction, including without limitation the
#   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
#   sell copies of the Software, and to permit persons to whom the Software is
#   furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in
#   all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
#   IN THE SOFTWARE.

dnl macro that checks for mako module in python
AC_DEFUN([AX_CHECK_PYTHON_MAKO_MODULE],
[AC_MSG_CHECKING(if module mako in python is installed)
    echo "
try:
    import sys
    import mako
except ImportError as err:
    sys.exit(err)
else:
    ver_req = map(int, '$1'.split('.'))
    ver_act = map(int, mako.__version__.split('.'))
    sys.exit(int(ver_req > ver_act))
    " | $PYTHON2 -

    if test $? -ne 0 ; then
       AC_MSG_RESULT(no)
       AC_SUBST(acv_mako_found, 'no')
    else
       AC_MSG_RESULT(yes)
       AC_SUBST(acv_mako_found, 'yes')
    fi
])
