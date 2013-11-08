/*
 * xwin-config.h.in
 *
 * This file has all defines used in the xwin ddx
 *
 */
#include <dix-config.h>

/* Winsock networking */
#define HAS_WINSOCK

/* Cygwin has /dev/windows for signaling new win32 messages */
/* #undef HAS_DEVWINDOWS */

/* Switch on debug messages */ 
/* #undef CYGDEBUG */
/* #undef CYGWINDOWING_DEBUG */
/* #undef CYGMULTIWINDOW_DEBUG */

/* Define to 1 if unsigned long is 64 bits. */
/* #undef _XSERVER64 */

/* Short vendor name */
#define XVENDORNAMESHORT "VcXsrv"

/* Vendor web address for support */
#define __VENDORDWEBSUPPORT__ "http://www.hc-consult.be/"

/* Location of system.XWinrc */
#define SYSCONFDIR "."

/* Default log location */
#define DEFAULT_LOGDIR "."

/* Whether we should re-locate the root to where the executable lives */
/* #undef RELOCATE_PROJECTROOT */
