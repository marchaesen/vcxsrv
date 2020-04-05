
# Copyright (C) 2012 Intel Corporation
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

# marshal_XML.py: factory for interpreting XML for the purpose of
# building thread marshalling code.

import gl_XML


class marshal_item_factory(gl_XML.gl_item_factory):
    """Factory to create objects derived from gl_item containing
    information necessary to generate thread marshalling code."""

    def create_function(self, element, context):
        return marshal_function(element, context)


class marshal_function(gl_XML.gl_function):
    def process_element(self, element):
        # Do normal processing.
        super(marshal_function, self).process_element(element)

        # Only do further processing when we see the canonical
        # function name.
        if element.get('name') != self.name:
            return

        # Classify fixed and variable parameters.
        self.fixed_params = []
        self.variable_params = []
        for p in self.parameters:
            if p.is_padding:
                continue
            if p.is_variable_length():
                self.variable_params.append(p)
            else:
                self.fixed_params.append(p)

        # Store the "marshal" attribute, if present.
        self.marshal = element.get('marshal')
        self.marshal_sync = element.get('marshal_sync')
        self.marshal_call_after = element.get('marshal_call_after')

    def marshal_flavor(self):
        """Find out how this function should be marshalled between
        client and server threads."""
        # If a "marshal" attribute was present, that overrides any
        # determination that would otherwise be made by this function.
        if self.marshal not in (None, 'draw'):
            return self.marshal

        if self.exec_flavor == 'skip':
            # Functions marked exec="skip" are not yet implemented in
            # Mesa, so don't bother trying to marshal them.
            return 'skip'

        if self.return_type != 'void':
            return 'sync'
        for p in self.parameters:
            if p.is_output:
                return 'sync'
            if (p.is_pointer() and not (p.count or p.counter or p.marshal_count)
                and not (self.marshal == 'draw'
                         and (p.name == 'indices' or p.name == 'indirect'))):
                return 'sync'
            if p.count_parameter_list and not p.marshal_count:
                # Parameter size is determined by enums; haven't
                # written logic to handle this yet.  TODO: fix.
                return 'sync'
        return 'async'
