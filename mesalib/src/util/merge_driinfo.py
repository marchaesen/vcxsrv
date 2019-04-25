#
# Copyright 2017 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
# THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.

"""
usage: merge_driinfo.py <list of input files>

Generates a source file which contains the DRI_CONF_xxx macros for generating
the driinfo XML that describes the available DriConf options for a driver and
its supported state trackers, based on the merged information from the input
files.
"""

from __future__ import print_function

import mako.template
import re
import sys


# Some regexps used during input parsing
RE_section_begin = re.compile(r'DRI_CONF_SECTION_(.*)')
RE_option = re.compile(r'DRI_CONF_(.*)\((.*)\)')


class Option(object):
   """
   Represent a config option as:
    * name: the xxx part of the DRI_CONF_xxx macro
    * defaults: the defaults parameters that are passed into the macro
   """
   def __init__(self, name, defaults):
      self.name = name
      self.defaults = defaults


class Verbatim(object):
   """
   Represent a chunk of code that is copied into the result file verbatim.
   """
   def __init__(self):
      self.string = ''


class Section(object):
   """
   Represent a config section description as:
    * name: the xxx part of the DRI_CONF_SECTION_xxx macro
    * options: list of options
   """
   def __init__(self, name):
      self.name = name
      self.options = []


def parse_inputs(input_filenames):
   success = True
   sections_lists = []

   for input_filename in input_filenames:
      with open(input_filename, 'r') as infile:
         sections = []
         sections_lists.append(sections)

         section = None

         linenum = 0
         verbatim = None
         for line in infile:
            linenum += 1

            if line.startswith('//= BEGIN VERBATIM'):
               if verbatim is not None:
                  print('{}:{}: nested verbatim'
                        .format(input_filename, linenum))
                  success = False
                  continue
               verbatim = Verbatim()

            if verbatim is not None:
               verbatim.string += line

               if line.startswith('//= END VERBATIM'):
                  if section is None:
                     sections.append(verbatim)
                  else:
                     section.options.append(verbatim)
                  verbatim = None
               continue

            line = line.strip()
            if not line:
               continue

            if line.startswith('//'):
               continue

            if line == 'DRI_CONF_SECTION_END':
               if section is None:
                  print('{}:{}: no open section'
                        .format(input_filename, linenum))
                  success = False
                  continue
               section = None
               continue

            m = RE_section_begin.match(line)
            if m:
               if section is not None:
                  print('{}:{}: nested sections are not supported'
                        .format(input_filename, linenum))
                  success = False
                  continue
               if sections is None:
                  print('{}:{}: missing DRIINFO line'
                        .format(input_filename, linenum))
                  success = False
                  break # parsing the rest really makes no sense
               section = Section(m.group(1))
               sections.append(section)
               continue

            m = RE_option.match(line)
            if m:
               if section is None:
                  print('{}:{}: no open section'
                        .format(input_filename, linenum))
                  success = False
                  break
               section.options.append(Option(m.group(1), m.group(2)))
               continue

            print('{}:{}: do not understand this line'
                  .format(input_filename, linenum))
            success = False

         if section is not None:
            print('{}:end-of-file: missing end of section'
                  .format(input_filename))
            success = False

   if success:
      return sections_lists
   return None


def merge_sections(section_list):
   """
   section_list: list of Section objects to be merged, all of the same name
   Return a merged Section object (everything is deeply copied)
   """
   merged_section = Section(section_list[0].name)

   for section in section_list:
      assert section.name == merged_section.name

      for orig_option in section.options:
         if isinstance(orig_option, Option):
            for merged_option in merged_section.options:
               if not isinstance(merged_option, Option):
                  continue
               if orig_option.name == merged_option.name:
                  merged_option.defaults = orig_option.defaults
                  break
            else:
               merged_section.options.append(Option(orig_option.name, orig_option.defaults))
         else:
            merged_section.options.append(orig_option)

   return merged_section


def merge_sections_lists(sections_lists):
   """
   sections_lists: list of lists of Section objects to be merged
   Return a merged list of merged Section objects; everything is deeply copied.
   Default values for options in later lists override earlier default values.
   """
   merged_sections = []

   for idx,sections in enumerate(sections_lists):
      for base_section in sections:
         if not isinstance(base_section, Section):
            merged_sections.append(base_section)
            continue

         original_sections = [base_section]
         for next_sections in sections_lists[idx+1:]:
            for j,section in enumerate(next_sections):
               if section.name == base_section.name:
                  original_sections.append(section)
                  del next_sections[j]
                  break

         merged_section = merge_sections(original_sections)

         merged_sections.append(merged_section)

   return merged_sections


def main(input_filenames):
   sections_lists = parse_inputs(input_filenames)
   if sections_lists is None:
      return False

   merged_sections_list = merge_sections_lists(sections_lists)

   driinfo_h_template = mako.template.Template("""\
// DO NOT EDIT - this file is automatically generated by merge_driinfo.py

/*
Use as:

#include "xmlpool.h"

static const char driinfo_xml[] =
#include "this_file"
;
*/

DRI_CONF_BEGIN
% for section in sections:
% if isinstance(section, Section):
   DRI_CONF_SECTION_${section.name}
% for option in section.options:
% if isinstance(option, Option):
      DRI_CONF_${option.name}(${option.defaults})
% else:
${option.string}
% endif
% endfor
   DRI_CONF_SECTION_END
% else:
${section.string}
% endif
% endfor
DRI_CONF_END""")

   print(driinfo_h_template.render(sections=merged_sections_list, Section=Section, Option=Option))
   return True


if __name__ == '__main__':
   if len(sys.argv) <= 1:
      print('Missing arguments')
      sys.exit(1)

   if not main(sys.argv[1:]):
      sys.exit(1)
