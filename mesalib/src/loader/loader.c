/*
 * Copyright (C) 2013 Rob Clark <robclark@freedesktop.org>
 * Copyright (C) 2014-2016 Emil Velikov <emil.l.velikov@gmail.com>
 * Copyright (C) 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
#include <GL/gl.h>
#include <GL/internal/dri_interface.h>
#include "loader.h"

#ifdef HAVE_LIBDRM
#include <xf86drm.h>
#ifdef USE_DRICONF
#include "util/xmlconfig.h"
#include "util/xmlpool.h"
#endif
#endif

#define __IS_LOADER
#include "pci_id_driver_map.h"

static void default_logger(int level, const char *fmt, ...)
{
   if (level <= _LOADER_WARNING) {
      va_list args;
      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
   }
}

static void (*log_)(int level, const char *fmt, ...) = default_logger;

int
loader_open_device(const char *device_name)
{
   int fd;
#ifdef O_CLOEXEC
   fd = open(device_name, O_RDWR | O_CLOEXEC);
   if (fd == -1 && errno == EINVAL)
#endif
   {
      fd = open(device_name, O_RDWR);
      if (fd != -1)
         fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
   }
   return fd;
}

static char *loader_get_kernel_driver_name(int fd)
{
#if HAVE_LIBDRM
   char *driver;
   drmVersionPtr version = drmGetVersion(fd);

   if (!version) {
      log_(_LOADER_WARNING, "failed to get driver name for fd %d\n", fd);
      return NULL;
   }

   driver = strndup(version->name, version->name_len);

   drmFreeVersion(version);
   return driver;
#else
   return NULL;
#endif
}

#if defined(HAVE_LIBDRM)
int
loader_open_render_node(const char *name)
{
   drmDevicePtr *devices, device;
   int err, render = -ENOENT, fd;
   unsigned int num, i;

   err = drmGetDevices2(0, NULL, 0);
   if (err < 0)
      return err;

   num = err;

   devices = calloc(num, sizeof(*devices));
   if (!devices)
      return -ENOMEM;

   err = drmGetDevices2(0, devices, num);
   if (err < 0) {
      render = err;
      goto free;
   }

   for (i = 0; i < num; i++) {
      device = devices[i];

      if ((device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          (device->bustype == DRM_BUS_PLATFORM)) {
         drmVersionPtr version;

         fd = open(device->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
         if (fd < 0)
            continue;

         version = drmGetVersion(fd);
         if (!version) {
            close(fd);
            continue;
         }

         if (strcmp(version->name, name) != 0) {
            drmFreeVersion(version);
            close(fd);
            continue;
         }

         drmFreeVersion(version);
         render = fd;
         break;
      }
   }

   drmFreeDevices(devices, num);

free:
   free(devices);
   return render;
}

#ifdef USE_DRICONF
static const char __driConfigOptionsLoader[] =
DRI_CONF_BEGIN
    DRI_CONF_SECTION_INITIALIZATION
        DRI_CONF_DEVICE_ID_PATH_TAG()
        DRI_CONF_DRI_DRIVER()
    DRI_CONF_SECTION_END
DRI_CONF_END;

static char *loader_get_dri_config_driver(int fd)
{
   driOptionCache defaultInitOptions;
   driOptionCache userInitOptions;
   char *dri_driver = NULL;
   char *kernel_driver = loader_get_kernel_driver_name(fd);

   driParseOptionInfo(&defaultInitOptions, __driConfigOptionsLoader);
   driParseConfigFiles(&userInitOptions, &defaultInitOptions, 0,
                       "loader", kernel_driver);
   if (driCheckOption(&userInitOptions, "dri_driver", DRI_STRING)) {
      char *opt = driQueryOptionstr(&userInitOptions, "dri_driver");
      /* not an empty string */
      if (*opt)
         dri_driver = strdup(opt);
   }
   driDestroyOptionCache(&userInitOptions);
   driDestroyOptionInfo(&defaultInitOptions);

   free(kernel_driver);
   return dri_driver;
}

static char *loader_get_dri_config_device_id(void)
{
   driOptionCache defaultInitOptions;
   driOptionCache userInitOptions;
   char *prime = NULL;

   driParseOptionInfo(&defaultInitOptions, __driConfigOptionsLoader);
   driParseConfigFiles(&userInitOptions, &defaultInitOptions, 0, "loader", NULL);
   if (driCheckOption(&userInitOptions, "device_id", DRI_STRING))
      prime = strdup(driQueryOptionstr(&userInitOptions, "device_id"));
   driDestroyOptionCache(&userInitOptions);
   driDestroyOptionInfo(&defaultInitOptions);

   return prime;
}
#endif

