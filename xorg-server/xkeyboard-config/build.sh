if [ -d builddir ]; then
  rm -rf builddir
fi

if [ -z "${CYGWIN}" ]; then
  meson setup --prefix=$(realpath ../xkbdata) builddir
else
  meson setup --prefix=$(cygpath -w ../xkbdata) builddir
fi
meson compile -C builddir
meson install -C builddir
