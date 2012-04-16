
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "TI.h"

typedef struct {
    const char *DeviceName;
} xf86TIramdacInfo;

extern xf86TIramdacInfo TIramdacDeviceInfo[];

#ifdef INIT_TI_RAMDAC_INFO
xf86TIramdacInfo TIramdacDeviceInfo[] = {
    {"TI TVP3030"},
    {"TI TVP3026"}
};
#endif

#define TISAVE(_reg) do { 						\
    ramdacReg->DacRegs[_reg] = (*ramdacPtr->ReadDAC)(pScrn, _reg);	\
} while (0)

#define TIRESTORE(_reg) do { 						\
    (*ramdacPtr->WriteDAC)(pScrn, _reg, 				\
	(ramdacReg->DacRegs[_reg] & 0xFF00) >> 8, 			\
	ramdacReg->DacRegs[_reg]);					\
} while (0)
