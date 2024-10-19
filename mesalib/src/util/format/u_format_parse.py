
'''
/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
'''


import copy
import yaml
import sys

try:
    from yaml import CSafeLoader as YAMLSafeLoader
except:
    from yaml import SafeLoader as YAMLSafeLoader

VOID, UNSIGNED, SIGNED, FIXED, FLOAT = range(5)

SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W, SWIZZLE_0, SWIZZLE_1, SWIZZLE_NONE, = range(7)

PLAIN = 'plain'

RGB = 'RGB'
SRGB = 'SRGB'
YUV = 'YUV'
ZS = 'ZS'


_type_parse_map = {
    '':  VOID,
    'X': VOID,
    'U': UNSIGNED,
    'S': SIGNED,
    'H': FIXED,
    'F': FLOAT,
}

_swizzle_parse_map = {
    'X': SWIZZLE_X,
    'Y': SWIZZLE_Y,
    'Z': SWIZZLE_Z,
    'W': SWIZZLE_W,
    '0': SWIZZLE_0,
    '1': SWIZZLE_1,
    '_': SWIZZLE_NONE,
}


def is_pot(x):
    return (x & (x - 1)) == 0


VERY_LARGE = 99999999999999999999999

def validate_str(x):
    if not isinstance(x, str):
        raise ValueError(type(x))

def validate_int(x):
    if not isinstance(x, int):
        raise ValueError(f"invalid type {type(x)}")

def validate_list_str_4(x):
    if not isinstance(x, list):
        raise ValueError(f"invalid type {type(x)}")
    if len(x) != 4:
        raise ValueError(f"invalid length {len(x)}")
    for i in range(len(x)):
        if isinstance(x[i], int):
            x[i] = str(x[i])
        if not isinstance(x[i], str):
            raise ValueError(f"invalid member type {type(x[i])}")

def validate_list_str_le4(x):
    if not isinstance(x, list):
        raise ValueError(f"invalid type {type(x)}")
    if len(x) > 4:
        raise ValueError(f"invalid length {len(x)}")
    for i in range(len(x)):
        if isinstance(x[i], int):
            x[i] = str(x[i])
        if not isinstance(x[i], str):
            raise ValueError(f"invalid member type {type(x[i])}")


def get_and_delete(d, k):
    ret = d[k]
    del(d[k])
    return ret

def do_consume(d, *args):
    if len(args) == 1:
        return get_and_delete(d, args[0])
    else:
        return do_consume(d[args[0]], *args[1:])

def consume(f, validate, d, *args):
    if len(args) > 1:
        sub = " under " + ".".join([f"'{a}'" for a in args[:-1]])
    else:
        sub = ""

    try:
        ret = do_consume(d, *args)
        validate(ret)
        return ret
    except KeyError:
        raise RuntimeError(f"Key '{args[-1]}' not present{sub} in format {f.name}")
    except ValueError as e:
        raise RuntimeError(f"Key '{args[-1]}' invalid{sub} in format {f.name}: {e.args[0]}")

def consume_str(f, d, *args):
    return consume(f, validate_str, d, *args)

def consume_int(f, d, *args):
    return consume(f, validate_int, d, *args)

def consume_list_str_4(f, d, *args):
    return consume(f, validate_list_str_4, d, *args)

def consume_list_str_le4(f, d, *args):
    return consume(f, validate_list_str_le4, d, *args)

def consumed(f, d, *args):
    if args:
        d = do_consume(d, *args)
    if len(d) > 0:
        keys = ", ".join([f"'{k}'" for k in d.keys()])
        if args:
            sub = " under " + ".".join([f"'{a}'" for a in args])
        else:
            sub = ""
        raise RuntimeError(f"Unknown keys ({keys}) present in format {f.name}{sub}")


