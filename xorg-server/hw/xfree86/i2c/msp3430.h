#ifndef __MSP3430_H__
#define __MSP3430_H__

#include "xf86i2c.h"

typedef struct {
    I2CDevRec d;

    int standard;
    int connector;
    int mode;

    CARD8 hardware_version, major_revision, product_code, rom_version;
#ifdef MSP_DEBUG
    CARD8 registers_present[256];
#endif

    CARD16 chip_id;
    CARD8 chip_family;
    Bool recheck;               /*reinitialization needed after channel change */
    CARD8 c_format;             /*current state of audio format */
    CARD16 c_standard;          /*current state of standard register */
    CARD8 c_source;             /*current state of source register */
    CARD8 c_matrix;             /*current state of matrix register */
    CARD8 c_fmmatrix;           /*current state of fmmatrix register */
    int c_mode;                 /* current state of mode for autoswitchimg */
    CARD8 volume;
} MSP3430Rec, *MSP3430Ptr;

#define MSP3430_ADDR_1      0x80
#define MSP3430_ADDR_2		0x84
#define MSP3430_ADDR_3		0x88

#define MSP3430_PAL		1
#define MSP3430_NTSC		2
#define MSP3430_PAL_DK1         (0x100 | MSP3430_PAL)
#define MSP3430_SECAM           3

#define MSP3430_CONNECTOR_1     1       /* tuner on AIW cards */
#define MSP3430_CONNECTOR_2     2       /* SVideo on AIW cards */
#define MSP3430_CONNECTOR_3     3       /* composite on AIW cards */

#define MSP3430_ADDR(a)         ((a)->d.SlaveAddr)

#define MSP3430_FAST_MUTE	0xFF
/* a handy volume transform function, -1000..1000 -> 0x01..0x7F */
#define MSP3430_VOLUME(value) (0x01+(0x7F-0x01)*log(value+1001)/log(2001))

/*----------------------------------------------------------*/

/* MSP chip families */
#define MSPFAMILY_UNKNOWN	0
#define MSPFAMILY_34x0D		1
#define MSPFAMILY_34x5D		2
#define MSPFAMILY_34x0G		3
#define MSPFAMILY_34x5G		4

/* values for MSP standard */
#define MSPSTANDARD_UNKNOWN	0x00
#define MSPSTANDARD_AUTO	0x01
#define MSPSTANDARD_FM_M	0x02
#define MSPSTANDARD_FM_BG	0x03
#define MSPSTANDARD_FM_DK1	0x04
#define MSPSTANDARD_FM_DK2	0x04
#define MSPSTANDARD_NICAM_BG	0x08
#define MSPSTANDARD_NICAM_L	0x09
#define MSPSTANDARD_NICAM_I	0x0A
#define MSPSTANDARD_NICAM_DK	0x0B

/* values for MSP format */
#define MSPFORMAT_UNKNOWN	0x00
#define MSPFORMAT_FM		0x10
#define MSPFORMAT_1xFM		0x00|MSPFORMAT_FM
#define MSPFORMAT_2xFM		0x01|MSPFORMAT_FM
#define MSPFORMAT_NICAM		0x20
#define MSPFORMAT_NICAM_FM	0x00|MSPFORMAT_NICAM
#define MSPFORMAT_NICAM_AM	0x01|MSPFORMAT_NICAM
#define MSPFORMAT_SCART		0x30

/* values for MSP mode */
#define MSPMODE_UNKNOWN		0
/* automatic modes */
#define MSPMODE_STEREO_AB	1
#define MSPMODE_STEREO_A	2
#define MSPMODE_STEREO_B	3
/* forced modes */
#define MSPMODE_MONO		4
#define MSPMODE_STEREO		5
#define MSPMODE_AB			6
#define MSPMODE_A			7
#define MSPMODE_B			8
/*----------------------------------------------------------*/

#define xf86_InitMSP3430	InitMSP3430
extern _X_EXPORT void InitMSP3430(MSP3430Ptr m);

#define xf86_DetectMSP3430	DetectMSP3430
extern _X_EXPORT MSP3430Ptr DetectMSP3430(I2CBusPtr b, I2CSlaveAddr addr);

#define xf86_ResetMSP3430	ResetMSP3430
extern _X_EXPORT void ResetMSP3430(MSP3430Ptr m);

#define xf86_MSP3430SetVolume	MSP3430SetVolume
extern _X_EXPORT void MSP3430SetVolume(MSP3430Ptr m, CARD8 value);

#define xf86_MSP3430SetSAP	MSP3430SetSAP
extern _X_EXPORT void MSP3430SetSAP(MSP3430Ptr m, int mode);

#define MSP3430SymbolsList \
		"InitMSP3430", \
		"DetectMSP3430", \
		"ResetMSP3430", \
		"MSP3430SetVolume", \
		"MSP3430SetSAP"

#endif
