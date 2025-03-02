# Contributing to xkeyboard-config

Thank you for your interest in contributing to xkeyboard-config! There are many ways to
contribute and we appreciate all of them.

## Getting help

First of all, make sure you read the following:
- [Code of conduct]
- [Introduction to XKB]
- [How to enhance XKB configuration](docs/README.enhancing)
- [The XKB Configuration Guide](docs/README.config)
- [Symbols files](docs/README.symbols)
- [The XKB file format] (exhaustive documentation)

[Code of conduct]:     https://www.freedesktop.org/wiki/CodeOfConduct/
[Introduction to XKB]: https://xkbcommon.org/doc/current/xkb-intro.html
[The XKB file format]: https://xkbcommon.org/doc/current/keymap-text-format-v1.html

## Bug reports

Bugs are reported in our [issue tracker].
- *Please first check if an existing issue* on the topic already exists before creating a
  new one.
- Please use the appropriate *template* in the corresponding interface, so that the issue
  can be efficiently processed.

[issue tracker]: https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues

## Making changes to the keyboard database

### Getting started

This project is built using [meson]:

<!-- [NOTE] Keep the following instructions sync with README.md -->

  1. [Install meson]:

     ```bash
     # Recommended: with pipx
     pipx install meson
     # Alternative: with pip
     pip3 install --user meson
     ```

  2. Setup & build:

     ```bash
     # You may choose the install directory with --prefix
     meson setup build --prefix="$PWD/inst"
     meson compile -C build
     ```

  3. Install *locally* for debugging:

     ```bash
     meson install -C build
     ```

[meson]:         https://mesonbuild.com
[Install meson]: https://mesonbuild.com/Getting-meson.html

We ensure our code quality by using [pre-commit]:

  1. [Install pre-commit]:

     ```bash
     # Recommended: with pipx
     pipx install pre-commit
     # Alternative: with pip
     pip3 install --user pre-commit
     ```

  2. Install the git hook scripts:

     ```bash
     pre-commit install
     ```

  3. The checks will then be run before each commit. You may also run them manually,
     like so:

     ```bash
     pre-commit run --all
     ```

[pre-commit]:         https://pre-commit.com/
[Install pre-commit]: https://pre-commit.com/#quick-start

### Testing

**Note:** Remember to re-run `meson install -C build` after each modification and before
running the tests!

- With Xorg tools:

  ```bash
  # Compile keymap to a file
  setxkbmap -print -rules "$PWD/inst/share/X11/xkb/rules/evdev" -layout … \
  	| xkbcomp -I -I"$PWD/inst/share/X11/xkb" \
  		-xkb - /tmp/keymap.xkb
  # Activate keymap
  setxkbmap -print -rules "$PWD/inst/share/X11/xkb/rules/evdev" -layout … \
  	| xkbcomp -I -I"$PWD/inst/share/X11/xkb" - "$DISPLAY"
  # Interactive debugging
  xev -event keyboard
  ```

- With [libxkbcommon] tools:

  ```bash
  # Compile keymap to a file
  xkbcli compile-keymap --include "$PWD/inst/share/X11/xkb" \
  			--layout … > /tmp/keymap.xkb
  # Interactive debugging; require having your user in group “input”
  xkbcli interactive-evdev --include "$PWD/inst/share/X11/xkb" \
  			--layout …
  ```

[libxkbcommon]: https://github.com/xkbcommon/libxkbcommon

### Guidelines for modifications

<!-- Adapted from: https://www.freedesktop.org/wiki/Software/XKeyboardConfig/Rules/ -->

> [!WARNING]
> 🚧 Work in progress 🚧
>
> Some rules may be obsolete, we are working on it

Please *check if an existing issue or merge request* on the topic already exists before
creating a new one.

#### Types

There are no special rules for new types. They are going to be introduced very conservatively.

#### Geometries

There are relatively few geometry descriptions that are currently available.

<!-- FIXME:
People are very welcome to contribute them. The only recommendation is to have them
visually pleasant and precise.
-->

The historical XKB format is complex and is considered deprecated. We accept only fixes.

#### Models

1. Most of PC keyboard models should be defined by the following actions:

   - adding new `xkb_symbols` section to `symbols/inet`
   - extending `$inetkbds` list in `rules/base.lists.part`
   - adding new `model` section to `rules/base.xml` (in `modelList`)

