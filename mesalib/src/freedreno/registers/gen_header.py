#!/usr/bin/python3

import xml.parsers.expat
import sys
import os

class Error(Exception):
	def __init__(self, message):
		self.message = message

class Enum(object):
	def __init__(self, name):
		self.name = name
		self.values = []

	def dump(self):
		prev = 0
		use_hex = False
		for (name, value) in self.values:
			if value > 0x1000:
				use_hex = True

		print("enum %s {" % self.name)
		for (name, value) in self.values:
			if use_hex:
				print("\t%s = 0x%08x," % (name, value))
			else:
				print("\t%s = %d," % (name, value))
		print("};\n")

	def dump_pack_struct(self):
		pass

class Field(object):
	def __init__(self, name, low, high, shr, type, parser):
		self.name = name
		self.low = low
		self.high = high
		self.shr = shr
		self.type = type

		builtin_types = [ None, "a3xx_regid", "boolean", "uint", "hex", "int", "fixed", "ufixed", "float", "address", "waddress" ]

		if low < 0 or low > 31:
			raise parser.error("low attribute out of range: %d" % low)
		if high < 0 or high > 31:
			raise parser.error("high attribute out of range: %d" % high)
		if high < low:
			raise parser.error("low is greater than high: low=%d, high=%d" % (low, high))
		if self.type == "boolean" and not low == high:
			raise parser.error("booleans should be 1 bit fields");
		elif self.type == "float" and not (high - low == 31 or high - low == 15):
			raise parser.error("floats should be 16 or 32 bit fields")
		elif not self.type in builtin_types and not self.type in parser.enums:
			raise parser.error("unknown type '%s'" % self.type);

	def ctype(self, var_name):
		if self.type == None:
			type = "uint32_t"
			val = var_name
		elif self.type == "boolean":
			type = "bool"
			val = var_name
		elif self.type == "uint" or self.type == "hex" or self.type == "a3xx_regid":
			type = "uint32_t"
			val = var_name
		elif self.type == "int":
			type = "int32_t"
			val = var_name
		elif self.type == "fixed":
			type = "float"
			val = "((int32_t)(%s * %d.0))" % (var_name, 1 << self.radix)
		elif self.type == "ufixed":
			type = "float"
			val = "((uint32_t)(%s * %d.0))" % (var_name, 1 << self.radix)
		elif self.type == "float" and self.high - self.low == 31:
			type = "float"
			val = "fui(%s)" % var_name
		elif self.type == "float" and self.high - self.low == 15:
			type = "float"
			val = "_mesa_float_to_half(%s)" % var_name
		elif self.type in [ "address", "waddress" ]:
			type = "uint64_t"
			val = var_name
		else:
			type = "enum %s" % self.type
			val = var_name

		if self.shr > 0:
			val = "(%s >> %d)" % (val, self.shr)

		return (type, val)

def tab_to(name, value):
	tab_count = (68 - (len(name) & ~7)) // 8
	if tab_count <= 0:
		tab_count = 1
	print(name + ('\t' * tab_count) + value)

def mask(low, high):
	return ((0xffffffff >> (32 - (high + 1 - low))) << low)

