Rusticl
=======

Rusticl is an OpenCL implementation on top of Gallium drivers.

Enabling
--------

In order to use Rusticl on any platform the environment variable
:envvar:`RUSTICL_ENABLE` has to be used. Rusticl does not advertise devices
for any driver by default yet as doing so can impact system stability until
remaining core issues are ironed out.

Enabling drivers by default
---------------------------

Distributions and everybody building rusticl themselves can opt-in or opt-out
certain drivers from being enabled by default. The
``gallium-rusticl-enable-drivers`` takes a list of drivers to enable by
default. The environment variable :envvar:`RUSTICL_ENABLE` will overwrite this
list at runtime.

Not all drivers are supported to be enabled by default, because that should
require opt-in by the driver maintainers. Check out the meson option
documentation to see for which drivers this option is supported.

The ``auto`` option might not enable all drivers supported by this flag, but
for distribution it's recommended to use that one unless they get an ack from
driver maintainers to expand the list.

Building
--------

To build Rusticl you need to satisfy the following build dependencies:

-  ``rustc``
-  ``rustfmt`` (highly recommended, but only *required* for CI builds
   or when authoring patches)
-  ``bindgen``
-  `LLVM <https://github.com/llvm/llvm-project/>`__ built with
   ``libclc`` and ``-DLLVM_ENABLE_DUMP=ON``.
-  `SPIRV-Tools <https://github.com/KhronosGroup/SPIRV-Tools>`__
-  `SPIRV-LLVM-Translator
   <https://github.com/KhronosGroup/SPIRV-LLVM-Translator>`__ for a
   ``libLLVMSPIRVLib.so`` matching your version of LLVM, i.e. if you're
   using LLVM 15 (``libLLVM.so.15``), then you need a
   ``libLLVMSPIRVLib.so.15``.

The minimum versions to build Rusticl are:

-  Rust: 1.76
-  Meson: 1.4.0
-  Bindgen: 0.65.0
-  LLVM: 15.0.0
-  Clang: 15.0.0
   Updating clang requires a rebuilt of mesa and rusticl if and only if the value of
   ``CLANG_RESOURCE_DIR`` changes. It is defined through ``clang/Config/config.h``.
-  SPIRV-Tools: any version (recommended: v2022.3)

Afterwards you only need to add ``-Dgallium-rusticl=true -Dllvm=enabled
-Drust_std=2021`` to your build options.

Most of the code related to Mesa's C code lives inside ``/mesa``, with
the occasional use of enums, structs or constants through the code base.

If you need help ping ``karolherbst`` either in ``#dri-devel`` or
``#rusticl`` on OFTC.

Contributing 
------------

The minimum configuration you need to start developing with rust
is ``RUSTC=clippy-driver meson configure -Dgallium-rusticl=true
-Dllvm=enabled -Drust_std=2021``. In addition you probably want to enable
any device drivers on your platform. Some device drivers as well as some
features are locked behind flags during runtime. See
:ref:`Rusticl environment variables <rusticl-env-var>` for
more info.

All patches that are potentially conformance breaking and also patches
that add new features should be ran against the appropriate conformance
tests.

Also, make sure the formatting is in order before submitting code. That
can easily be done via ``git ls-files */{lib,main}.rs | xargs rustfmt``.

When submitting Merge Requests or filing bugs related to Rusticl, make
sure to add the ``Rusticl`` label so people subscribed to that Label get
pinged.

Known issues
------------

One issue you might come across is, that the Rust edition meson sets is
not right. This is a known `meson bug
<https://github.com/mesonbuild/meson/issues/10664>`__ and in order to
fix it, simply run ``meson configure $your_build_dir -Drust_std=2021``
