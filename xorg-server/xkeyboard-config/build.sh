meson --prefix=$(realpath ../xkbdata) builddir
cd builddir; meson compile; meson install