class Bitset(object):
	def __init__(self, name, template):
		self.name = name
		self.inline = False
		if template:
			self.fields = template.fields
		else:
			self.fields = []

	def dump_pack_struct(self, prefix=None, array=None):
		def field_name(prefix, name):
			if f.name:
				name = f.name.lower()
			else:
				name = prefix.lower()

			if (name in [ "double", "float", "int" ]) or not (name[0].isalpha()):
					name = "_" + name

			return name

		if not prefix:
			return
		if prefix == None:
			prefix = self.name

		value_name = "dword"
		print("struct %s {" % prefix)
		for f in self.fields:
			if f.type == "waddress":
				value_name = "qword"
			if f.type in [ "address", "waddress" ]:
				tab_to("    __bo_type", "bo;")
				tab_to("    uint32_t", "bo_offset;")
				continue
			name = field_name(prefix, f.name)

			type, val = f.ctype("var")

			tab_to("    %s" % type, "%s;" % name)
		if value_name == "qword":
			tab_to("    uint64_t", "unknown;")
			tab_to("    uint64_t", "qword;")
		else:
			tab_to("    uint32_t", "unknown;")
			tab_to("    uint32_t", "dword;")
		print("};\n")

		address = None;
		for f in self.fields:
			if f.type in [ "address", "waddress" ]:
				address = f
		if array:
			print("static inline struct fd_reg_pair\npack_%s(uint32_t i, struct %s fields)\n{" %
				  (prefix, prefix));
		else:
			print("static inline struct fd_reg_pair\npack_%s(struct %s fields)\n{" %
				  (prefix, prefix));

		print("#ifndef NDEBUG")
		known_mask = 0
		for f in self.fields:
			known_mask |= mask(f.low, f.high)
			if f.type in [ "boolean", "address", "waddress" ]:
				continue
			type, val = f.ctype("fields.%s" % field_name(prefix, f.name))
			print("    assert((%-40s & 0x%08x) == 0);" % (val, 0xffffffff ^ mask(0 , f.high - f.low)))
		print("    assert((%-40s & 0x%08x) == 0);" % ("fields.unknown", known_mask))
		print("#endif\n")

		print("    return (struct fd_reg_pair) {")
		if array:
			print("        .reg = REG_%s(i)," % prefix)
		else:
			print("        .reg = REG_%s," % prefix)

		print("        .value =")
		for f in self.fields:
			if f.type in [ "address", "waddress" ]:
				continue
			else:
				type, val = f.ctype("fields.%s" % field_name(prefix, f.name))
				print("            (%-40s << %2d) |" % (val, f.low))
		print("            fields.unknown | fields.%s," % (value_name,))

		if address:
			print("        .is_address = true,")
			print("        .bo = fields.bo,")
			if f.type == "waddress":
				print("        .bo_write = true,")
			print("        .bo_offset = fields.bo_offset,")
			print("        .bo_shift = %d" % address.shr)

		print("    };\n}\n")

		if address:
			skip = ", { .reg = 0 }"
		else:
			skip = ""

		if array:
			print("#define %s(i, ...) pack_%s(i, (struct %s) { __VA_ARGS__ })%s\n" %
				  (prefix, prefix, prefix, skip))
		else:
			print("#define %s(...) pack_%s((struct %s) { __VA_ARGS__ })%s\n" %
				  (prefix, prefix, prefix, skip))


	def dump(self, prefix=None):
		if prefix == None:
			prefix = self.name
		for f in self.fields:
			if f.name:
				name = prefix + "_" + f.name
			else:
				name = prefix

			if not f.name and f.low == 0 and f.shr == 0 and not f.type in ["float", "fixed", "ufixed"]:
				pass
			elif f.type == "boolean" or (f.type == None and f.low == f.high):
				tab_to("#define %s" % name, "0x%08x" % (1 << f.low))
			else:
				tab_to("#define %s__MASK" % name, "0x%08x" % mask(f.low, f.high))
				tab_to("#define %s__SHIFT" % name, "%d" % f.low)
				type, val = f.ctype("val")

				print("static inline uint32_t %s(%s val)\n{" % (name, type))
				if f.shr > 0:
					print("\tassert(!(val & 0x%x));" % mask(0, f.shr - 1))
				print("\treturn ((%s) << %s__SHIFT) & %s__MASK;\n}" % (val, name, name))
		print()

class Array(object):
	def __init__(self, attrs, domain):
		if "name" in attrs:
			self.name = attrs["name"]
		else:
			self.name = ""
		self.domain = domain
		self.offset = int(attrs["offset"], 0)
		self.stride = int(attrs["stride"], 0)
		self.length = int(attrs["length"], 0)

	def dump(self):
		print("static inline uint32_t REG_%s_%s(uint32_t i0) { return 0x%08x + 0x%x*i0; }\n" % (self.domain, self.name, self.offset, self.stride))

	def dump_pack_struct(self):
		pass

class Reg(object):
	def __init__(self, attrs, domain, array, bit_size):
		self.name = attrs["name"]
		self.domain = domain
		self.array = array
		self.offset = int(attrs["offset"], 0)
		self.type = None
		self.bit_size = bit_size

		if self.array:
			self.full_name = self.domain + "_" + self.array.name + "_" + self.name
		else:
			self.full_name = self.domain + "_" + self.name

	def dump(self):
		if self.array:
			offset = self.array.offset + self.offset
			print("static inline uint32_t REG_%s(uint32_t i0) { return 0x%08x + 0x%x*i0; }" % (self.full_name, offset, self.array.stride))
		else:
			tab_to("#define REG_%s" % self.full_name, "0x%08x" % self.offset)

		if self.bitset.inline:
			self.bitset.dump(self.full_name)
		print("")

	def dump_pack_struct(self):
		if self.bitset.inline:
			self.bitset.dump_pack_struct(self.full_name, not self.array == None)


def parse_variants(attrs):
		if not "variants" in attrs:
				return None
		variant = attrs["variants"].split(",")[0]
		if "-" in variant:
			variant = variant[:variant.index("-")]

		return variant

