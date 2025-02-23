# Fragments for the changelog

This directory contains fragments for the future [Changelog](../ChangeLog.md) file.

## Introduction

We use <code>[towncrier]</code> to produce useful, summarized news files.
The idea is to add revelant fragments in each MR, so that we can generate
a user-friendly changelog automatically.

There are 5 sections types:

- Model: `changes/models`
- Layout: `changes/layouts`
- Option: `changes/options`
- Miscellaneous: `changes/misc`
- Build System: `changes/build`

There are 3 news fragments types:

- Breaking changes: `.breaking`
- New: `.feature`
- Fixes: `.bugfix`

[towncrier]: https://pypi.org/project/towncrier/

## Adding a fragment

Add a short description of the change in a file `changes/SECTION/ID.FRAGMENT.md`,
where:

- `SECTION` and `FRAGMENT` values are described in the [previous section](#introduction).
- `ID` is the corresponding issue identifier on Github, if relevant. If there is
  no such issue, then `ID` should start with `+` and some identifier that make
  the file unique in the directory.

Examples:
- A _bug fix_ for the _issue #465_ is a _build_ change, so the corresponding file
  should be named `changes/build/465.bugfix.md`.
- A _new feature_ for _options_ like !662 corresponds to e.g.
  `changes/options/+add-scrolllock-mod3.feature.md`.

Guidelines for the fragment files:

- Use the [Markdown] markup.
- Use past tense, e.g. “Fixed a segfault”.
- Look at the previous releases [ChangeLog](../ChangeLog.md) file for further examples.

[Markdown]: https://daringfireball.net/projects/markdown/

## Build the changelog

Note: this step is optional for contributors.

Install <code>[towncrier]</code> from Pypi:

```bash
python3 -m pip install towncrier
```

Then build the changelog:

<dl>
  <dt>Only check the result</dt>
  <dd>
  Useful after adding a new fragment.

  ```bash
  towncrier build --draft --version 2.99
  ```
  </dd>
  <dt>Write the changelog & delete the news fragments</dt>
  <dd>

  **⚠️ Warning: For maintainers only! ⚠️**
  This step must be done just before release.

  ```bash
  towncrier build --yes --version 2.99
  ```
  </dd>
</dl>