2. It is recommended to use **{v}{m}** pattern for the model name, where **{v}** is
   abbreviated vendor name. **{m}** is the model name. Recommended vendor abbreviations:

   - `a4tech` - A4Tech
   - `acer` - Acer
   - `cherry` - Cherry
   - `chicony` - Chicony
   - `compaq` - Compaq
   - `dell` - Dell
   - `genius` - Genius
   - `logi` - Logitech
   - `microsoft` - Microsoft
   - `samsung` - Samsung

3. The name of `xkb_symbols` section (in `symbols/inet`) should be same as the new element
   in the `$inetkbds` list (in `base.lists.part`), same as *name* element (in *base.xml*).

4. The `vendor` element in `base.xml` has to be specified.

5. While defining `xkb_symbols`, it is strongly recommended to include (where applicable)
   shared sections for the media and navigation keys:

   - `acpi_common`
   - `media_common`
   - `media_acpi_common`
   - `media_nav_acpi_common`
   - `media_nav_common`
   - `nav_common`
   - `nav_acpi_common`

   These sections should be used even if not all of the keys specified in that section are present in the defined model. The syntax is standard:

   ```c
   include "inet(media_common)"
   ```

   For example, if keys *prev/next/play/stop* have the mapping as defined in `media_common`
   (and other keys do not exist in the keyboard at all) - this is a good case for including
   entire `media_common` anyway

6. The key mappings have to be sorted alphabetically, by the keycode name.

7. If your keyboard model mapping is entirely covered by some existing section `symbols/inet`
   (some keycodes may be not actually used by your keyboard), do not create a new section
   consisting of a single `include` statement. Instead, create an alias in `rules/base.m_s.part` file and add it into `rules/base.xml`, as usual.

#### Layouts, Variants

1. Every layout/variant has to define a *single group*: group 1. Layouts with multiple
   groups are not accepted.

2. Every layout/variant has to be defined for some particular country, it should go into
   the file `symbols/{cc}` where `{cc}` is 2-letter country code from [ISO 3166-1 alpha 2].
   The language-based layout/file names are not accepted. If several countries are using
   the same layout (for example, several countries share the same language), it should be
   fully defined for one country only - and included by reference into the files for other
   countries.

   [ISO 3166-1 alpha 2]: http://en.wikipedia.org/wiki/ISO_3166-1_alpha-2

3. Every layout/variant has to be registered in `rules/base.xml`.

   There is only one exception: default variants. These are the variants which are either
   marked by the `default` keyword in the symbols file (it is recommended to put them first
   and name them `basic`), or used as default because of some rule in the `rules/base`
   file (usually by modifying `rules/base.ml_s.part` component).

4. There are popular variant names which are used by many countries, so they should be
   considered as first candidates (where applicable) for new variants. They are:

   - `dvorak` - for national variants of the Dvorak layout
   - `intl` - for variants suitable for multiple languages; usually - for the official
      state language(s) and some other frequently used foreign languages.
   - `mac` - for variants specific to Macintosh keyboards
   - `nodeadkeys` - for variants without dead keys (if the some other national variants
     are using them)
   - `phonetic` - for variants which are using phonetic resemblance (usually - based on
     standard Americal layout)
   - `sundeadkeys` - for variants with dead keys
   - `us` - for variants which are using standard American layout as a basis, adding some
     national characters
   - `winkeys` - for variants which are not standardized nationally but used in Microsoft
     Windows

5. Every layout/variant has to provide a full description. First - as the group name in
   the symbols file, second - as the translatable description in `rules/base.xml`.

   The general approach for the descriptions is to follow “<Language>” or <“Language>
   (<variation>)” convention, for example: “English” or “Russian (legacy)”. The usual
   practice is to use the country name as variation name, for example: “French (Canada)”.
   The language has to be chosen as the one that is most frequently used with that layout/
   variant. That language has to be put first into the `languageList` attribute in
   `base.xml` (if the variant record does not have own `languageList`, the parent layout’s
   languages are used).

