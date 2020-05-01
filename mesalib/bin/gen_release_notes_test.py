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

import pytest

from .gen_release_notes import *


@pytest.mark.parametrize(
    'current, is_point, expected',
    [
        ('19.2.0', True, '19.2.1'),
        ('19.3.6', True, '19.3.7'),
        ('20.0.0-rc4', False, '20.0.0'),
    ])
def test_next_version(current: str, is_point: bool, expected: str) -> None:
    assert calculate_next_version(current, is_point) == expected


@pytest.mark.parametrize(
    'current, is_point, expected',
    [
        ('19.3.6', True, '19.3.6'),
        ('20.0.0-rc4', False, '19.3.0'),
    ])
def test_previous_version(current: str, is_point: bool, expected: str) -> None:
    assert calculate_previous_version(current, is_point) == expected


@pytest.mark.asyncio
async def test_get_shortlog():
    # Certainly not perfect, but it's something
    version = '19.2.0'
    out = await get_shortlog(version)
    assert out


@pytest.mark.asyncio
async def test_gather_commits():
    # Certainly not perfect, but it's something
    version = '19.2.0'
    out = await gather_commits(version)
    assert out