class Parser(object):
	def __init__(self):
		self.current_array = None
		self.current_domain = None
		self.current_prefix = None
		self.current_stripe = None
		self.current_bitset = None
		self.bitsets = {}
		self.enums = {}
		self.file = []

	def error(self, message):
		parser, filename = self.stack[-1]
		return Error("%s:%d:%d: %s" % (filename, parser.CurrentLineNumber, parser.CurrentColumnNumber, message))

	def prefix(self):
		if self.current_stripe:
			return self.current_stripe + "_" + self.current_domain
		elif self.current_prefix:
			return self.current_prefix + "_" + self.current_domain
		else:
			return self.current_domain

	def parse_field(self, name, attrs):
		try:
			if "pos" in attrs:
				high = low = int(attrs["pos"], 0)
			elif "high" in attrs and "low" in attrs:
				high = int(attrs["high"], 0)
				low = int(attrs["low"], 0)
			else:
				low = 0
				high = 31

			if "type" in attrs:
				type = attrs["type"]
			else:
				type = None
	
			if "shr" in attrs:
				shr = int(attrs["shr"], 0)
			else:
				shr = 0

			b = Field(name, low, high, shr, type, self)

			if type == "fixed" or type == "ufixed":
				b.radix = int(attrs["radix"], 0)

			self.current_bitset.fields.append(b)
		except ValueError as e:
			raise self.error(e);

	def do_parse(self, filename):
		file = open(filename, "rb")
		parser = xml.parsers.expat.ParserCreate()
		self.stack.append((parser, filename))
		parser.StartElementHandler = self.start_element
		parser.EndElementHandler = self.end_element
		parser.ParseFile(file)
		self.stack.pop()
		file.close()

	def parse(self, rnn_path, filename):
		self.path = rnn_path
		self.stack = []
		self.do_parse(filename)

	def parse_reg(self, attrs, bit_size):
		if "type" in attrs and attrs["type"] in self.bitsets:
			self.current_bitset = self.bitsets[attrs["type"]]
		else:
			self.current_bitset = Bitset(attrs["name"], None)
			self.current_bitset.inline = True
			if "type" in attrs:
				self.parse_field(None, attrs)

		self.current_reg = Reg(attrs, self.prefix(), self.current_array, bit_size)
		self.current_reg.bitset = self.current_bitset

		if len(self.stack) == 1:
			self.file.append(self.current_reg)

	def start_element(self, name, attrs):
		if name == "import":
			filename = attrs["file"]
			self.do_parse(os.path.join(self.path, filename))
		elif name == "domain":
			self.current_domain = attrs["name"]
			if "prefix" in attrs and attrs["prefix"] == "chip":
				self.current_prefix = parse_variants(attrs)
		elif name == "stripe":
			self.current_stripe = parse_variants(attrs)
		elif name == "enum":
			self.current_enum_value = 0
			self.current_enum = Enum(attrs["name"])
			self.enums[attrs["name"]] = self.current_enum
			if len(self.stack) == 1:
				self.file.append(self.current_enum)
		elif name == "value":
			if "value" in attrs:
				value = int(attrs["value"], 0)
			else:
				value = self.current_enum_value
			self.current_enum.values.append((attrs["name"], value))
			# self.current_enum_value = value + 1
		elif name == "reg32":
			self.parse_reg(attrs, 32)
		elif name == "reg64":
			self.parse_reg(attrs, 64)
		elif name == "array":
			self.current_array = Array(attrs, self.prefix())
			if len(self.stack) == 1:
				self.file.append(self.current_array)
		elif name == "bitset":
			self.current_bitset = Bitset(attrs["name"], None)
			if "inline" in attrs and attrs["inline"] == "yes":
				self.current_bitset.inline = True
			self.bitsets[self.current_bitset.name] = self.current_bitset
			if len(self.stack) == 1 and not self.current_bitset.inline:
				self.file.append(self.current_bitset)
		elif name == "bitfield" and self.current_bitset:
			self.parse_field(attrs["name"], attrs)

	def end_element(self, name):
		if name == "domain":
			self.current_domain = None
			self.current_prefix = None
		elif name == "stripe":
			self.current_stripe = None
		elif name == "bitset":
			self.current_bitset = None
		elif name == "reg32":
			self.current_reg = None
		elif name == "array":
			self.current_array = None;
		elif name == "enum":
			self.current_enum = None

	def dump(self):
		enums = []
		bitsets = []
		regs = []
		for e in self.file:
			if isinstance(e, Enum):
				enums.append(e)
			elif isinstance(e, Bitset):
				bitsets.append(e)
			else:
				regs.append(e)

		for e in enums + bitsets + regs:
			e.dump()

	def dump_structs(self):
		for e in self.file:
			e.dump_pack_struct()


def main():
	p = Parser()
	rnn_path = sys.argv[1]
	xml_file = sys.argv[2]
	if len(sys.argv) > 3 and sys.argv[3] == '--pack-structs':
		do_structs = True
		guard = str.replace(os.path.basename(xml_file), '.', '_').upper() + '_STRUCTS'
	else:
		do_structs = False
		guard = str.replace(os.path.basename(xml_file), '.', '_').upper()

	print("#ifndef %s\n#define %s\n" % (guard, guard))

	try:
		p.parse(rnn_path, xml_file)
	except Error as e:
		print(e)
		exit(1)

	if do_structs:
		p.dump_structs()
	else:
		p.dump()

	print("\n#endif /* %s */" % guard)

if __name__ == '__main__':
	main()
