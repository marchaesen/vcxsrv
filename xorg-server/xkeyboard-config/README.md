# xkeyboard-config

This project provides a consistent, well-structured, frequently
released, open source database of keyboard configuration data.
The project is targeted at **XKB**-based systems.

## What is XKB?

The **X** **K**ey**B**oard (XKB) Extension essentially replaces the core
protocol definition of a keyboard. The extension makes it possible to
specify clearly and explicitly most aspects of keyboard behaviour on a
per-key basis, and to track more closely the logical and physical state
of a keyboard. It also includes a number of keyboard controls designed
to make keyboards more accessible to people with physical impairments.

There are five components that define a complete keyboard mapping:
*symbols*, *geometry*, *keycodes*, *compat*, and *types*; these five
components can be combined together using the 'rules' component of the
database provided by this project, xkeyboard-config.

The complete specification for the XKB Extension can be found here:
http://xfree86.org/current/XKBproto.pdf

## Documentation

- XKB configuration information, see:
  [docs/README.config](docs/README.config)

- For information on how to enhance the database itself, see:
  [docs/README.enhancing](docs/README.enhancing)

- For guidelines to making contributions to this project, see:
  [Contributing](CONTRIBUTING.md)

- To submit bug reports (and patches), please use the issue system in
  freedesktop.org's gitlab instance:
  https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/issues

## Building

This project is built using [meson]. The following instructions aim at a *minimal*
build setup; for *contributing* please see the [dedicated instructions](CONTRIBUTING.md).

<!-- [NOTE] Keep the following instructions sync with CONTRIBUTING.md -->

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

  4. Test:

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

[meson]: https://mesonbuild.com
[Install meson]: https://mesonbuild.com/Getting-meson.html
[libxkbcommon]: https://github.com/xkbcommon/libxkbcommon

# Contributing

Contributions are much appreciated! See our [contribution guide](CONTRIBUTING.md)
for further details.
