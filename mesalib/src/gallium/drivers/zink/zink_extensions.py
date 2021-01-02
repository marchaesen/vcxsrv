class Extension:
    name           : str   = None
    alias          : str   = None
    is_required    : bool  = False
    has_properties : bool  = False
    has_features   : bool  = False
    enable_conds   : [str] = None
    guard          : bool  = False

    def __init__(self, name, alias="", required=False, properties=False, features=False, conditions=None, guard=False):
        self.name = name
        self.alias = alias
        self.is_required = required
        self.has_properties = properties
        self.has_features = features
        self.enable_conds = conditions
        self.guard = guard

        if alias == "" and (properties == True or features == True):
            raise RuntimeError("alias must be available when properties and/or features are used")

    # e.g.: "VK_EXT_robustness2" -> "robustness2"
    def pure_name(self):
        return '_'.join(self.name.split('_')[2:])
    
    # e.g.: "VK_EXT_robustness2" -> "EXT_robustness2"
    def name_with_vendor(self):
        return self.name[3:]
    
    # e.g.: "VK_EXT_robustness2" -> "Robustness2"
    def name_in_camel_case(self):
        return "".join([x.title() for x in self.name.split('_')[2:]])
    
    # e.g.: "VK_EXT_robustness2" -> "VK_EXT_ROBUSTNESS2_EXTENSION_NAME"
    # do note that inconsistencies exist, i.e. we have
    # VK_EXT_ROBUSTNESS_2_EXTENSION_NAME defined in the headers, but then
    # we also have VK_KHR_MAINTENANCE1_EXTENSION_NAME
    def extension_name(self):
        return self.name.upper() + "_EXTENSION_NAME"

    # generate a C string literal for the extension
    def extension_name_literal(self):
        return '"' + self.name + '"'

    # get the field in zink_device_info that refers to the extension's
    # feature/properties struct
    # e.g. rb2_<suffix> for VK_EXT_robustness2
    def field(self, suffix: str):
        return self.alias + '_' + suffix

    # the sType of the extension's struct
    # e.g. VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
    # for VK_EXT_transform_feedback and struct="FEATURES"
    def stype(self, struct: str):
        return ("VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_" 
                + self.pure_name().upper()
                + '_' + struct + '_' 
                + self.vendor())

    # e.g. EXT in VK_EXT_robustness2
    def vendor(self):
        return self.name.split('_')[1]


class Version:
    driver_version  : (1,0,0)
    struct_version  : (1,0)

    def __init__(self, version, struct):
        self.device_version = version
        self.struct_version = struct

    # e.g. "VM_MAKE_VERSION(1,2,0)"
    def version(self):
        return ("VK_MAKE_VERSION("
               + str(self.device_version[0])
               + ","
               + str(self.device_version[1])
               + ","
               + str(self.device_version[2])
               + ")")

    # e.g. "10"
    def struct(self):
        return (str(self.struct_version[0])+str(self.struct_version[1]))

    # the sType of the extension's struct
    # e.g. VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
    # for VK_EXT_transform_feedback and struct="FEATURES"
    def stype(self, struct: str):
        return ("VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_"
                + str(self.struct_version[0]) + "_" + str(self.struct_version[1])
                + '_' + struct)

# Type aliases
Layer = Extension