class Channel:
    '''Describe the channel of a color channel.'''

    def __init__(self, type, norm, pure, size, name=''):
        self.type = type
        self.norm = norm
        self.pure = pure
        self.size = size
        self.sign = type in (SIGNED, FIXED, FLOAT)
        self.name = name

    def __str__(self):
        s = str(self.type)
        if self.norm:
            s += 'n'
        if self.pure:
            s += 'p'
        s += str(self.size)
        return s

    def __repr__(self):
        return "Channel({})".format(self.__str__())

    def __eq__(self, other):
        if other is None:
            return False

        return self.type == other.type and self.norm == other.norm and self.pure == other.pure and self.size == other.size

    def __ne__(self, other):
        return not self == other

    def max(self):
        '''Maximum representable number.'''
        if self.type == FLOAT:
            return VERY_LARGE
        if self.type == FIXED:
            return (1 << (self.size // 2)) - 1
        if self.norm:
            return 1
        if self.type == UNSIGNED:
            return (1 << self.size) - 1
        if self.type == SIGNED:
            return (1 << (self.size - 1)) - 1
        assert False

    def min(self):
        '''Minimum representable number.'''
        if self.type == FLOAT:
            return -VERY_LARGE
        if self.type == FIXED:
            return -(1 << (self.size // 2))
        if self.type == UNSIGNED:
            return 0
        if self.norm:
            return -1
        if self.type == SIGNED:
            return -(1 << (self.size - 1))
        assert False


class Format:
    '''Describe a pixel format.'''

    def __init__(self, source):
        self.name = "unknown"
        self.name = f"PIPE_FORMAT_{consume_str(self, source, 'name')}"
        self.layout = consume_str(self, source, 'layout')
        if 'sublayout' in source:
            self.sublayout = consume_str(self, source, 'sublayout')
        else:
            self.sublayout = None
        self.block_width = consume_int(self, source, 'block', 'width')
        self.block_height = consume_int(self, source, 'block', 'height')
        self.block_depth = consume_int(self, source, 'block', 'depth')
        consumed(self, source, 'block')
        self.colorspace = consume_str(self, source, 'colorspace')
        self.srgb_equivalent = None
        self.linear_equivalent = None

        # Formats with no endian-dependent swizzling declare their channel and
        # swizzle layout at the top level. Else they can declare an
        # endian-dependent swizzle. This only applies to packed formats,
        # however we can't use is_array() or is_bitmask() to test because they
        # depend on the channels having already been parsed.
        if 'swizzles' in source:
            self.le_swizzles = list(map(lambda x: _swizzle_parse_map[x],
                                        consume_list_str_4(self, source, 'swizzles')))
            self.le_channels = _parse_channels(consume_list_str_le4(self, source, 'channels'),
                                               self.layout, self.colorspace, self.le_swizzles)
            self.be_swizzles = None
            self.be_channels = None
            if source.get('little_endian', {}).get('swizzles') or \
               source.get('big_endian', {}).get('swizzles'):
                raise RuntimeError(f"Format {self.name} must not declare endian-dependent and endian-independent swizzles")
        else:
            self.le_swizzles = list(map(lambda x: _swizzle_parse_map[x],
                                        consume_list_str_4(self, source, 'little_endian', 'swizzles')))
            self.le_channels = _parse_channels(consume_list_str_le4(self, source, 'little_endian', 'channels'),
                                               self.layout, self.colorspace, self.le_swizzles)
            self.be_swizzles = list(map(lambda x: _swizzle_parse_map[x],
                                        consume_list_str_4(self, source, 'big_endian', 'swizzles')))
            self.be_channels = _parse_channels(consume_list_str_le4(self, source, 'big_endian', 'channels'),
                                               self.layout, self.colorspace, self.be_swizzles)
            if self.is_array():
                raise RuntimeError("Array format {self.name} must not define endian-specific swizzles")
            if self.is_bitmask():
                raise RuntimeError("Bitmask format {self.name} must not define endian-specific swizzles")

        self.le_alias = None
        self.be_alias = None
        if 'little_endian' in source:
            if 'alias' in source['little_endian']:
                self.le_alias = f"PIPE_FORMAT_{consume_str(self, source, 'little_endian', 'alias')}"
            consumed(self, source, 'little_endian')
        if 'big_endian' in source:
            if 'alias' in source['big_endian']:
                self.be_alias = f"PIPE_FORMAT_{consume_str(self, source, 'big_endian', 'alias')}"
            consumed(self, source, 'big_endian')

        consumed(self, source)
        del(source)

        if self.is_bitmask() and not self.is_array():
            # Bitmask formats are "load a word the size of the block and
            # bitshift channels out of it." However, the channel shifts
            # defined in u_format_table.c are numbered right-to-left on BE
            # for some historical reason (see below), which is hard to
            # change due to llvmpipe, so we also have to flip the channel
            # order and the channel-to-rgba swizzle values to read
            # right-to-left from the defined (non-VOID) channels so that the
            # correct shifts happen.
            #
            # This is nonsense, but it's the nonsense that makes
            # u_format_test pass and you get the right colors in softpipe at
            # least.
            chans = self.nr_channels()
            self.be_channels = self.le_channels[chans -
                                                1::-1] + self.le_channels[chans:4]

            xyzw = [SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W]
            chan_map = {SWIZZLE_X: xyzw[chans - 1] if chans >= 1 else SWIZZLE_X,
                        SWIZZLE_Y: xyzw[chans - 2] if chans >= 2 else SWIZZLE_X,
                        SWIZZLE_Z: xyzw[chans - 3] if chans >= 3 else SWIZZLE_X,
                        SWIZZLE_W: xyzw[chans - 4] if chans >= 4 else SWIZZLE_X,
                        SWIZZLE_1: SWIZZLE_1,
                        SWIZZLE_0: SWIZZLE_0,
                        SWIZZLE_NONE: SWIZZLE_NONE}
            self.be_swizzles = [chan_map[s] for s in self.le_swizzles]
        elif not self.be_channels:
            self.be_channels = copy.deepcopy(self.le_channels)
            self.be_swizzles = self.le_swizzles

        le_shift = 0
        for channel in self.le_channels:
            channel.shift = le_shift
            le_shift += channel.size

        be_shift = 0
        for channel in reversed(self.be_channels):
            channel.shift = be_shift
            be_shift += channel.size

        assert le_shift == be_shift
        for i in range(4):
            assert (self.le_swizzles[i] != SWIZZLE_NONE) == (
                self.be_swizzles[i] != SWIZZLE_NONE)

    def __str__(self):
        return self.name

    def __eq__(self, other):
        if not other:
            return False
        return self.name == other.name

    def __hash__(self):
        return hash(self.name)

    def short_name(self):
        '''Make up a short norm for a format, suitable to be used as suffix in
        function names.'''

        name = self.name
        if name.startswith('PIPE_FORMAT_'):
            name = name[len('PIPE_FORMAT_'):]
        name = name.lower()
        return name

    def block_size(self):
        size = 0
        for channel in self.le_channels:
            size += channel.size
        return size

    def nr_channels(self):
        nr_channels = 0
        for channel in self.le_channels:
            if channel.size:
                nr_channels += 1
        return nr_channels

    def array_element(self):
        if self.layout != PLAIN:
            return None
        ref_channel = self.le_channels[0]
        if ref_channel.type == VOID:
            ref_channel = self.le_channels[1]
        for channel in self.le_channels:
            if channel.size and (channel.size != ref_channel.size or channel.size % 8):
                return None
            if channel.type != VOID:
                if channel.type != ref_channel.type:
                    return None
                if channel.norm != ref_channel.norm:
                    return None
                if channel.pure != ref_channel.pure:
                    return None
        return ref_channel

    def is_array(self):
        return self.array_element() != None

    def is_mixed(self):
        if self.layout != PLAIN:
            return False
        ref_channel = self.le_channels[0]
        if ref_channel.type == VOID:
            ref_channel = self.le_channels[1]
        for channel in self.le_channels[1:]:
            if channel.type != VOID:
                if channel.type != ref_channel.type:
                    return True
                if channel.norm != ref_channel.norm:
                    return True
                if channel.pure != ref_channel.pure:
                    return True
        return False

    def is_compressed(self):
        for channel in self.le_channels:
            if channel.type != VOID:
                return False
        return True

    def is_unorm(self):
        # Non-compressed formats all have unorm or srgb in their name.
        for keyword in ['_UNORM', '_SRGB']:
            if keyword in self.name:
                return True

        # All the compressed formats in GLES3.2 and GL4.6 ("Table 8.14: Generic
        # and specific compressed internal formats.") that aren't snorm for
        # border colors are unorm, other than BPTC_*_FLOAT.
        return self.is_compressed() and not ('FLOAT' in self.name or self.is_snorm())

    def is_snorm(self):
        return '_SNORM' in self.name

    def is_pot(self):
        return is_pot(self.block_size())

    def is_int(self):
        if self.layout != PLAIN:
            return False
        for channel in self.le_channels:
            if channel.type not in (VOID, UNSIGNED, SIGNED):
                return False
        return True

    def is_float(self):
        if self.layout != PLAIN:
            return False
        for channel in self.le_channels:
            if channel.type not in (VOID, FLOAT):
                return False
        return True

    def is_bitmask(self):
        if self.layout != PLAIN:
            return False
        if self.block_size() not in (8, 16, 32):
            return False
        for channel in self.le_channels:
            if channel.type not in (VOID, UNSIGNED, SIGNED):
                return False
        return True

    def is_pure_color(self):
        if self.layout != PLAIN or self.colorspace == ZS:
            return False
        pures = [channel.pure
                 for channel in self.le_channels
                 if channel.type != VOID]
        for x in pures:
            assert x == pures[0]
        return pures[0]

    def channel_type(self):
        types = [channel.type
                 for channel in self.le_channels
                 if channel.type != VOID]
        for x in types:
            assert x == types[0]
        return types[0]

    def is_pure_signed(self):
        return self.is_pure_color() and self.channel_type() == SIGNED

    def is_pure_unsigned(self):
        return self.is_pure_color() and self.channel_type() == UNSIGNED

    def has_channel(self, id):
        return self.le_swizzles[id] != SWIZZLE_NONE

    def has_depth(self):
        return self.colorspace == ZS and self.has_channel(0)

    def has_stencil(self):
        return self.colorspace == ZS and self.has_channel(1)

    def stride(self):
        return self.block_size()/8



def _parse_channels(fields, layout, colorspace, swizzles):
    if layout == PLAIN:
        names = ['']*4
        if colorspace in (RGB, SRGB):
            for i in range(4):
                swizzle = swizzles[i]
                if swizzle < 4:
                    names[swizzle] += 'rgba'[i]
        elif colorspace == ZS:
            for i in range(4):
                swizzle = swizzles[i]
                if swizzle < 4:
                    names[swizzle] += 'zs'[i]
        else:
            assert False
        for i in range(4):
            if names[i] == '':
                names[i] = 'x'
    else:
        names = ['x', 'y', 'z', 'w']

    channels = []
    for i in range(0, 4):
        if i < len(fields):
            field = fields[i]
            type = _type_parse_map[field[0]]
            if field[1] == 'N':
                norm = True
                pure = False
                size = int(field[2:])
            elif field[1] == 'P':
                pure = True
                norm = False
                size = int(field[2:])
            else:
                norm = False
                pure = False
                size = int(field[1:])
        else:
            type = VOID
            norm = False
            pure = False
            size = 0
        channel = Channel(type, norm, pure, size, names[i])
        channels.append(channel)

    return channels

def mostly_equivalent(one, two):
    if one.layout != two.layout or \
       one.sublayout != two.sublayout or \
       one.block_width != two.block_width or \
       one.block_height != two.block_height or \
       one.block_depth != two.block_depth or \
       one.le_swizzles != two.le_swizzles or \
       one.le_channels != two.le_channels or \
       one.be_swizzles != two.be_swizzles or \
       one.be_channels != two.be_channels:
        return False
    return True

def should_ignore_for_mapping(fmt):
    # This format is a really special reinterpretation of depth/stencil as
    # RGB. Until we figure out something better, just special-case it so
    # we won't consider it as equivalent to anything.
    if fmt.name == "PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8":
        return True
    return False
    

def parse(filename):
    '''Parse the format description in YAML format in terms of the
    Channel and Format classes above.'''

    stream = open(filename)
    doc = yaml.load(stream, Loader=YAMLSafeLoader)
    assert(isinstance(doc, list))

    ret = []
    for entry in doc:
        assert(isinstance(entry, dict))
        try:
            f = Format(copy.deepcopy(entry))
        except Exception as e:
            raise RuntimeError(f"Failed to parse entry {entry}: {e}")
        if f in ret:
            raise RuntimeError(f"Duplicate format entry {f.name}")
        ret.append(f)

    for fmt in ret:
        if should_ignore_for_mapping(fmt):
            continue
        if fmt.colorspace != RGB and fmt.colorspace != SRGB:
            continue
        if fmt.colorspace == RGB:
            for equiv in ret:
                if equiv.colorspace != SRGB or not mostly_equivalent(fmt, equiv) or \
                   should_ignore_for_mapping(equiv):
                    continue
                assert(fmt.srgb_equivalent == None)
                fmt.srgb_equivalent = equiv
        elif fmt.colorspace == SRGB:
            for equiv in ret:
                if equiv.colorspace != RGB or not mostly_equivalent(fmt, equiv) or \
                   should_ignore_for_mapping(equiv):
                    continue
                assert(fmt.linear_equivalent == None)
                fmt.linear_equivalent = equiv

    return ret
