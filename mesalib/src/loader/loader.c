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
#include <limits.h>
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
#define MAX_DRM_DEVICES 64
#ifdef USE_DRICONF
#include "util/xmlconfig.h"
#include "util/driconf.h"
#endif
#endif

#include "util/macros.h"

#define __IS_LOADER
#include "pci_id_driver_map.h"

/* For systems like Hurd */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void default_logger(int level, const char *fmt, ...)
{
   if (level <= _LOADER_WARNING) {
      va_list args;
      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
   }
}

static loader_logger *log_ = default_logger;

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
   if (fd == -1 && errno == EACCES) {
      log_(_LOADER_WARNING, "failed to open %s: %s\n",
           device_name, strerror(errno));
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
   log_(driver ? _LOADER_DEBUG : _LOADER_WARNING, "using driver %s for %d\n",
        driver, fd);

   drmFreeVersion(version);
   return driver;
#else
   return NULL;
#endif
}

bool
is_kernel_i915(int fd)
{
   char *kernel_driver = loader_get_kernel_driver_name(fd);
   bool is_i915 = kernel_driver && strcmp(kernel_driver, "i915") == 0;

   free(kernel_driver);
   return is_i915;
}

#if defined(HAVE_LIBDRM)
int
loader_open_render_node(const char *name)
{
   drmDevicePtr devices[MAX_DRM_DEVICES], device;
   int i, num_devices, fd = -1;

   num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
   if (num_devices <= 0)
      return -ENOENT;

   for (i = 0; i < num_devices; i++) {
      device = devices[i];

      if ((device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          (device->bustype == DRM_BUS_PLATFORM)) {
         drmVersionPtr version;

         fd = loader_open_device(device->nodes[DRM_NODE_RENDER]);
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
         break;
      }
   }
   drmFreeDevices(devices, num_devices);

   if (i == num_devices)
      return -ENOENT;

   return fd;
}

#ifdef USE_DRICONF
static const driOptionDescription __driConfigOptionsLoader[] = {
    DRI_CONF_SECTION_INITIALIZATION
        DRI_CONF_DEVICE_ID_PATH_TAG()
        DRI_CONF_DRI_DRIVER()
    DRI_CONF_SECTION_END
};

static char *loader_get_dri_config_driver(int fd)
{
   driOptionCache defaultInitOptions;
   driOptionCache userInitOptions;
   char *dri_driver = NULL;
   char *kernel_driver = loader_get_kernel_driver_name(fd);

   driParseOptionInfo(&defaultInitOptions, __driConfigOptionsLoader,
                      ARRAY_SIZE(__driConfigOptionsLoader));
   driParseConfigFiles(&userInitOptions, &defaultInitOptions, 0,
                       "loader", kernel_driver, NULL, 0, NULL, 0);
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

   driParseOptionInfo(&defaultInitOptions, __driConfigOptionsLoader,
                      ARRAY_SIZE(__driConfigOptionsLoader));
   driParseConfigFiles(&userInitOptions, &defaultInitOptions, 0,
                       "loader", NULL, NULL, 0, NULL, 0);
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
   const char *dri_prime = getenv("DRI_PRIME");
   char *default_tag, *prime = NULL;
   drmDevicePtr devices[MAX_DRM_DEVICES];
   int i, num_devices, fd = -1;

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
   if (num_devices <= 0)
      goto err;

   for (i = 0; i < num_devices; i++) {
      if (!(devices[i]->available_nodes & 1 << DRM_NODE_RENDER))
         continue;

      /* two formats of DRI_PRIME are supported:
       * "1": choose any other card than the card used by default.
       * id_path_tag: (for example "pci-0000_02_00_0") choose the card
       * with this id_path_tag.
       */
      if (!strcmp(prime,"1")) {
         if (drm_device_matches_tag(devices[i], default_tag))
            continue;
      } else {
         if (!drm_device_matches_tag(devices[i], prime))
            continue;
      }

      fd = loader_open_device(devices[i]->nodes[DRM_NODE_RENDER]);
      break;
   }
   drmFreeDevices(devices, num_devices);

   if (i == num_devices)
      goto err;

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

static bool
drm_get_pci_id_for_fd(int fd, int *vendor_id, int *chip_id)
{
   drmDevicePtr device;

   if (drmGetDevice2(fd, 0, &device) != 0) {
      log_(_LOADER_WARNING, "MESA-LOADER: failed to retrieve device information\n");
      return false;
   }

   if (device->bustype != DRM_BUS_PCI) {
      drmFreeDevice(&device);
      log_(_LOADER_DEBUG, "MESA-LOADER: device is not located on the PCI bus\n");
      return false;
   }

   *vendor_id = device->deviceinfo.pci->vendor_id;
   *chip_id = device->deviceinfo.pci->device_id;
   drmFreeDevice(&device);
   return true;
}
#endif


bool
loader_get_pci_id_for_fd(int fd, int *vendor_id, int *chip_id)
{
#if HAVE_LIBDRM
   return drm_get_pci_id_for_fd(fd, vendor_id, chip_id);
#endif
   return false;
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

static char *
loader_get_pci_driver(int fd)
{
   int vendor_id, chip_id, i, j;
   char *driver = NULL;

   if (!loader_get_pci_id_for_fd(fd, &vendor_id, &chip_id))
      return NULL;

   for (i = 0; i < ARRAY_SIZE(driver_map); i++) {
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

char *
loader_get_driver_for_fd(int fd)
{
   char *driver;

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

   driver = loader_get_pci_driver(fd);
   if (!driver)
      driver = loader_get_kernel_driver_name(fd);

   return driver;
}

void
loader_set_logger(loader_logger *logger)
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
   char *dl_error = NULL;
   end = search_paths + strlen(search_paths);
   for (char *p = search_paths; p < end; p = next + 1) {
      int len;
      next = strchr(p, ':');
      if (next == NULL)
         next = end;

      len = next - p;
#if USE_ELF_TLS
      snprintf(path, sizeof(path), "%.*s/tls/%s_dri.so", len, p, driver_name);
      driver = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
      if (driver == NULL) {
         snprintf(path, sizeof(path), "%.*s/%s_dri.so", len, p, driver_name);
         driver = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
         if (driver == NULL) {
            dl_error = dlerror();
            log_(_LOADER_DEBUG, "MESA-LOADER: failed to open %s: %s\n",
                 path, dl_error);
         }
      }
      /* not need continue to loop all paths once the driver is found */
      if (driver != NULL)
         break;
   }

   if (driver == NULL) {
      log_(_LOADER_WARNING, "MESA-LOADER: failed to open %s: %s (search paths %s)\n",
           driver_name, dl_error, search_paths);
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
