import argparse
import copy
import re
import xml.etree.ElementTree as et

def _bool_to_c_expr(b):
    if b is True:
        return 'true'
    if b is False:
        return 'false'
    return b

class Extension:
    def __init__(self, name, ext_version, enable):
        self.name = name
        self.ext_version = int(ext_version)
        self.enable = _bool_to_c_expr(enable)

    def c_android_condition(self):
        # if it's an EXT or vendor extension, it's allowed
        if not self.name.startswith(ANDROID_EXTENSION_WHITELIST_PREFIXES):
            return 'true'

        allowed_version = ALLOWED_ANDROID_VERSION.get(self.name, None)
        if allowed_version is None:
            return 'false'

        return 'ANDROID_API_LEVEL >= %d' % (allowed_version)

class ApiVersion:
    def __init__(self, version, enable):
        self.version = version
        self.enable = _bool_to_c_expr(enable)

class VkVersion:
    def __init__(self, string):
        split = string.split('.')
        self.major = int(split[0])
        self.minor = int(split[1])
        if len(split) > 2:
            assert len(split) == 3
            self.patch = int(split[2])
        else:
            self.patch = None

        # Sanity check.  The range bits are required by the definition of the
        # VK_MAKE_VERSION macro
        assert self.major < 1024 and self.minor < 1024
        assert self.patch is None or self.patch < 4096
        assert(str(self) == string)

    def __str__(self):
        ver_list = [str(self.major), str(self.minor)]
        if self.patch is not None:
            ver_list.append(str(self.patch))
        return '.'.join(ver_list)

    def c_vk_version(self):
        patch = self.patch if self.patch is not None else 0
        ver_list = [str(self.major), str(self.minor), str(patch)]
        return 'VK_MAKE_VERSION(' + ', '.join(ver_list) + ')'

    def __int_ver(self):
        # This is just an expansion of VK_VERSION
        patch = self.patch if self.patch is not None else 0
        return (self.major << 22) | (self.minor << 12) | patch

    def __gt__(self, other):
        # If only one of them has a patch version, "ignore" it by making
        # other's patch version match self.
        if (self.patch is None) != (other.patch is None):
            other = copy.copy(other)
            other.patch = self.patch

        return self.__int_ver() > other.__int_ver()

# Sort the extension list the way we expect: KHR, then EXT, then vendors
# alphabetically. For digits, read them as a whole number sort that.
# eg.: VK_KHR_8bit_storage < VK_KHR_16bit_storage < VK_EXT_acquire_xlib_display
def extension_order(ext):
    order = []
    for substring in re.split('(KHR|EXT|[0-9]+)', ext.name):
        if substring == 'KHR':
            order.append(1)
        if substring == 'EXT':
            order.append(2)
        elif substring.isdigit():
            order.append(int(substring))
        else:
            order.append(substring)
    return order

def get_all_exts_from_xml(xml):
    """ Get a list of all Vulkan extensions. """

    xml = et.parse(xml)

    extensions = []
    for ext_elem in xml.findall('.extensions/extension'):
        supported = ext_elem.attrib['supported'] == 'vulkan'
        name = ext_elem.attrib['name']
        if not supported and name != 'VK_ANDROID_native_buffer':
            continue
        version = None
        for enum_elem in ext_elem.findall('.require/enum'):
            if enum_elem.attrib['name'].endswith('_SPEC_VERSION'):
                # Skip alias SPEC_VERSIONs
                if 'value' in enum_elem.attrib:
                    assert version is None
                    version = int(enum_elem.attrib['value'])
        ext = Extension(name, version, True)
        extensions.append(Extension(name, version, True))

    return sorted(extensions, key=extension_order)

def init_exts_from_xml(xml, extensions, platform_defines):
    """ Walk the Vulkan XML and fill out extra extension information. """

    xml = et.parse(xml)

    ext_name_map = {}
    for ext in extensions:
        ext_name_map[ext.name] = ext

    # KHR_display is missing from the list.
    platform_defines.append('VK_USE_PLATFORM_DISPLAY_KHR')
    for platform in xml.findall('./platforms/platform'):
        platform_defines.append(platform.attrib['protect'])

    for ext_elem in xml.findall('.extensions/extension'):
        ext_name = ext_elem.attrib['name']
        if ext_name not in ext_name_map:
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

