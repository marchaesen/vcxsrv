# encoding=utf-8
#
# Usage:
#     gen_xmlpool.py /path/to/t_option.h localedir lang lang lang ...
#
# For each given language, this script expects to find a .mo file at
# `{localedir}/{language}/LC_MESSAGES/options.mo`.
#

from __future__ import print_function, unicode_literals
import argparse
import gettext
import io
import os
import re
import sys

if sys.version_info < (3, 0):
    gettext_method = 'ugettext'
else:
    gettext_method = 'gettext'

# Escape special characters in C strings
def escapeCString(s):
    escapeSeqs = {'\a' : '\\a', '\b' : '\\b', '\f' : '\\f', '\n' : '\\n',
                  '\r' : '\\r', '\t' : '\\t', '\v' : '\\v', '\\' : '\\\\'}
    # " -> '' is a hack. Quotes (") aren't possible in XML attributes.
    # Better use Unicode characters for typographic quotes in option
    # descriptions and translations.
    last_quote = '”'
    i = 0
    r = ''
    for c in s:
        # Special case: escape double quote with “ or ”, depending
        # on whether it's an open or close quote. This is needed because plain
        # double quotes are not possible in XML attributes.
        if c == '"':
            if last_quote == '”':
                q = '“'
            else:
                q = '”'
            last_quote = q
            r = r + q
        elif c in escapeSeqs:
            r = r + escapeSeqs[c]
        else:
            r = r + c
    return r

# Expand escape sequences in C strings (needed for gettext lookup)
def expandCString(s):
    escapeSeqs = {'a' : '\a', 'b' : '\b', 'f' : '\f', 'n' : '\n',
                  'r' : '\r', 't' : '\t', 'v' : '\v',
                  '"' : '"', '\\' : '\\'}
    escape = False
    hexa = False
    octa = False
    num = 0
    digits = 0
    r = u''
    for c in s:
        if not escape:
            if c == '\\':
                escape = True
            else:
                r = r + c
        elif hexa:
            if (c >= '0' and c <= '9') or \
               (c >= 'a' and c <= 'f') or \
               (c >= 'A' and c <= 'F'):
                num = num * 16 + int(c, 16)
                digits = digits + 1
            else:
                digits = 2
            if digits >= 2:
                hexa = False
                escape = False
                r = r + chr(num)
        elif octa:
            if c >= '0' and c <= '7':
                num = num * 8 + int(c, 8)
                digits = digits + 1
            else:
                digits = 3
            if digits >= 3:
                octa = False
                escape = False
                r = r + chr(num)
        else:
            if c in escapeSeqs:
                r = r + escapeSeqs[c]
                escape = False
            elif c >= '0' and c <= '7':
                octa = True
                num = int(c, 8)
                if num <= 3:
                    digits = 1
                else:
                    digits = 2
            elif c == 'x' or c == 'X':
                hexa = True
                num = 0
                digits = 0
            else:
                r = r + c
                escape = False
    return r

# Expand matches. The first match is always a DESC or DESC_BEGIN match.
# Subsequent matches are ENUM matches.
#
# DESC, DESC_BEGIN format: \1 \2=<lang> \3 \4=gettext(" \5=<text> \6=") \7
# ENUM format:             \1 \2=gettext(" \3=<text> \4=") \5
def expandMatches(matches, translations, outfile, end=None):
    assert len(matches) > 0
    nTranslations = len(translations)
    i = 0
    # Expand the description+enums for all translations
    for lang,trans in translations:
        i = i + 1
        # Make sure that all but the last line of a simple description
        # are extended with a backslash.
        suffix = ''
        if len(matches) == 1 and i < len(translations) and \
               not matches[0].expand(r'\7').endswith('\\'):
            suffix = ' \\'
        text = escapeCString(getattr(trans, gettext_method)(expandCString(
            matches[0].expand (r'\5'))))
        text = (matches[0].expand(r'\1' + lang + r'\3"' + text + r'"\7') + suffix)

        outfile.write(text + '\n')

        # Expand any subsequent enum lines
        for match in matches[1:]:
            text = escapeCString(getattr(trans, gettext_method)(expandCString(
                match.expand(r'\3'))))
            text = match.expand(r'\1"' + text + r'"\5')

            outfile.write(text + '\n')

        # Expand description end
        if end:
            outfile.write(end)

# Regular expressions:
reLibintl_h = re.compile(r'#\s*include\s*<libintl.h>')
reDESC = re.compile(r'(\s*DRI_CONF_DESC\s*\(\s*)([a-z]+)(\s*,\s*)(gettext\s*\(\s*")(.*)("\s*\))(\s*\)[ \t]*\\?)$')
reDESC_BEGIN = re.compile(r'(\s*DRI_CONF_DESC_BEGIN\s*\(\s*)([a-z]+)(\s*,\s*)(gettext\s*\(\s*")(.*)("\s*\))(\s*\)[ \t]*\\?)$')
reENUM = re.compile(r'(\s*DRI_CONF_ENUM\s*\([^,]+,\s*)(gettext\s*\(\s*")(.*)("\s*\))(\s*\)[ \t]*\\?)$')
reDESC_END = re.compile(r'\s*DRI_CONF_DESC_END')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--template', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--localedir', required=True)
    parser.add_argument('--languages', nargs='*', default=[])
    args = parser.parse_args()

    # Compile a list of translation classes to all supported languages.
    # The first translation is always a NullTranslations.
    translations = [("en", gettext.NullTranslations())]
    for lang in args.languages:
        try:
            filename = os.path.join(args.localedir, '{}.gmo'.format(lang))
            with io.open(filename, 'rb') as f:
                trans = gettext.GNUTranslations(f)
        except (IOError, OSError):
            print("Warning: language '%s' not found." % lang, file=sys.stderr)
            continue
        translations.append((lang, trans))

    with io.open(args.output, mode='wt', encoding='utf-8') as output:
        output.write("/* This is file is generated automatically. Don't edit!  */\n")

        # Process the options template and generate options.h with all
        # translations.
        with io.open(args.template, mode="rt", encoding='utf-8') as template:
            descMatches = []
            for line in template:
                if descMatches:
                    matchENUM = reENUM.match(line)
                    matchDESC_END = reDESC_END.match(line)
                    if matchENUM:
                        descMatches.append(matchENUM)
                    elif matchDESC_END:
                        expandMatches(descMatches, translations, output, line)
                        descMatches = []
                    else:
                        print("Warning: unexpected line inside description dropped:\n",
                              line, file=sys.stderr)
                    continue
                if reLibintl_h.search(line):
                    # Ignore (comment out) #include <libintl.h>
                    output.write("/* %s * commented out by gen_xmlpool.py */\n" % line)
                    continue
                matchDESC = reDESC.match(line)
                matchDESC_BEGIN = reDESC_BEGIN.match(line)
                if matchDESC:
                    assert not descMatches
                    expandMatches([matchDESC], translations, output)
                elif matchDESC_BEGIN:
                    assert not descMatches
                    descMatches = [matchDESC_BEGIN]
                else:

                    output.write(line)

        if descMatches:
            print("Warning: unterminated description at end of file.", file=sys.stderr)
            expandMatches(descMatches, translations, output)


if __name__ == '__main__':
    main()
