#!/usr/bin/env python3
#
# Converts the YAML format from the layout tester into JUnit XML
#
# This file is formatted with Python Black

import argparse
import pathlib
import sys
from xml.dom import minidom

import yaml

parser = argparse.ArgumentParser(description="Converts YAML to JUnit XML")
parser.add_argument(
    "inputfile",
    type=pathlib.Path,
    help="The YAML output file from the keyboard layout tester",
)
parser.add_argument(
    "--additional-successful-tests",
    type=int,
    default=0,
    help="Number of successful tests from another source",
)
args = parser.parse_args()
if not args.inputfile.exists():
    print(f"No such file: {args.inputfile}")
    sys.exit(0)

with args.inputfile.open() as fd:
    yml = yaml.safe_load(fd)

    # Ensure there is a yaml document
    if yml is None:
        yml = yaml.safe_load("[]")

    doc = minidom.Document()
    suite = doc.createElement("testsuite")
    suite.setAttribute("name", "XKB layout compilation tests")
    doc.appendChild(suite)

    # JUnit differs between test case failures
    # and errors (something else blew up)
    # We use failures for unrecognized keysyms and errors
    # for everything else (i.e. keymap compilation errors)
    ntests, nfailures, nerrors = args.additional_successful_tests, 0, 0

    for testcase in yml:
        ntests += 1
        node = doc.createElement("testcase")
        node.setAttribute("classname", f"{testcase['rmlvo'][0]} rules layout test")
        # We don't care about rules and model here, LVO is enough
        r, m, l, v, o = testcase["rmlvo"]
        if v:
            name = f"{l}({v})"
        else:
            name = l
        if o:
            name += f", {o}"
        node.setAttribute("name", f"keymap compilation: {name}")
        suite.appendChild(node)

        if testcase["status"] != 0:
            f = None
            if testcase["status"] == 99:  # missing keysym
                nfailures += 1
                f = doc.createElement("failure")
            else:  # everything else is an error
                nerrors += 1
                f = doc.createElement("error")
            f.setAttribute("message", testcase["error"])
            cdata = doc.createCDATASection(
                f"Error message: {testcase['error']} in command {testcase['cmd']}"
            )
            f.appendChild(cdata)
            node.appendChild(f)

    suite.setAttribute("tests", str(ntests))
    suite.setAttribute("errors", str(nerrors))
    suite.setAttribute("failures", str(nfailures))

    print(doc.toprettyxml(indent="  "))
