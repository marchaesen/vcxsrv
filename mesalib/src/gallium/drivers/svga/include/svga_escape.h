/*
 * Copyright (c) 2007-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0 OR MIT
 */

/*
 * svga_escape.h --
 *
 *    Definitions for our own (vendor-specific) SVGA Escape commands.
 */

#ifndef _SVGA_ESCAPE_H_
#define _SVGA_ESCAPE_H_


/*
 * Namespace IDs for the escape command
 */

#define SVGA_ESCAPE_NSID_VMWARE 0x00000000
#define SVGA_ESCAPE_NSID_DEVEL  0xFFFFFFFF


/*
 * Within SVGA_ESCAPE_NSID_VMWARE, we multiplex commands according to
 * the first DWORD of escape data (after the nsID and size). As a
 * guideline we're using the high word and low word as a major and
 * minor command number, respectively.
 *
 * Major command number allocation:
 *
 *   0000: Reserved
 *   0001: SVGA_ESCAPE_VMWARE_LOG (svga_binary_logger.h)
 *   0002: SVGA_ESCAPE_VMWARE_VIDEO (svga_overlay.h)
 *   0003: SVGA_ESCAPE_VMWARE_HINT (svga_escape.h)
 */

#define SVGA_ESCAPE_VMWARE_MAJOR_MASK  0xFFFF0000


/*
 * SVGA Hint commands.
 *
 * These escapes let the SVGA driver provide optional information to
 * he host about the state of the guest or guest applications. The
 * host can use these hints to make user interface or performance
 * decisions.
 *
 * Notes:
 *
 *   - SVGA_ESCAPE_VMWARE_HINT_FULLSCREEN is deprecated for guests
 *     that use the SVGA Screen Object extension. Instead of sending
 *     this escape, use the SVGA_SCREEN_FULLSCREEN_HINT flag on your
 *     Screen Object.
 */

#define SVGA_ESCAPE_VMWARE_HINT               0x00030000
#define SVGA_ESCAPE_VMWARE_HINT_FULLSCREEN    0x00030001  /* Deprecated */

typedef
struct {
   uint32 command;
   uint32 fullscreen;
   struct {
      int32 x, y;
   } monitorPosition;
} SVGAEscapeHintFullscreen;

#endif /* _SVGA_ESCAPE_H_ */
