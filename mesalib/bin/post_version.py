#!/usr/bin/env python3
# Copyright © 2019-2020 Intel Corporation

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
import subprocess

from lxml import (
    etree,
    html,
)


def is_point_release(version: str) -> bool:
    return not version.endswith('.0')


def update_index(is_point: bool, version: str) -> None:
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
        body, 'a', attrib={'href': f'relnotes/{version}.html'})
    a.text = f"Mesa {version}"
    if is_point:
        a.tail = " is released. This is a bug fix release."
    else:
        a.tail = (" is released. This is a new development release. "
                  "See the release notes for more information about this release.")

    root = news.getparent()
    index = root.index(news) + 1
    root.insert(index, body)
    root.insert(index, header)

    tree.write(p.as_posix(), method='html', pretty_print=True)
    subprocess.run(['git', 'add', p])


def update_release_notes(version: str) -> None:
    p = pathlib.Path(__file__).parent.parent / 'docs' / 'relnotes.html'
    with p.open('rt') as f:
        tree = html.parse(f)

    li = etree.Element('li')
    a = etree.SubElement(li, 'a', href=f'relnotes/{version}.html')
    a.text = f'{version} release notes'

    ul = tree.xpath('.//ul')[0]
    ul.insert(0, li)

    tree.write(p.as_posix(), method='html', pretty_print=True)
    subprocess.run(['git', 'add', p])


def update_calendar(version: str) -> None:
    p = pathlib.Path(__file__).parent.parent / 'docs' / 'release-calendar.html'
    with p.open('rt') as f:
        tree = html.parse(f)

    base_version = version[:-2]

    old = None
    new = None

    for tr in tree.xpath('.//tr'):
        if old is not None:
            new = tr
            break

        for td in tr.xpath('./td'):
            if td.text == base_version:
                old = tr
                break

    assert old is not None
    assert new is not None
    old.getparent().remove(old)

    # rowspan is 1 based in html, but 0 based in lxml
    rowspan = int(td.get("rowspan")) - 1
    if rowspan:
        td.set("rowspan", str(rowspan))
        new.insert(0, td)

    tree.write(p.as_posix(), method='html', pretty_print=True)
    subprocess.run(['git', 'add', p])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('version', help="The released version.")
    args = parser.parse_args()

    is_point = is_point_release(args.version)

    update_index(is_point, args.version)
    update_release_notes(args.version)
    update_calendar(args.version)
    subprocess.run(['git', 'commit', '-m',
                    'docs: update calendar, add news item, and link releases '
                    f'notes for {args.version}'])


if __name__ == "__main__":
    main()
