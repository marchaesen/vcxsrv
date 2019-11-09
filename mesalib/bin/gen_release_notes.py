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

"""Generates release notes for a given version of mesa."""

import asyncio
import datetime
import os
import pathlib
import sys
import textwrap
import typing
import urllib.parse

import aiohttp
from mako.template import Template
from mako import exceptions


CURRENT_GL_VERSION = '4.6'
CURRENT_VK_VERSION = '1.1'

TEMPLATE = Template(textwrap.dedent("""\
    <%!
        import html
    %>
    <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
    <html lang="en">
    <head>
    <meta http-equiv="content-type" content="text/html; charset=utf-8">
    <title>Mesa Release Notes</title>
    <link rel="stylesheet" type="text/css" href="../mesa.css">
    </head>
    <body>

    <div class="header">
    <h1>The Mesa 3D Graphics Library</h1>
    </div>

    <iframe src="../contents.html"></iframe>
    <div class="content">

    <h1>Mesa ${next_version} Release Notes / ${today}</h1>

    <p>
    %if not bugfix:
        Mesa ${next_version} is a new development release. People who are concerned
        with stability and reliability should stick with a previous release or
        wait for Mesa ${version[:-1]}1.
    %else:
        Mesa ${next_version} is a bug fix release which fixes bugs found since the ${version} release.
    %endif
    </p>
    <p>
    Mesa ${next_version} implements the OpenGL ${gl_version} API, but the version reported by
    glGetString(GL_VERSION) or glGetIntegerv(GL_MAJOR_VERSION) /
    glGetIntegerv(GL_MINOR_VERSION) depends on the particular driver being used.
    Some drivers don't support all the features required in OpenGL ${gl_version}. OpenGL
    ${gl_version} is <strong>only</strong> available if requested at context creation.
    Compatibility contexts may report a lower version depending on each driver.
    </p>
    <p>
    Mesa ${next_version} implements the Vulkan ${vk_version} API, but the version reported by
    the apiVersion property of the VkPhysicalDeviceProperties struct
    depends on the particular driver being used.
    </p>

    <h2>SHA256 checksum</h2>
    <pre>
    TBD.
    </pre>


    <h2>New features</h2>

    <ul>
    %for f in features:
        <li>${html.escape(f)}</li>
    %endfor
    </ul>

    <h2>Bug fixes</h2>

    <ul>
    %for b in bugs:
        <li>${html.escape(b)}</li>
    %endfor
    </ul>

    <h2>Changes</h2>

    <ul>
    %for c, author in changes:
      %if author:
        <p>${html.escape(c)}</p>
      %else:
        <li>${html.escape(c)}</li>
      %endif
    %endfor
    </ul>

    </div>
    </body>
    </html>
    """))


async def gather_commits(version: str) -> str:
    p = await asyncio.create_subprocess_exec(
        'git', 'log', f'mesa-{version}..', '--grep', r'Closes: \(https\|#\).*',
        stdout=asyncio.subprocess.PIPE)
    out, _ = await p.communicate()
    assert p.returncode == 0, f"git log didn't work: {version}"
    return out.decode().strip()


async def gather_bugs(version: str) -> typing.List[str]:
    commits = await gather_commits(version)

    issues: typing.List[str] = []
    for commit in commits.split('\n'):
        sha, message = commit.split(maxsplit=1)
        p = await asyncio.create_subprocess_exec(
            'git', 'log', '--max-count', '1', r'--format=%b', sha,
            stdout=asyncio.subprocess.PIPE)
        _out, _ = await p.communicate()
        out = _out.decode().split('\n')
        for line in reversed(out):
            if line.startswith('Closes:'):
                bug = line.lstrip('Closes:').strip()
                break
        else:
            raise Exception('No closes found?')
        if bug.startswith('h'):
            # This means we have a bug in the form "Closes: https://..."
            issues.append(os.path.basename(urllib.parse.urlparse(bug).path))
        else:
            issues.append(bug.lstrip('#'))

    loop = asyncio.get_event_loop()
    async with aiohttp.ClientSession(loop=loop) as session:
        results = await asyncio.gather(*[get_bug(session, i) for i in issues])
    typing.cast(typing.Tuple[str, ...], results)
    return list(results)


async def get_bug(session: aiohttp.ClientSession, bug_id: str) -> str:
    """Query gitlab to get the name of the issue that was closed."""
    # Mesa's gitlab id is 176,
    url = 'https://gitlab.freedesktop.org/api/v4/projects/176/issues'
    params = {'iids[]': bug_id}
    async with session.get(url, params=params) as response:
        content = await response.json()
    return content[0]['title']


async def get_shortlog(version: str) -> str:
    """Call git shortlog."""
    p = await asyncio.create_subprocess_exec('git', 'shortlog', f'mesa-{version}..',
                                             stdout=asyncio.subprocess.PIPE)
    out, _ = await p.communicate()
    assert p.returncode == 0, 'error getting shortlog'
    assert out is not None, 'just for mypy'
    return out.decode()


def walk_shortlog(log: str) -> typing.Generator[typing.Tuple[str, bool], None, None]:
    for l in log.split('\n'):
        if l.startswith(' '): # this means we have a patch description
            yield l, False
        else:
            yield l, True


def calculate_next_version(version: str, is_point: bool) -> str:
    """Calculate the version about to be released."""
    if '-' in version:
        version = version.split('-')[0]
    if is_point:
        base = version.split('.')
        base[2] = str(int(base[2]) + 1)
        return '.'.join(base)
    return version


def calculate_previous_version(version: str, is_point: bool) -> str:
    """Calculate the previous version to compare to.

    In the case of -rc to final that verison is the previous .0 release,
    (19.3.0 in the case of 20.0.0, for example). for point releases that is
    the last point release. This value will be the same as the input value
    for a point release, but different for a major release.
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


def get_features(is_point_release: bool) -> typing.Generator[str, None, None]:
    p = pathlib.Path(__file__).parent.parent / 'docs' / 'relnotes' / 'new_features.txt'
    if p.exists():
        if is_point_release:
            print("WARNING: new features being introduced in a point release", file=sys.stderr)
        with p.open('rt') as f:
            for line in f:
                yield line
    else:
        yield "None"


async def main() -> None:
    v = pathlib.Path(__file__).parent.parent / 'VERSION'
    with v.open('rt') as f:
        raw_version = f.read().strip()
    is_point_release = '-rc' not in raw_version
    assert '-devel' not in raw_version, 'Do not run this script on -devel'
    version = raw_version.split('-')[0]
    previous_version = calculate_previous_version(version, is_point_release)
    next_version = calculate_next_version(version, is_point_release)

    shortlog, bugs = await asyncio.gather(
        get_shortlog(previous_version),
        gather_bugs(previous_version),
    )

    final = pathlib.Path(__file__).parent.parent / 'docs' / 'relnotes' / f'{next_version}.html'
    with final.open('wt') as f:
        try:
            f.write(TEMPLATE.render(
                bugfix=is_point_release,
                bugs=bugs,
                changes=walk_shortlog(shortlog),
                features=get_features(is_point_release),
                gl_version=CURRENT_GL_VERSION,
                next_version=next_version,
                today=datetime.date.today(),
                version=previous_version,
                vk_version=CURRENT_VK_VERSION,
            ))
        except:
            print(exceptions.text_error_template().render())


if __name__ == "__main__":
    loop = asyncio.get_event_loop()
    loop.run_until_complete(main())