6. For the layouts/variants that are “exotic”, it is recommended to use `base.extras.xml`
   instead of `base.xml`. The word *exotic* is used in statistical sense only, e.g. a low
   users count compare to the other layouts.

   There is no formal definition of *exotic*, because in most cases it is not possible to
   prove the actual number of users. There are several “usual suspects” for the *exotic*
   section:

   1. The layouts for endangered/extinct languages/scripts. The statistics can be taken
      from <http://www.ethnologue.com/>. The potential candidates are languages with <100K
      speakers. If the number of speakers is <10K, the language most probably belongs to
      *extras*

   2. The languages that are used most frequently with layouts made for other languages.
      If it is more popular to type a language L1 with layouts designed for another
      language L2, then layouts designed specifically for L1 may be considered as exotic,
      even if the language L1 itself is popular. A possible scenario is national minorities
      using the languages with the script similar to the script used by the language of
      the larger nation in the same country.

   3. The exotic layouts for popular languages. If some relatively small group of people
      are using some variant suitable for their needs. Typical examples: variants for
      programmers, for typography, for particular group of sciences, for sacred texts, etc.

   Please note that **highly personal or experimental layouts will not be accepted**. We
   have [instructions for Xorg] and [instructions for Wayland] to install them *locally*
   as user-specific layouts.

   The following table sums up the the previous examples:

   | Layout | Language  | Speakers | Target use        | Users count  | Scenario | Layout popularity |
   | ------ | --------- | -------- | ----------------- | -----------  | -------- | ----------------- |
   | A      | L1        | \<10 K   | General (L1)      | Low          | 1        | Exotic?           |
   | B      | L2        | \>100 K  | General (L2)      | Average/High |          | Normal            |
   | ″      | L3        | \>100 K  | General (L3, L2?) | Average      | 2        | Exotic?           |
   | ″      | L1        | \<10 K   | General (L1, L2?) | Low          | 2        | Exotic?           |
   | C      | L2        | \>100 K  | General (L2)      | Low          | 3        | Exotic?           |
   | D      | ″         | \>100 K  | Niche             | Low          | 3        | Exotic?           |
   | E      | L1        | \<10 K   | Niche             | Very Low     | 3        | Exotic            |

   That’s typical, but not the only possible scenarios for putting layout/variants into
   the *extras* section.

   The maintainers of xkeyboard-config typically question the popularity of newly
   submitted layouts/variants. If no conclusive proof of number of users is provided, the
   layout may be:

   - *Accepted:* the layout can be put into the *extras* section (the maintainers
     reserve that right).

   - *Rejected:* note that this decision may not be final, e.g. the maintainers noted
     some potential but the layout needs to reach sufficient popularity before being
     proposed again.

     The layout can still be installed *locally* as a user-specific layout: we have
     [instructions for Xorg] and [instructions for Wayland].

   [instructions for Xorg]:    https://who-t.blogspot.com/2021/02/a-pre-supplied-custom-keyboard-layout.html
   [instructions for Wayland]: https://xkbcommon.github.io/libxkbcommon/doc/current/md_doc_user_configuration.html

   Putting layout/variant into the extras section is just a representation of the fact
   that layout is not popular enough to be included into the main section. The GUI tools
   can use any approach to displaying (or not displaying) *extras* - this issue is out
   of scope of xkeyboard-config.

   It is recommended to put *exotic* variants into the end of the corresponding symbols
   file - after the delimiter line: `// EXTRAS:`.

7. The short description (shortDescription tag) is recommended for the indicators that use
   labels (as opposite to flags) for providing the user with the information about
   currently used layout/variant. This description is expected to contain the 2-letter
   [ISO639-1 code] (in lowercase) of the primary language - or, if no ISO639-1 exists for
   that language, it can be 3-letter code from ISO639-2 or ISO639-3. That code is made
   translatable, so that localized GUI can display some abbreviations suitable for the
   current locale. If some variant does not provide own short description, the short
   description from the parent layout is expected to be used.

   [ISO639-1 code]: http://www.loc.gov/standards/iso639-2/php/code_list.php

8. For layouts/variants using more than 2 shift levels, it is highly recommended to
   include `level3(ralt_switch)` symbols definition - for standard switching to levels 3
   and 4 using `AltGr` key.

9. The authors are highly encouraged to use `include` statements wherever possible – it
   makes the resulting code more compact and easier to manage.

10. The key mappings have to be sorted alphabetically, by the keycode name.

11. When you contribute *model-specific* layout/variant (for example, German for Macintosh),
    try to separate layout-specific mappings from the general ones, common to any national
    layout on that hardware. Usually, alphanumeric and punctuation key mappings are
    layout-specific, while navigation keys, functional keys, modifiers etc are model-specific.
    Consider putting model-specific keys mappings to the model-specific definitions
    (usually it is a section of `symbols/inet` file).

<!--
12. It is recommended to use `-U 7` rather than `-u` option with diff, that makes patches
    easier to apply (see <https://bugs.freedesktop.org/show_bug.cgi?id=106261>).
-->
