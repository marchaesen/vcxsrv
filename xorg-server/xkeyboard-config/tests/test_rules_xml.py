#!/usr/bin/env python3
#
# Call with pytest. Requires XKB_CONFIG_ROOT to be set

import os
import pytest
from pathlib import Path
import xml.etree.ElementTree as ET


def _xkb_config_root():
    path = os.getenv('XKB_CONFIG_ROOT')
    assert path is not None, 'Environment variable XKB_CONFIG_ROOT must be set'
    print(f'Using {path}')

    xkbpath = Path(path)
    assert (xkbpath / 'rules').exists(), f'{path} is not an XKB installation'
    return xkbpath


@pytest.fixture
def xkb_config_root():
    return _xkb_config_root()


def pytest_generate_tests(metafunc):
    # for any test_foo function with an argument named rules_xml,
    # make it the list of XKB_CONFIG_ROOT/rules/*.xml files.
    if 'rules_xml' in metafunc.fixturenames:

        rules_xml = list(_xkb_config_root().glob('rules/*.xml'))
        assert rules_xml
        metafunc.parametrize('rules_xml', rules_xml)


def prettyxml(element):
    return ET.tostring(element).decode('utf-8')


class ConfigItem:
    def __init__(self, name, shortDescription=None, description=None):
        self.name = name
        self.shortDescription = shortDescription
        self.description = description

    @classmethod
    def _fetch_subelement(cls, parent, name):
        sub_element = parent.findall(name)
        if sub_element is not None and len(sub_element) == 1:
            return sub_element[0]
        else:
            return None

    @classmethod
    def _fetch_text(cls, parent, name):
        sub_element = cls._fetch_subelement(parent, name)
        if sub_element is None:
            return None
        return sub_element.text

    @classmethod
    def from_elem(cls, elem):
        try:
            ci_element = cls._fetch_subelement(elem, 'configItem')
            name = cls._fetch_text(ci_element, 'name')
            assert name is not None
            # shortDescription and description are optional
            sdesc = cls._fetch_text(ci_element, 'shortDescription')
            desc = cls._fetch_text(ci_element, 'description')
            return ConfigItem(name, sdesc, desc)
        except AssertionError as e:
            endl = "\n"  # f{} cannot contain backslashes
            e.args = (f'\nFor element {prettyxml(elem)}\n{endl.join(e.args)}',)
            raise


def test_duplicate_layouts(rules_xml):
    tree = ET.parse(rules_xml)
    root = tree.getroot()
    layouts = {}
    for layout in root.iter('layout'):
        ci = ConfigItem.from_elem(layout)
        assert ci.name not in layouts, f'Duplicate layout {ci.name}'
        layouts[ci.name] = True

        variants = {}
        for variant in layout.iter('variant'):
            vci = ConfigItem.from_elem(variant)
            assert vci.name not in variants, \
                f'{rules_xml}: duplicate variant {ci.name}({vci.name}):\n{prettyxml(variant)}'
            variants[vci.name] = True


def test_duplicate_models(rules_xml):
    tree = ET.parse(rules_xml)
    root = tree.getroot()
    models = {}
    for model in root.iter('model'):
        ci = ConfigItem.from_elem(model)
        assert ci.name not in models, f'Duplicate model {ci.name}'
        models[ci.name] = True
