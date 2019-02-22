#!/usr/bin/env python3

from os import get_terminal_size
from textwrap import wrap
from mesonbuild import coredata
from mesonbuild import optinterpreter

(COLUMNS, _) = get_terminal_size()

def describe_option(option_name: str, option_default_value: str,
                    option_type: str, option_message: str) -> None:
    print('name:    ' + option_name)
    print('default: ' + option_default_value)
    print('type:    ' + option_type)
    for line in wrap(option_message, width=COLUMNS - 9):
        print('         ' + line)
    print('---')

oi = optinterpreter.OptionInterpreter('')
oi.process('meson_options.txt')

for (name, value) in oi.options.items():
    if isinstance(value, coredata.UserStringOption):
        describe_option(name,
                        value.value,
                        'string',
                        "You can type what you want, but make sure it makes sense")
    elif isinstance(value, coredata.UserBooleanOption):
        describe_option(name,
                        'true' if value.value else 'false',
                        'boolean',
                        "You can set it to 'true' or 'false'")
    elif isinstance(value, coredata.UserIntegerOption):
        describe_option(name,
                        str(value.value),
                        'integer',
                        "You can set it to any integer value between '{}' and '{}'".format(value.min_value, value.max_value))
    elif isinstance(value, coredata.UserUmaskOption):
        describe_option(name,
                        str(value.value),
                        'umask',
                        "You can set it to 'preserve' or a value between '0000' and '0777'")
    elif isinstance(value, coredata.UserComboOption):
        choices = '[' + ', '.join(["'" + v + "'" for v in value.choices]) + ']'
        describe_option(name,
                        value.value,
                        'combo',
                        "You can set it to any one of those values: " + choices)
    elif isinstance(value, coredata.UserArrayOption):
        choices = '[' + ', '.join(["'" + v + "'" for v in value.choices]) + ']'
        value = '[' + ', '.join(["'" + v + "'" for v in value.value]) + ']'
        describe_option(name,
                        value,
                        'array',
                        "You can set it to one or more of those values: " + choices)
    elif isinstance(value, coredata.UserFeatureOption):
        describe_option(name,
                        value.value,
                        'feature',
                        "You can set it to 'auto', 'enabled', or 'disabled'")
    else:
        print(name + ' is an option of a type unknown to this script')
        print('---')
