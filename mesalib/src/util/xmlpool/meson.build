# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

_langs = ['ca', 'es', 'de', 'nl', 'sv', 'fr']

_langs_po_files = []
foreach lang : _langs
  _langs_po_files += files(lang + '.po')
endforeach

xmlpool_options_h = custom_target(
  'xmlpool_options.h',
  input : ['gen_xmlpool.py', 't_options.h'],
  output : 'options.h',
  command : [
    prog_python, '@INPUT0@', '--template', '@INPUT1@', '--output', '@OUTPUT@',
    '--localedir', meson.current_build_dir(), '--languages',  _langs,
  ],
  depend_files : _langs_po_files,
)

i18n = import('i18n')
i18n.gettext('xmlpool', install : false)
