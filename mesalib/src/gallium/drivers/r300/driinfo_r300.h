/* DriConf options specific to r300 */
DRI_CONF_SECTION_DEBUG
#define OPT_BOOL(name, dflt, description) DRI_CONF_OPT_B(r300_##name, dflt, description)

#include "r300_debug_options.h"
DRI_CONF_SECTION_END