# Mapping between extension name and the android version in which the extension
# was whitelisted in Android CTS.
ALLOWED_ANDROID_VERSION = {
    # Allowed Instance KHR Extensions
    "VK_KHR_surface": 26,
    "VK_KHR_display": 26,
    "VK_KHR_android_surface": 26,
    "VK_KHR_mir_surface": 26,
    "VK_KHR_wayland_surface": 26,
    "VK_KHR_win32_surface": 26,
    "VK_KHR_xcb_surface": 26,
    "VK_KHR_xlib_surface": 26,
    "VK_KHR_get_physical_device_properties2": 26,
    "VK_KHR_get_surface_capabilities2": 26,
    "VK_KHR_external_memory_capabilities": 28,
    "VK_KHR_external_semaphore_capabilities": 28,
    "VK_KHR_external_fence_capabilities": 28,
    "VK_KHR_device_group_creation": 28,
    "VK_KHR_get_display_properties2": 29,
    "VK_KHR_surface_protected_capabilities": 29,

    # Allowed Device KHR Extensions
    "VK_KHR_swapchain": 26,
    "VK_KHR_display_swapchain": 26,
    "VK_KHR_sampler_mirror_clamp_to_edge": 26,
    "VK_KHR_shader_draw_parameters": 26,
    "VK_KHR_shader_float_controls": 29,
    "VK_KHR_shader_float16_int8": 29,
    "VK_KHR_maintenance1": 26,
    "VK_KHR_push_descriptor": 26,
    "VK_KHR_descriptor_update_template": 26,
    "VK_KHR_incremental_present": 26,
    "VK_KHR_shared_presentable_image": 26,
    "VK_KHR_storage_buffer_storage_class": 28,
    "VK_KHR_8bit_storage": 29,
    "VK_KHR_16bit_storage": 28,
    "VK_KHR_get_memory_requirements2": 28,
    "VK_KHR_external_memory": 28,
    "VK_KHR_external_memory_fd": 28,
    "VK_KHR_external_memory_win32": 28,
    "VK_KHR_external_semaphore": 28,
    "VK_KHR_external_semaphore_fd": 28,
    "VK_KHR_external_semaphore_win32": 28,
    "VK_KHR_external_fence": 28,
    "VK_KHR_external_fence_fd": 28,
    "VK_KHR_external_fence_win32": 28,
    "VK_KHR_win32_keyed_mutex": 28,
    "VK_KHR_dedicated_allocation": 28,
    "VK_KHR_variable_pointers": 28,
    "VK_KHR_relaxed_block_layout": 28,
    "VK_KHR_bind_memory2": 28,
    "VK_KHR_maintenance2": 28,
    "VK_KHR_image_format_list": 28,
    "VK_KHR_sampler_ycbcr_conversion": 28,
    "VK_KHR_device_group": 28,
    "VK_KHR_multiview": 28,
    "VK_KHR_maintenance3": 28,
    "VK_KHR_draw_indirect_count": 28,
    "VK_KHR_create_renderpass2": 28,
    "VK_KHR_depth_stencil_resolve": 29,
    "VK_KHR_driver_properties": 28,
    "VK_KHR_swapchain_mutable_format": 29,
    "VK_KHR_shader_atomic_int64": 29,
    "VK_KHR_vulkan_memory_model": 29,
    "VK_KHR_performance_query": 30,

    "VK_GOOGLE_display_timing": 26,
    "VK_ANDROID_native_buffer": 26,
    "VK_ANDROID_external_memory_android_hardware_buffer": 28,
}

# Extensions with these prefixes are checked in Android CTS, and thus must be
# whitelisted per the preceding dict.
ANDROID_EXTENSION_WHITELIST_PREFIXES = (
    "VK_KHX",
    "VK_KHR",
    "VK_GOOGLE",
    "VK_ANDROID"
)
