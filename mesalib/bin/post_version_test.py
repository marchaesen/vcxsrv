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

from unittest import mock
import textwrap

from lxml import html
import pytest

from . import post_version


# Mock out subprocess.run to avoid having git commits
@mock.patch('bin.post_version.subprocess.run', mock.Mock())
class TestUpdateCalendar:

    HEAD = textwrap.dedent("""\
        <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
        <html lang="en">
        <head>
        <meta http-equiv="content-type" content="text/html; charset=utf-8">
        <title>Release Calendar</title>
        <link rel="stylesheet" type="text/css" href="mesa.css">
        </head>
        <body>
        """)

    TABLE = textwrap.dedent("""\
        <table>
        <tr>
        <th>Branch</th>
        <th>Expected date</th>
        <th>Release</th>
        <th>Release manager</th>
        <th>Notes</th>
        </tr>
        """)

    FOOT = "</body></html>"

    TABLE_FOOT = "</table>"

    def wrap_table(self, table: str) -> str:
        return self.HEAD + self.TABLE + table + self.TABLE_FOOT + self.FOOT

    def test_basic(self):
        data = self.wrap_table(textwrap.dedent("""\
            <tr>
            <td rowspan="3">19.2</td>
            <td>2019-11-06</td>
            <td>19.2.3</td>
            <td>Dylan Baker</td>
            </tr>
            <tr>
            <td>2019-11-20</td>
            <td>19.2.4</td>
            <td>Dylan Baker</td>
            </tr>
            <tr>
            <td>2019-12-04</td>
            <td>19.2.5</td>
            <td>Dylan Baker</td>
            <td>Last planned 19.2.x release</td>
            </tr>
            """))

        parsed = html.fromstring(data)
        parsed.write = mock.Mock()

        with mock.patch('bin.post_version.html.parse',
                        mock.Mock(return_value=parsed)):
            post_version.update_calendar('19.2.3')

        assert len(parsed.findall('.//tr')) == 3
        # we need the second element becouse the first is the header

        tr = parsed.findall('.//tr')[1]
        tds = tr.findall('.//td')
        assert tds[0].get("rowspan") == "2"
        assert tds[0].text == "19.2"
        assert tds[1].text == "2019-11-20"

    @pytest.fixture
    def two_releases(self) -> html.etree.ElementTree:
        data = self.wrap_table(textwrap.dedent("""\
            <tr>
            <td rowspan="1">19.1</td>
            <td>2019-11-06</td>
            <td>19.1.8</td>
            <td>Not Dylan Baker</td>
            </tr>
            <tr>
            <td rowspan="3">19.2</td>
            <td>2019-11-06</td>
            <td>19.2.3</td>
            <td>Dylan Baker</td>
            </tr>
            <tr>
            <td>2019-11-20</td>
            <td>19.2.4</td>
            <td>Dylan Baker</td>
            </tr>
            <tr>
            <td>2019-12-04</td>
            <td>19.2.5</td>
            <td>Dylan Baker</td>
            <td>Last planned 19.2.x release</td>
            </tr>
            """))

        p = html.fromstring(data)
        p.write = mock.Mock()
        return p

    def test_two_releases(self, two_releases: html.etree.ElementTree):
        with mock.patch('bin.post_version.html.parse',
                        mock.Mock(return_value=two_releases)):
            post_version.update_calendar('19.2.3')

        assert len(two_releases.findall('.//tr')) == 4
        # we need the second element becouse the first is the header

        tr = two_releases.findall('.//tr')[2]
        tds = tr.findall('.//td')
        assert tds[0].get("rowspan") == "2"
        assert tds[0].text == "19.2"
        assert tds[1].text == "2019-11-20"

    def test_last_Release(self, two_releases: html.etree.ElementTree):
        with mock.patch('bin.post_version.html.parse',
                        mock.Mock(return_value=two_releases)):
            post_version.update_calendar('19.1.8')

        assert len(two_releases.findall('.//tr')) == 4
        # we need the second element becouse the first is the header

        tr = two_releases.findall('.//tr')[1]
        tds = tr.findall('.//td')
        assert tds[0].get("rowspan") == "3"
        assert tds[0].text == "19.2"
        assert tds[1].text == "2019-11-06"
