
#include <xf86RamDac.h>

extern _X_EXPORT unsigned long TIramdacCalculateMNPForClock(unsigned long
                                                            RefClock,
                                                            unsigned long
                                                            ReqClock,
                                                            char IsPixClock,
                                                            unsigned long
                                                            MinClock,
                                                            unsigned long
                                                            MaxClock,
                                                            unsigned long *rM,
                                                            unsigned long *rN,
                                                            unsigned long *rP);
extern _X_EXPORT RamDacHelperRecPtr TIramdacProbe(ScrnInfoPtr pScrn,
                                                  RamDacSupportedInfoRecPtr
                                                  ramdacs);
extern _X_EXPORT void TIramdacSave(ScrnInfoPtr pScrn, RamDacRecPtr RamDacRec,
                                   RamDacRegRecPtr RamDacRegRec);
extern _X_EXPORT void TIramdacRestore(ScrnInfoPtr pScrn, RamDacRecPtr RamDacRec,
                                      RamDacRegRecPtr RamDacRegRec);
extern _X_EXPORT void TIramdac3026SetBpp(ScrnInfoPtr pScrn,
                                         RamDacRegRecPtr RamDacRegRec);
extern _X_EXPORT void TIramdac3030SetBpp(ScrnInfoPtr pScrn,
                                         RamDacRegRecPtr RamDacRegRec);
extern _X_EXPORT void TIramdacHWCursorInit(xf86CursorInfoPtr infoPtr);
extern _X_EXPORT void TIramdacLoadPalette(ScrnInfoPtr pScrn, int numColors,
                                          int *indices, LOCO * colors,
                                          VisualPtr pVisual);

typedef void TIramdacLoadPaletteProc(ScrnInfoPtr, int, int *, LOCO *,
                                     VisualPtr);
extern _X_EXPORT TIramdacLoadPaletteProc *TIramdacLoadPaletteWeak(void);

#define TI3030_RAMDAC		(VENDOR_TI << 16) | 0x00
#define TI3026_RAMDAC		(VENDOR_TI << 16) | 0x01

/*
 * TI Ramdac registers
 */

#define TIDAC_rev		0x01
#define TIDAC_ind_curs_ctrl	0x06
#define TIDAC_byte_router_ctrl	0x07
#define TIDAC_latch_ctrl	0x0f
#define TIDAC_true_color_ctrl	0x18
#define TIDAC_multiplex_ctrl	0x19
#define TIDAC_clock_select	0x1a
#define TIDAC_palette_page	0x1c
#define TIDAC_general_ctrl	0x1d
#define TIDAC_misc_ctrl		0x1e
#define TIDAC_pll_addr		0x2c
#define TIDAC_pll_pixel_data	0x2d
#define TIDAC_pll_memory_data	0x2e
#define TIDAC_pll_loop_data	0x2f
#define TIDAC_key_over_low	0x30
#define TIDAC_key_over_high	0x31
#define TIDAC_key_red_low	0x32
#define TIDAC_key_red_high	0x33
#define TIDAC_key_green_low	0x34
#define TIDAC_key_green_high	0x35
#define TIDAC_key_blue_low	0x36
#define TIDAC_key_blue_high	0x37
#define TIDAC_key_ctrl		0x38
#define TIDAC_clock_ctrl	0x39
#define TIDAC_sense_test	0x3a
#define TIDAC_test_mode_data	0x3b
#define TIDAC_crc_remain_lsb	0x3c
#define TIDAC_crc_remain_msb	0x3d
#define TIDAC_crc_bit_select	0x3e
#define TIDAC_id		0x3f

/* These are pll values that are accessed via TIDAC_pll_pixel_data */
#define TIDAC_PIXEL_N		0x80
#define TIDAC_PIXEL_M		0x81
#define TIDAC_PIXEL_P		0x82
#define TIDAC_PIXEL_VALID	0x83

/* These are pll values that are accessed via TIDAC_pll_loop_data */
#define TIDAC_LOOP_N		0x90
#define TIDAC_LOOP_M		0x91
#define TIDAC_LOOP_P		0x92
#define TIDAC_LOOP_VALID	0x93

/* Direct mapping addresses */
#define TIDAC_INDEX		0xa0
#define TIDAC_PALETTE_DATA	0xa1
#define TIDAC_READ_MASK		0xa2
#define TIDAC_READ_ADDR		0xa3
#define TIDAC_CURS_WRITE_ADDR	0xa4
#define TIDAC_CURS_COLOR	0xa5
#define TIDAC_CURS_READ_ADDR	0xa7
#define TIDAC_CURS_CTL		0xa9
#define TIDAC_INDEXED_DATA	0xaa
#define TIDAC_CURS_RAM_DATA	0xab
#define TIDAC_CURS_XLOW		0xac
#define TIDAC_CURS_XHIGH	0xad
#define TIDAC_CURS_YLOW		0xae
#define TIDAC_CURS_YHIGH	0xaf

#define TIDAC_sw_reset		0xff

/* Constants */
#define TIDAC_TVP_3026_ID       0x26
#define TIDAC_TVP_3030_ID       0x30
