meson -D=xorg-rules-copy=true --prefix=$(realpath ../xkbdata) builddir
cd builddir; meson compile; meson install

