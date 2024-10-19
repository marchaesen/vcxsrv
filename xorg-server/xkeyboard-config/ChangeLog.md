xkeyboard-config [2.43](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/tree/xkeyboard-config-2.43) - 2024-10-01

## Models

### New

- Restore geometries for Brazilian ABNT2 (`abnt2`), Japanese 106 (`jp106`)
  and Korean 106 (`kr106`) models. ([#292](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/292))

### Fixes

- geometry: Fixed label of `<LSGT>` key in Kinesis.

  Contributed by Arlen Kleinsasser


## Layouts

### Breaking changes

- `us(colemak_dh_wide_iso)`: Swapped `<AB06>` and `<AD12>` keys to match [specification](https://colemakmods.github.io/mod-dh/keyboards.html)

  Contributed by Callum Andrew ([#442](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/442))
- Updated `de(e1)` and `de(e2)`: implemented the changes made to these layouts in the latest revision of the specification, DIN 2137-1:2023-08; namely, some of the *group 2* symbols, that are accessed by first pressing Alt&nbsp;Gr+f, for keys `´`, `u`, `p`, `,`, and space bar were altered.

  Contributed by Jan Henning Klasen and Jakob Kramer. ([#745](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/745))
- `us(colemak_dh)`: Made `<CAPS>` key behave as Caps Lock by default, as shown in the [specification](https://colemakmods.github.io/mod-dh/keyboards.html).

### New

- Added Diktor layout `ru(diktor)` for Russian.

  Contributed by Hloya Nizhelska ([!712](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/merge_requests/712))
- Added the RuIntl keyboard layouts set `ru(ruintl_ru)`, `ru(ruintl_en)`.

  Contributed by Denis Kaliberov ([!752](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/merge_requests/752))
- Updated layout `us 3l` to include qwerty symbols and correct symbols for less than or equal and greater than or equal.

### Fixes

- rules: Fix broken layout compatibility rules, for symbols sections that where renamed or moved. ([#478](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/478))


## Options

### Breaking changes

- Map `Hyper` to `Mod3` by default to make `Super` and `Hyper` independent
  modifiers. ([#440](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/440))

### New

- Added `caps:return` to make the `Caps Lock` key an additional `Return` key. ([#121](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/121))
- Added `fkeys:basic_13-24`: define `F13-F24` keys with their corresponding function keysyms.

  Contributed by twistedturtle ([#306](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/306))
- Added `altwin:swap_ralt_rwin` to swap right `Alt` with right `Win`. ([#474](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/474))
- Added `caps:digits_row_independent_lock` option to lock digits on the digits
  row (Azerty layouts).

  Contributed by Alexandre Petit
- Added option `lv3:caps_switch_capslock_with_ctrl` to use Caps Lock as
  3rd-level chooser and Ctrl + Caps Lock as original Caps Lock action.

  Contributed by Helfried Wiesinger

### Fixes

- Added `caps:ctrl_shifted_capslock`: make `Caps Lock` an additional `Ctrl`
  and `Shift + Caps Lock` the regular `Caps Lock`.

  Contributed by Han-Miru Kim ([#447](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/447))


## Miscellaneous

### New

- Added `<I570>` keycode (`KEY_REFRESH_RATE_TOGGLE`).


## Build system

### New

- Add a new build option `non-latin-layouts-list` to generate lists of
  non-Latin layouts, e.g. layouts that cannot produce the basic A-Z Latin
  letters. This can be used e.g. in an OS installer to add automatically
  a default layout in such case. ([#120](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/120))

### Fixes

- Relaxed Python version requirement to support ≥ 3.9.
  Improved version detection and corresponding error messages. ([#465](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/465))


xkeyboard-config [2.42](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/tree/xkeyboard-config-2.42) - 2024-06-07
===================================================================================================================================

## Models

### Breaking

- Removed the old Macs
- Removed the MacBook 78/79
- Removed the Intel Classmate
- Removed a few old Nokia devices


## Layouts

### Breaking change

- `dz`: Renamed `la` to `azerty-oss`.
- `br`: Removed the default `Scroll_Lock` mapping.

### New

- `ara(mac-phonetic)`: use new dead key `dead_hamza`.
- `dz`: Added `kab` to the language list.
- `fr`: Added Ergo‑L layout and variant (`ergol`, `ergol_iso`).
- `gr`: Added missing characters from `cp1253` and `varEpsilon`.
- `hu`: Added Old Hungarian layouts for users in SK
- symbols: Added grab and srvrkeys with a single section
- `ru`: Updated Rulemak to latest version.
- `ru(ruu)`: Added `Ukrainian_i` as `Cyrillic_i` alternative to the
   3rd level of `<AB05>`.
- `ua`: Enabled typing “g” with `AltGr`.

### Fixes

- `fr(oss)`: Updated behaviour of space key to match doc.
  [#439](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/439)
- `fr(bepo_afnor)`: Removed unnecessary include.
  [#448](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/448)


## Options

## New

- Added `eurosign:E`.
  [#444](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/issues/444)
- Added `caps:digits_row` for Azerty layouts.
- Added `scrolllock:mod3`.


Older versions
==============

Unfortunately there is no detailed changelog for recent versions. Please see
[`ChangeLog.old`](ChangeLog.old) for versions up to `1.8` (2010) or use the
[git log](https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/commits/master).
