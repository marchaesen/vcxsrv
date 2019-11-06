#!/usr/bin/env python3
# Copyright © 2019 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Update the main page, release notes, and calendar."""

import argparse
import calendar
import datetime
import pathlib
from lxml import (
    etree,
    html,
)


def calculate_previous_version(version: str, is_point: bool) -> str:
    """Calculate the previous version to compare to.

    In the case of -rc to final that verison is the previous .0 release,
    (19.3.0 in the case of 20.0.0, for example). for point releases that is
    the last point release. This value will be the same as the input value
    for a poiont release, but different for a major release.
    """
    if '-' in version:
        version = version.split('-')[0]
    if is_point:
        return version
    base = version.split('.')
    if base[1] == '0':
        base[0] = str(int(base[0]) - 1)
        base[1] = '3'
    else:
        base[1] = str(int(base[1]) - 1)
    return '.'.join(base)


def is_point_release(version: str) -> bool:
    return not version.endswith('.0')


def update_index(is_point: bool, version: str, previous_version: str) -> None:
    p = pathlib.Path(__file__).parent.parent / 'docs' / 'index.html'
    with p.open('rt') as f:
        tree = html.parse(f)

    news = tree.xpath('.//h1')[0]

    date = datetime.date.today()
    month = calendar.month_name[date.month]
    header = etree.Element('h2')
    header.text = f"{month} {date.day}, {date.year}"

    body = etree.Element('p')
    a = etree.SubElement(
        body, 'a', attrib={'href': f'relnotes/{previous_version}.html'})
    a.text = f"Mesa {previous_version}"
    if is_point:
        a.tail = " is released. This is a bug fix release."
    else:
        a.tail = (" is released. This is a new development release. "
                  "See the release notes for mor information about this release.")

    root = news.getparent()
    index = root.index(news) + 1
    root.insert(index, body)
    root.insert(index, header)

    tree.write(p.as_posix(), method='html')


def update_release_notes(previous_version: str) -> None:
    p = pathlib.Path(__file__).parent.parent / 'docs' / 'relnotes.html'
    with p.open('rt') as f:
        tree = html.parse(f)

    li = etree.Element('li')
    a = etree.SubElement(li, 'a', href=f'relnotes/{previous_version}.html')
    a.text = f'{previous_version} release notes'

    ul = tree.xpath('.//ul')[0]
    ul.insert(0, li)

    tree.write(p.as_posix(), method='html')


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('version', help="The released version.")
    args = parser.parse_args()

    is_point = is_point_release(args.version)
    previous_version = calculate_previous_version(args.version, is_point)

    update_index(is_point, args.version, previous_version)
    update_release_notes(previous_version)


if __name__ == "__main__":
    main()