static char *drm_construct_id_path_tag(drmDevicePtr device)
{
   char *tag = NULL;

   if (device->bustype == DRM_BUS_PCI) {
      if (asprintf(&tag, "pci-%04x_%02x_%02x_%1u",
                   device->businfo.pci->domain,
                   device->businfo.pci->bus,
                   device->businfo.pci->dev,
                   device->businfo.pci->func) < 0) {
         return NULL;
      }
   } else if (device->bustype == DRM_BUS_PLATFORM ||
              device->bustype == DRM_BUS_HOST1X) {
      char *fullname, *name, *address;

      if (device->bustype == DRM_BUS_PLATFORM)
         fullname = device->businfo.platform->fullname;
      else
         fullname = device->businfo.host1x->fullname;

      name = strrchr(fullname, '/');
      if (!name)
         name = strdup(fullname);
      else
         name = strdup(name + 1);

      address = strchr(name, '@');
      if (address) {
         *address++ = '\0';

         if (asprintf(&tag, "platform-%s_%s", address, name) < 0)
            tag = NULL;
      } else {
         if (asprintf(&tag, "platform-%s", name) < 0)
            tag = NULL;
      }

      free(name);
   }
   return tag;
}

static bool drm_device_matches_tag(drmDevicePtr device, const char *prime_tag)
{
   char *tag = drm_construct_id_path_tag(device);
   int ret;

   if (tag == NULL)
      return false;

   ret = strcmp(tag, prime_tag);

   free(tag);
   return ret == 0;
}

static char *drm_get_id_path_tag_for_fd(int fd)
{
   drmDevicePtr device;
   char *tag;

   if (drmGetDevice2(fd, 0, &device) != 0)
       return NULL;

   tag = drm_construct_id_path_tag(device);
   drmFreeDevice(&device);
   return tag;
}

int loader_get_user_preferred_fd(int default_fd, bool *different_device)
{
/* Arbitrary "maximum" value of drm devices. */
#define MAX_DRM_DEVICES 32
   const char *dri_prime = getenv("DRI_PRIME");
   char *default_tag, *prime = NULL;
   drmDevicePtr devices[MAX_DRM_DEVICES];
   int i, num_devices, fd;
   bool found = false;

   if (dri_prime)
      prime = strdup(dri_prime);
#ifdef USE_DRICONF
   else
      prime = loader_get_dri_config_device_id();
#endif

   if (prime == NULL) {
      *different_device = false;
      return default_fd;
   }

   default_tag = drm_get_id_path_tag_for_fd(default_fd);
   if (default_tag == NULL)
      goto err;

   num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
   if (num_devices < 0)
      goto err;

   /* two format are supported:
    * "1": choose any other card than the card used by default.
    * id_path_tag: (for example "pci-0000_02_00_0") choose the card
    * with this id_path_tag.
    */
   if (!strcmp(prime,"1")) {
      /* Hmm... detection for 2-7 seems to be broken. Oh well ...
       * Pick the first render device that is not our own.
       */
      for (i = 0; i < num_devices; i++) {
         if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
             !drm_device_matches_tag(devices[i], default_tag)) {

            found = true;
            break;
         }
      }
   } else {
      for (i = 0; i < num_devices; i++) {
         if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
            drm_device_matches_tag(devices[i], prime)) {

            found = true;
            break;
         }
      }
   }

   if (!found) {
      drmFreeDevices(devices, num_devices);
      goto err;
   }

   fd = loader_open_device(devices[i]->nodes[DRM_NODE_RENDER]);
   drmFreeDevices(devices, num_devices);
   if (fd < 0)
      goto err;

   close(default_fd);

   *different_device = !!strcmp(default_tag, prime);

   free(default_tag);
   free(prime);
   return fd;

 err:
   *different_device = false;

   free(default_tag);
   free(prime);
   return default_fd;
}
#else
int
loader_open_render_node(const char *name)
{
   return -1;
}

int loader_get_user_preferred_fd(int default_fd, bool *different_device)
{
   *different_device = false;
   return default_fd;
}
#endif

#if defined(HAVE_LIBDRM)

static int
drm_get_pci_id_for_fd(int fd, int *vendor_id, int *chip_id)
{
   drmDevicePtr device;
   int ret;

   if (drmGetDevice2(fd, 0, &device) == 0) {
      if (device->bustype == DRM_BUS_PCI) {
         *vendor_id = device->deviceinfo.pci->vendor_id;
         *chip_id = device->deviceinfo.pci->device_id;
         ret = 1;
      }
      else {
         log_(_LOADER_DEBUG, "MESA-LOADER: device is not located on the PCI bus\n");
         ret = 0;
      }
      drmFreeDevice(&device);
   }
   else {
      log_(_LOADER_WARNING, "MESA-LOADER: failed to retrieve device information\n");
      ret = 0;
   }

   return ret;
}
#endif


int
loader_get_pci_id_for_fd(int fd, int *vendor_id, int *chip_id)
{
#if HAVE_LIBDRM
   if (drm_get_pci_id_for_fd(fd, vendor_id, chip_id))
      return 1;
#endif
   return 0;
}

char *
loader_get_device_name_for_fd(int fd)
{
   char *result = NULL;

#if HAVE_LIBDRM
   result = drmGetDeviceNameFromFd2(fd);
#endif

   return result;
}

