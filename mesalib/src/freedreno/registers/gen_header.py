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

class Field(object):
	def __init__(self, name, low, high, shr, type, parser):
		self.name = name
		self.low = low
		self.high = high
		self.shr = shr
		self.type = type

		builtin_types = [ None, "boolean", "uint", "hex", "int", "fixed", "ufixed", "float" ]

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

	def ctype(self):
		if self.type == None:
			type = "uint32_t"
			val = "val"
		elif self.type == "boolean":
			type = "bool"
			val = "val"
		elif self.type == "uint" or self.type == "hex":
			type = "uint32_t"
			val = "val"
		elif self.type == "int":
			type = "int32_t"
			val = "val"
		elif self.type == "fixed":
			type = "float"
			val = "((int32_t)(val * %d.0))" % (1 << self.radix)
		elif self.type == "ufixed":
			type = "float"
			val = "((uint32_t)(val * %d.0))" % (1 << self.radix)
		elif self.type == "float" and self.high - self.low == 31:
			type = "float"
			val = "fui(val)"
		elif self.type == "float" and self.high - self.low == 15:
			type = "float"
			val = "util_float_to_half(val)"
		else:
			type = "enum %s" % self.type
			val = "val"

		if self.shr > 0:
			val = "%s >> %d" % (val, self.shr)

		return (type, val)

def tab_to(name, value):
	tab_count = (68 - (len(name) & ~7)) // 8
	if tab_count == 0:
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
				type, val = f.ctype()

				print("static inline uint32_t %s(%s val)\n{" % (name, type))
				if f.shr > 0:
					print("\tassert(!(val & 0x%x));" % mask(0, f.shr - 1))
				print("\treturn ((%s) << %s__SHIFT) & %s__MASK;\n}" % (val, name, name))

class Array(object):
	def __init__(self, attrs, domain):
		self.name = attrs["name"]
		self.domain = domain
		self.offset = int(attrs["offset"], 0)
		self.stride = int(attrs["stride"], 0)
		self.length = int(attrs["length"], 0)

	def dump(self):
		print("static inline uint32_t REG_%s_%s(uint32_t i0) { return 0x%08x + 0x%x*i0; }\n" % (self.domain, self.name, self.offset, self.stride))

class Reg(object):
	def __init__(self, attrs, domain, array):
		self.name = attrs["name"]
		self.domain = domain
		self.array = array
		self.offset = int(attrs["offset"], 0)
		self.type = None

	def dump(self):
		if self.array:
			name = self.domain + "_" + self.array.name + "_" + self.name
			offset = self.array.offset + self.offset
			print("static inline uint32_t REG_%s(uint32_t i0) { return 0x%08x + 0x%x*i0; }" % (name, offset, self.array.stride))
		else:
			name = self.domain + "_" + self.name
			tab_to("#define REG_%s" % name, "0x%08x" % self.offset)

		if self.bitset.inline:
			self.bitset.dump(name)
		print("")
		
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

	def parse(self, filename):
		self.path = os.path.dirname(filename)
		self.stack = []
		self.do_parse(filename)

	def start_element(self, name, attrs):
		if name == "import":
			filename = os.path.basename(attrs["file"])
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
			if "type" in attrs and attrs["type"] in self.bitsets:
				self.current_bitset = self.bitsets[attrs["type"]]
			else:
				self.current_bitset = Bitset(attrs["name"], None)
				self.current_bitset.inline = True
				if "type" in attrs:
					self.parse_field(None, attrs)

			self.current_reg = Reg(attrs, self.prefix(), self.current_array)
			self.current_reg.bitset = self.current_bitset

			if len(self.stack) == 1:
				self.file.append(self.current_reg)
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

def main():
	p = Parser()
	xml_file = sys.argv[1]

	guard = str.replace(os.path.basename(xml_file), '.', '_').upper()
	print("#ifndef %s\n#define %s\n" % (guard, guard))

	try:
		p.parse(xml_file)
	except Error as e:
		print(e)
		exit(1)

	p.dump()

	print("\n#endif /* %s */" % guard)

if __name__ == '__main__':
	main()
