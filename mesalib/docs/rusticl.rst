Rusticl
=======

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
   ``libLLVMSPIRVLib.so`` matching your version of LLVM, ie. if you're
   using LLVM 15 (``libLLVM.so.15``), then you need a
   ``libLLVMSPIRVLib.so.15``.

The minimum versions to build Rusticl are:

-  Rust: 1.59
-  Meson: 0.61.4
-  LLVM: 11.0.0 (recommended: 15.0.0)
-  SPIRV-Tools: any version (recommended: v2022.3)

Afterwards you only need to add ``-Dgallium-rusticl=true -Dllvm=enabled
-Drust_std=2021`` to your build options.

Most of the code related to Mesa's C code lives inside ``/mesa``, with
the occasional use of enums, structs or constants through the code base.

If you need help ping ``karolherbst`` either in ``#dri-devel`` or
``#rusticl`` on OFTC.

Also, make sure that before submitting code to verify the formatting is
in order. That can easily be done via ``git ls-files */{lib,app}.rs
| xargs rustfmt``

When submitting Merge Requests or filing bugs related to Rusticl, make
sure to add the ``Rusticl`` label so people subscribed to that Label get
pinged.

Known issues
------------

One issue you might come across is, that the Rust edition meson sets is
not right. This is a known `meson bug
<https://github.com/mesonbuild/meson/issues/10664>`__ and in order to
fix it, simply run ``meson configure $your_build_dir -Drust_std=2021``