char *
loader_get_driver_for_fd(int fd)
{
   int vendor_id, chip_id, i, j;
   char *driver = NULL;

   /* Allow an environment variable to force choosing a different driver
    * binary.  If that driver binary can't survive on this FD, that's the
    * user's problem, but this allows vc4 simulator to run on an i965 host,
    * and may be useful for some touch testing of i915 on an i965 host.
    */
   if (geteuid() == getuid()) {
      driver = getenv("MESA_LOADER_DRIVER_OVERRIDE");
      if (driver)
         return strdup(driver);
   }

#if defined(HAVE_LIBDRM) && defined(USE_DRICONF)
   driver = loader_get_dri_config_driver(fd);
   if (driver)
      return driver;
#endif

   if (!loader_get_pci_id_for_fd(fd, &vendor_id, &chip_id)) {
      driver = loader_get_kernel_driver_name(fd);
      if (driver)
         log_(_LOADER_INFO, "using driver %s for %d\n", driver, fd);
      return driver;
   }

   for (i = 0; driver_map[i].driver; i++) {
      if (vendor_id != driver_map[i].vendor_id)
         continue;

      if (driver_map[i].predicate && !driver_map[i].predicate(fd))
         continue;

      if (driver_map[i].num_chips_ids == -1) {
         driver = strdup(driver_map[i].driver);
         goto out;
      }

      for (j = 0; j < driver_map[i].num_chips_ids; j++)
         if (driver_map[i].chip_ids[j] == chip_id) {
            driver = strdup(driver_map[i].driver);
            goto out;
         }
   }

out:
   log_(driver ? _LOADER_DEBUG : _LOADER_WARNING,
         "pci id for fd %d: %04x:%04x, driver %s\n",
         fd, vendor_id, chip_id, driver);
   return driver;
}

void
loader_set_logger(void (*logger)(int level, const char *fmt, ...))
{
   log_ = logger;
}

char *
loader_get_extensions_name(const char *driver_name)
{
   char *name = NULL;

   if (asprintf(&name, "%s_%s", __DRI_DRIVER_GET_EXTENSIONS, driver_name) < 0)
      return NULL;

   const size_t len = strlen(name);
   for (size_t i = 0; i < len; i++) {
      if (name[i] == '-')
         name[i] = '_';
   }

   return name;
}

/**
 * Opens a DRI driver using its driver name, returning the __DRIextension
 * entrypoints.
 *
 * \param driverName - a name like "i965", "radeon", "nouveau", etc.
 * \param out_driver - Address where the dlopen() return value will be stored.
 * \param search_path_vars - NULL-terminated list of env vars that can be used
 * to override the DEFAULT_DRIVER_DIR search path.
 */
const struct __DRIextensionRec **
loader_open_driver(const char *driver_name,
                   void **out_driver_handle,
                   const char **search_path_vars)
{
   char path[PATH_MAX], *search_paths, *next, *end;
   char *get_extensions_name;
   const struct __DRIextensionRec **extensions = NULL;
   const struct __DRIextensionRec **(*get_extensions)(void);

   search_paths = NULL;
   if (geteuid() == getuid() && search_path_vars) {
      for (int i = 0; search_path_vars[i] != NULL; i++) {
         search_paths = getenv(search_path_vars[i]);
         if (search_paths)
            break;
      }
   }
   if (search_paths == NULL)
      search_paths = DEFAULT_DRIVER_DIR;

   void *driver = NULL;
   end = search_paths + strlen(search_paths);
   for (char *p = search_paths; p < end; p = next + 1) {
      int len;
      next = strchr(p, ':');
      if (next == NULL)
         next = end;

      len = next - p;
#if GLX_USE_TLS
      snprintf(path, sizeof(path), "%.*s/tls/%s_dri.so", len, p, driver_name);
      driver = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
      if (driver == NULL) {
         snprintf(path, sizeof(path), "%.*s/%s_dri.so", len, p, driver_name);
         driver = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
         if (driver == NULL)
            log_(_LOADER_DEBUG, "MESA-LOADER: failed to open %s: %s\n",
                 path, dlerror());
      }
      /* not need continue to loop all paths once the driver is found */
      if (driver != NULL)
         break;
   }

   if (driver == NULL) {
      log_(_LOADER_WARNING, "MESA-LOADER: failed to open %s (search paths %s)\n",
           driver_name, search_paths);
      *out_driver_handle = NULL;
      return NULL;
   }

   log_(_LOADER_DEBUG, "MESA-LOADER: dlopen(%s)\n", path);

   get_extensions_name = loader_get_extensions_name(driver_name);
   if (get_extensions_name) {
      get_extensions = dlsym(driver, get_extensions_name);
      if (get_extensions) {
         extensions = get_extensions();
      } else {
         log_(_LOADER_DEBUG, "MESA-LOADER: driver does not expose %s(): %s\n",
              get_extensions_name, dlerror());
      }
      free(get_extensions_name);
   }

   if (!extensions)
      extensions = dlsym(driver, __DRI_DRIVER_EXTENSIONS);
   if (extensions == NULL) {
      log_(_LOADER_WARNING,
           "MESA-LOADER: driver exports no extensions (%s)\n", dlerror());
      dlclose(driver);
   }

   *out_driver_handle = driver;
   return extensions;
}
