Android
=======

Mesa hardware drivers can be built for Android one of two ways: built
into the Android OS using the ndk-build build system on older versions
of Android, or out-of-tree using the Meson build system and the
Android NDK.

The ndk-build build system has proven to be hard to maintain, as one
needs a built Android tree to build against, and it has never been
tested in CI.  The Meson build system flow is frequently used by
Chrome OS developers for building and testing Android drivers.

When building llvmpipe or lavapipe for Android the ndk-build workflow
is also used, but there are additional steps required to add the driver
to the Android OS image.

Building using the Android NDK
------------------------------

Download and install the NDK using whatever method you normally would.
Then, create your Meson cross file to use it, something like this
``~/.local/share/meson/cross/android-aarch64`` file:

.. code-block:: ini

    [binaries]
    ar = 'NDKDIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android-ar'
    c = ['ccache', 'NDKDIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android34-clang']
    cpp = ['ccache', 'NDKDIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android34-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
    c_ld = 'lld'
    cpp_ld = 'lld'
    strip = 'NDKDIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android-strip'
    # Android doesn't come with a pkg-config, but we need one for Meson to be happy not
    # finding all the optional deps it looks for.  Use system pkg-config pointing at a
    # directory we get to populate with any .pc files we want to add for Android
    pkg-config = ['env', 'PKG_CONFIG_LIBDIR=NDKDIR/pkgconfig', '/usr/bin/pkg-config']

    [host_machine]
    system = 'android'
    cpu_family = 'aarch64'
    cpu = 'armv8'
    endian = 'little'

Now, use that cross file for your Android build directory (as in this
one cross-compiling the turnip driver for a stock Pixel phone)

.. code-block:: sh

    meson setup build-android-aarch64 \
        --cross-file android-aarch64 \
	-Dplatforms=android \
	-Dplatform-sdk-version=34 \
	-Dandroid-stub=true \
	-Dgallium-drivers= \
	-Dvulkan-drivers=freedreno \
	-Dfreedreno-kmds=kgsl
    meson compile -C build-android-aarch64

Replacing Android drivers on stock Android
------------------------------------------

The vendor partition with the drivers is normally mounted from a
read-only disk image on ``/vendor``.  To be able to replace them for
driver development, we need to unlock the device and remount
``/vendor`` read/write.

.. code-block:: sh

    adb disable-verity
    adb reboot
    adb remount -R
    adb remount

Now you can replace drivers as in:

.. code-block:: sh

    adb push build-android-aarch64/src/freedreno/vulkan/libvulkan_freedreno.so /vendor/lib64/hw/vulkan.sdm710.so

Note this command doesn't quite work because libvulkan wants the
SONAME to match. You can use ``patchelf`` to fix this:

.. code-block:: sh

   cp build-android-aarch64/src/freedreno/vulkan/libvulkan_freedreno.so /tmp/vulkan.sdm710.so
   patchelf --set-soname vulkan.sdm710.so /tmp/vulkan.sdm710.so
   adb push /tmp/vulkan.sdm710.so /vendor/lib64/hw/

Replacing Android drivers on Chrome OS
--------------------------------------

Chrome OS's ARC++ is an Android container with hardware drivers inside
of it.  The vendor partition with the drivers is normally mounted from
a read-only squashfs image on disk.  For doing rapid driver
development, you don't want to regenerate that image.  So, we'll take
the existing squashfs image, copy it out on the host, and then use a
bind mount instead of a loopback mount so we can update our drivers
using scp from outside the container.

On your device, you'll want to make ``/`` read-write.  ssh in as root
and run:

.. code-block:: sh

    crossystem dev_boot_signed_only=0
    /usr/share/vboot/bin/make_dev_ssd.sh --remove_rootfs_verification --partitions 4
    reboot

Then, we'll switch Android from using an image for ``/vendor`` to using a
bind-mount from a directory we control.

.. code-block:: sh

    cd /opt/google/containers/android/
    mkdir vendor-ro
    mount -o loop vendor.raw.img vendor-ro
    cp -a vendor-ro vendor-rw
    emacs config.json

In the ``config.json``, you want to find the block for ``/vendor`` and
change it to::

            {
                "destination": "/vendor",
                "type": "bind",
                "source": "/opt/google/containers/android/vendor-rw",
                "options": [
                    "bind",
                    "rw"
                ]
            },

Now, restart the UI to do a full reload:

.. code-block:: sh

    restart ui

At this point, your android container is restarted with your new
bind-mount ``/vendor``, and if you use ``android-sh`` to shell into it
then the ``mount`` command should show::

    /dev/root on /vendor type ext2 (rw,seclabel,relatime)

Now, replacing your DRI driver with a new one built for Android should
be a matter of:

.. code-block:: sh

    scp msm_dri.so $HOST:/opt/google/containers/android/vendor-rw/lib64/dri/

You can do your build of your DRI driver using ``emerge-$BOARD
arc-mesa-freedreno`` (for example) if you have a source tree with
ARC++, but it should also be possible to build using the NDK as
described above.  There are currently rough edges with this, for
example the build will require that you have your arc-libdrm build
available to the NDK, assuming you're building anything but the
Freedreno Vulkan driver for KGSL.  You can mostly put things in place
with:

.. code-block:: sh

    scp $HOST:/opt/google/containers/android/vendor-rw/lib64/libdrm.so \
        NDKDIR/sysroot/usr/lib/aarch64-linux-android/lib/

    ln -s \
        /usr/include/xf86drm.h \
	/usr/include/libsync.h \
	/usr/include/libdrm \
	NDKDIR/sysroot/usr/include/

It seems that new invocations of an application will often reload the
DRI driver, but depending on the component you're working on you may
find you need to reload the whole Android container.  To do so without
having to log in to Chrome again every time, you can just kill the
container and let it restart:

.. code-block:: sh

    kill $(cat /run/containers/android-run_oci/container.pid )

Adding out-of-tree drivers to Android OS image
----------------------------------------------

When building your own Android OS images it's possible to add
drivers built out of tree directly into the OS image. For
running llvmpipe and lavapipe on Android this step is required
to ensure Android is able to load the drivers correctly.

The following steps provide and example for building
the android cuttlefish image following the official Android
documentation from https://source.android.com/docs/setup

When building llvmpipe or lavapipe for Android, it is required
to do this so that the permissions for accessing the library
are set correctly.

Following the Android documentation, we can run the following
commands

.. code-block:: sh

   repo init -b main -u https://android.googlesource.com/platform/manifest
   repo sync -c -j8

   source build/envsetup.sh
   lunch aosp_cf_x86_64_phone-trunk_staging-userdebug

Be aware that the sync command can take a long time to run as
it will download all of the source code. This will set up
the ``aosp_cf_x86_64_phone-trunk_staging-userdebug`` build target
for Android. Please note that the x86_64 cuttlefish target will require
you to build mesa for 32bit and 64bit. Next we need to copy the build
driver libraries into the source tree of Android and patch the binary names.

.. code-block:: sh

   mkdir prebuilts/mesa
   mkdir prebuilts/mesa/x86_64
   mkdir prebuilts/mesa/x86
   cp ${INSTALL_PREFIX_64}/lib/libEGL.so prebuilts/mesa/x86_64/
   cp ${INSTALL_PREFIX_64}/lib/libgallium_dri.so prebuilts/mesa/x86_64/
   cp ${INSTALL_PREFIX_64}/lib/libGLESv1_CM.so  prebuilts/mesa/x86_64/
   cp ${INSTALL_PREFIX_64}/lib/libGLESv2.so  prebuilts/mesa/x86_64/
   cp ${INSTALL_PREFIX_64}/lib/libvulkan_lvp.so prebuilts/mesa/x86_64/
   cp ${INSTALL_PREFIX_32}/lib/libEGL.so prebuilts/mesa/x86
   cp ${INSTALL_PREFIX_32}/lib/libgallium_dri.so prebuilts/mesa/x86/
   cp ${INSTALL_PREFIX_32}/lib/libGLESv1_CM.so  prebuilts/mesa/x86
   cp ${INSTALL_PREFIX_32}/lib/libGLESv2.so  prebuilts/mesa/x86
   cp ${INSTALL_PREFIX_32}/lib/libvulkan_lvp.so prebuilts/mesa/x86

   patchelf --set-soname libEGL_lp.so prebuilts/mesa/x86_64/libEGL.so
   patchelf --set-soname libGLESv1_CM_lp.so prebuilts/mesa/x86_64/libGLESv1_CM.so
   patchelf --set-soname libGLESv2_lp.so prebuilts/mesa/x86_64/libGLESv2.so
   patchelf --set-soname vulkan.lvp.so prebuilts/mesa/x86_64/libvulkan_lvp.so
   patchelf --set-soname libEGL_lp.so prebuilts/mesa/x86/libEGL.so
   patchelf --set-soname libGLESv1_CM_lp.so prebuilts/mesa/x86/libGLESv1_CM.so
   patchelf --set-soname libGLESv2_lp.so prebuilts/mesa/x86/libGLESv2.so
   patchelf --set-soname vulkan.lvp.so prebuilts/mesa/x86/libvulkan_lvp.so

We then need to create an ``prebuilts/mesa/Android.bp`` build file to include
the libraries in the build.

.. code-block::

   cc_prebuilt_library_shared {
       name: "libgallium_dri",
       arch: {
           x86_64: {
               srcs: ["x86_64/libgallium_dri.so"],
           },
           x86: {
               srcs: ["x86/libgallium_dri.so"],
           },
       },
       strip: {
           none: true,
       },
       relative_install_path: "egl",
       shared_libs: ["libc", "libdl", "liblog", "libm"],
       check_elf_files: false,
       vendor: true
   }

   cc_prebuilt_library_shared {
       name: "libEGL_lp",
       arch: {
           x86_64: {
               srcs: ["x86_64/libEGL.so"],
           },
           x86: {
               srcs: ["x86/libEGL.so"],
           },
       },
       strip: {
           none: true,
       },
       relative_install_path: "egl",
       shared_libs: ["libc", "libdl", "liblog", "libm", "libcutils", "libdrm", "libhardware", "liblog", "libnativewindow", "libsync"],
       check_elf_files: false,
       vendor: true
   }

   cc_prebuilt_library_shared {
       name: "libGLESv1_CM_lp",
       arch: {
           x86_64: {
               srcs: ["x86_64/libGLESv1_CM.so"],
           },
           x86: {
               srcs: ["x86/libGLESv1_CM.so"],
           },
       },
       strip: {
           none: true,
       },
       relative_install_path: "egl",
       shared_libs: ["libc", "libdl", "liblog", "libm"],
       check_elf_files: false,
       vendor: true
   }

   cc_prebuilt_library_shared {
       name: "libGLESv2_lp",
       arch: {
           x86_64: {
               srcs: ["x86_64/libGLESv2.so"],
           },
           x86: {
               srcs: ["x86_64/libGLESv2.so"],
           },
       },
       strip: {
           none: true,
       },
       relative_install_path: "egl",
       shared_libs: ["libc", "libdl", "liblog", "libm"],
       check_elf_files: false,
       vendor: true
   }

   cc_prebuilt_library_shared {
       name: "vulkan.lvp",
       arch: {
           x86_64: {
               srcs: ["x86_64/libvulkan_lvp.so"],
           },
           x86: {
               srcs: ["x86/libvulkan_lvp.so"],
           },
       },
       strip: {
           none: true,
       },
       relative_install_path: "hw",
       shared_libs: ["libc", "libdl", "liblog", "libm", "libcutils", "libdrm", "liblog", "libnativewindow", "libsync", "libz"],
       vendor: true
   }


Next we need to update the device configuration to include the libraries
in the build, as well as set the appropriate system properties. We can
create the file
``device/google/cuttlefish/shared/mesa/device_vendor.mk``


.. code-block:: makefile

   PRODUCT_SOONG_NAMESPACES += prebuilts/mesa
   PRODUCT_PACKAGES += libglapi \
                       libGLESv1_CM_lp \
                       libGLESv2_lp \
                       libEGL_lp \
                       libgallium_dri.so \
                       vulkan.lvp
   PRODUCT_VENDOR_PROPERTIES += \
           ro.hardware.egl=lp \
           ro.hardware.vulkan=lvp \
           mesa.libgl.always.software=true \
           mesa.android.no.kms.swrast=true \
           debug.hwui.renderer=opengl \
           ro.gfx.angle.supported=false \
           debug.sf.disable_hwc_vds=1 \
           ro.vendor.hwcomposer.mode=client

Also the file ``device/google/cuttlefish/shared/mesa/BoardConfig.mk``

.. code-block:: makefile

   BOARD_VENDOR_SEPOLICY_DIRS += \
           device/google/cuttlefish/shared/mesa/sepolicy

Next the file ``device/google/cuttlefish/shared/mesa/sepolicy/file_contexts``

.. code-block:: sh

   /vendor/lib(64)?/egl/libEGL_lp\.so u:object_r:same_process_hal_file:s0
   /vendor/lib(64)?/egl/libGLESv1_CM_lp\.so u:object_r:same_process_hal_file:s0
   /vendor/lib(64)?/egl/libGLESv2_lp\.so u:object_r:same_process_hal_file:s0
   /vendor/lib(64)?/libglapi\.so u:object_r:same_process_hal_file:s0
   /vendor/lib(64)?/libgallium_dri\.so u:object_r:same_process_hal_file:s0
   /vendor/lib(64)?/hw/vulkan\.lvp\.so u:object_r:same_process_hal_file:s0

After creating these files we need to modify the existing config files
to include these build files. First we modify
``device/google/cuttlefish/shared/phone/device_vendor.mk``
to add the below code in the spot where other device_vendor
files are included.

.. code-block:: sh

   $(call inherit-product, device/google/cuttlefish/shared/mesa/device_vendor.mk)

Lastly we modify
``device/google/cuttlefish/vsoc_x86_64/BoardConfig.mk`` to include
the following line where the other BoardConfig files are included

.. code-block:: sh

   -include device/google/cuttlefish/shared/mesa/BoardConfig.mk

Then we are set to continue following the official instructions to
build the cuttlefish target and run it in the cuttlefish emulator.
