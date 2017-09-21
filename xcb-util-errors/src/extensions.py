#!/usr/bin/env python
import sys
from xml.etree.cElementTree import parse

class Module(object):
    def __init__(self, filename):
        self.errors = {}
        self.requests = {}
        self.events = {}
        self.xge_events = {}

        root = parse(filename).getroot()
        if root.get("header") == "xproto":
            self.xname = "xproto"
            self.name = "xproto"
            self.is_ext = False
        else:
            self.xname = root.get("extension-xname")
            self.name = root.get("extension-name")
            self.is_ext = True

        for elt in list(root):
            tag = elt.tag
            if tag == "error" or tag == "errorcopy" or tag == "event" or tag == "eventcopy":
                name = elt.get("name")
                number = int(elt.get("number"))
                if tag == "error" or tag == "errorcopy":
                    self.errors[number] = name
                else:
                    is_xge = elt.get("xge") == "true"
                    if tag == "eventcopy" and elt.get("ref") in self.xge_events.values():
                        is_xge = True
                    if is_xge:
                        self.xge_events[number] = name
                    else:
                        self.events[number] = name
            elif tag == "request":
                name = elt.get("name")
                opcode = int(elt.get("opcode"))
                self.requests[opcode] = name

        # Special case for XKB: It does its own event multiplexing, but this
        # library internally pretends it uses XGE.
        if self.name == "xkb":
            self.xge_events = self.events
            self.events = { 0: "XKB base event" }

        if not self.is_ext:
            self.events[35] = "GeGeneric"
            self.xge_events = {}

        self.errors_table = self.handle_type("error", self.errors)
        self.requests_table = self.handle_type("request", self.requests)
        self.events_table = self.handle_type("event", self.events)
        self.xge_events_table = self.handle_type("xge event", self.xge_events)

    def handle_type(self, kind, entries):
        # Do we have any entries at all?
        if not entries:
            return

        num_entries = 1 + max(num for num in entries)
        if not self.is_ext:
            num_entries = 256
        names = [ "Unknown (" + str(i) + ")" for i in range(0, num_entries)]
        for key in entries:
            if key < 0:
                print("%s: Ignoring invalid %s %s (%d)" % (self.name, kind, entries[key], key))
            else:
                names[key] = entries[key]
        return names

modules = []
xproto = None
def parseFile(filename):
    global xproto
    mod = Module(filename)
    if mod.is_ext:
        modules.append(mod)
    else:
        assert xproto == None
        xproto = mod

# Parse the xml file
output_file = sys.argv[1]
for input_file in sys.argv[2:]:
    parseFile(input_file)

assert xproto != None

output = open(output_file, "w")
output.write("/* Auto-generated file, do not edit */\n")
output.write("#include \"errors.h\"\n")
output.write("#include <string.h>\n")
output.write("\n")

def format_strings(name, table):
    if table is None:
        output.write("\t.num_%s = 0,\n" % name)
        output.write("\t.strings_%s = NULL,\n" % name)
    else:
        if len(table) == 256:
            # This must be xproto and the value isn't used, so instead use
            # something that fits into uint8_t.
            output.write("\t.num_%s = 0,\n" % (name))
        else:
            output.write("\t.num_%s = %d,\n" % (name, len(table)))
        output.write("\t.strings_%s = \"%s\\0\",\n" % (name, "\\0".join(table)))

def emit_module(module):
    t = ""
    prefix = "extension_"
    if module.is_ext:
        t = "static "
    else:
        prefix = ""
    output.write("%sconst struct static_extension_info_t %s%s_info = { // %s\n" % (t, prefix, module.name, module.xname))
    format_strings("minor", module.requests_table)
    format_strings("events", module.events_table)
    format_strings("xge_events", module.xge_events_table)
    format_strings("errors", module.errors_table)
    output.write("\t.name = \"%s\",\n" % module.name)
    output.write("};\n\n")

for module in modules:
    emit_module(module)
emit_module(xproto)

output.write("int register_extensions(xcb_errors_context_t *ctx, xcb_connection_t *conn)\n");
output.write("{\n");
output.write("\txcb_query_extension_cookie_t cookies[%d];\n" % len(modules));
output.write("\tint ret = 0;\n");
for idx in range(len(modules)):
    output.write("\tcookies[%d] = xcb_query_extension_unchecked(conn, strlen(\"%s\"), \"%s\");\n" % (idx, modules[idx].xname, modules[idx].xname));
for idx in range(len(modules)):
    output.write("\tret |= register_extension(ctx, conn, cookies[%d], &extension_%s_info);\n" % (idx, modules[idx].name));
output.write("\treturn ret;\n");
output.write("}\n");

output.close()
