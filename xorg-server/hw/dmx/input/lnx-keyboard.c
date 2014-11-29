/* Portions of this file were derived from the following files:
 *
 **********************************************************************
 *
 * xfree86/common/{xf86Io.c,xf86Kbd.c,xf86Events.c}
 *
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Thomas Roell not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Thomas Roell makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THOMAS ROELL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THOMAS ROELL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 **********************************************************************
 *
 * xfree86/common/xf86KbdLnx.c
 *
 * Linux version of keymapping setup. The kernel (since 0.99.14) has support
 * for fully remapping the keyboard, but there are some differences between
 * the Linux map and the SVR4 map (esp. in the extended keycodes). We also
 * remove the restriction on what keycodes can be remapped.
 * Orest Zborowski.
 *
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Thomas Roell not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Thomas Roell makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THOMAS ROELL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THOMAS ROELL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 **********************************************************************
 *
 * xfree86/os-support/linux/lnx_io.c
 *
 * Copyright 1992 by Orest Zborowski <obz@Kodak.com>
 * Copyright 1993 by David Dawes <dawes@xfree86.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Orest Zborowski and David Dawes
 * not be used in advertising or publicity pertaining to distribution of
 * the software without specific, written prior permission.  Orest Zborowski
 * and David Dawes make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * OREST ZBOROWSKI AND DAVID DAWES DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL OREST ZBOROWSKI OR DAVID DAWES BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * Copyright 2001-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 *
 * This code implements a low-level device driver for the Linux
 * keyboard.  The code is derived from code by Thomas Roell, Orest
 * Zborowski, and David Dawes (see the source code for complete
 * references). */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

/*****************************************************************************/
/* Define some macros to make it easier to move this file to another
 * part of the Xserver tree.  All calls to the dmx* layer are #defined
 * here for the .c file.  The .h file will also have to be edited. */
#include "dmxinputinit.h"
#include "lnx-keyboard.h"

#define GETPRIV       myPrivate *priv                            \
                      = ((DMXLocalInputInfoPtr)(pDev->devicePrivate))->private

#define LOG0(f)       dmxLog(dmxDebug,f)
#define LOG1(f,a)     dmxLog(dmxDebug,f,a)
#define LOG2(f,a,b)   dmxLog(dmxDebug,f,a,b)
#define LOG3(f,a,b,c) dmxLog(dmxDebug,f,a,b,c)
#define FATAL0(f)     dmxLog(dmxFatal,f)
#define FATAL1(f,a)   dmxLog(dmxFatal,f,a)
#define FATAL2(f,a,b) dmxLog(dmxFatal,f,a,b)
#define MOTIONPROC    dmxMotionProcPtr
#define ENQUEUEPROC   dmxEnqueueProcPtr
#define CHECKPROC     dmxCheckSpecialProcPtr
#define SWITCHRETPROC dmxVTSwitchReturnProcPtr
#define BLOCK         DMXBlockType
#define MESSAGE       "\033c\n\n\nDMX taking input from this console..."
#define FINALMESSAGE  "\033cDMX terminated."

/* End of interface definitions. */
/*****************************************************************************/

#include "inputstr.h"
#include <X11/Xos.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/vt.h>
#include <sys/kd.h>
#include <termios.h>
#include "atKeynames.h"
#if 00
#include "xf86Keymap.h"
#endif
#include <linux/keyboard.h>
#include <xkbsrv.h>

#define NUM_AT2LNX (sizeof(at2lnx) / sizeof(at2lnx[0]))
#define NUM_STATE_ENTRIES (256/32)

/* Private area for Linux-style keyboards. */
typedef struct _myPrivate {
    int fd;
    int vtno;
    int vtcurrent;
    int kbdtrans;
    struct termios kbdtty;
    int kbdType;
    CARD32 kbdState[NUM_STATE_ENTRIES];
    DeviceIntPtr pKeyboard;
    unsigned char prefix;

    int switched;
    SWITCHRETPROC switch_return;
    void *switch_return_data;

    /* For bell */
    int pitch;
    unsigned long duration;
} myPrivate;

static myPrivate *PRIV = NULL;

#undef SYSCALL
#define SYSCALL(call) while(((call) == -1) && (errno == EINTR))

static int
kbdLinuxKeyDown(myPrivate * priv, int keyCode)
{
    CARD8 byte = keyCode >> 5;
    CARD32 bit = 1 << (keyCode & 0x1f);

    if (byte > NUM_STATE_ENTRIES)
        return 0;
    return priv->kbdState[byte] & bit;
}

static void
kbdLinuxKeyState(myPrivate * priv, int type, int keyCode)
{
    CARD8 byte = keyCode >> 5;
    CARD32 bit = 1 << (keyCode & 0x1f);

    if (byte > NUM_STATE_ENTRIES)
        return;
    if (type == KeyPress)
        priv->kbdState[byte] |= bit;
    else
        priv->kbdState[byte] &= ~bit;
}

static KeySym linux_to_x[256] = {
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    XK_BackSpace, XK_Tab, XK_Linefeed, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, XK_Escape,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    XK_space, XK_exclam, XK_quotedbl, XK_numbersign,
    XK_dollar, XK_percent, XK_ampersand, XK_apostrophe,
    XK_parenleft, XK_parenright, XK_asterisk, XK_plus,
    XK_comma, XK_minus, XK_period, XK_slash,
    XK_0, XK_1, XK_2, XK_3,
    XK_4, XK_5, XK_6, XK_7,
    XK_8, XK_9, XK_colon, XK_semicolon,
    XK_less, XK_equal, XK_greater, XK_question,
    XK_at, XK_A, XK_B, XK_C,
    XK_D, XK_E, XK_F, XK_G,
    XK_H, XK_I, XK_J, XK_K,
    XK_L, XK_M, XK_N, XK_O,
    XK_P, XK_Q, XK_R, XK_S,
    XK_T, XK_U, XK_V, XK_W,
    XK_X, XK_Y, XK_Z, XK_bracketleft,
    XK_backslash, XK_bracketright, XK_asciicircum, XK_underscore,
    XK_grave, XK_a, XK_b, XK_c,
    XK_d, XK_e, XK_f, XK_g,
    XK_h, XK_i, XK_j, XK_k,
    XK_l, XK_m, XK_n, XK_o,
    XK_p, XK_q, XK_r, XK_s,
    XK_t, XK_u, XK_v, XK_w,
    XK_x, XK_y, XK_z, XK_braceleft,
    XK_bar, XK_braceright, XK_asciitilde, XK_BackSpace,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    NoSymbol, NoSymbol, NoSymbol, NoSymbol,
    XK_nobreakspace, XK_exclamdown, XK_cent, XK_sterling,
    XK_currency, XK_yen, XK_brokenbar, XK_section,
    XK_diaeresis, XK_copyright, XK_ordfeminine, XK_guillemotleft,
    XK_notsign, XK_hyphen, XK_registered, XK_macron,
    XK_degree, XK_plusminus, XK_twosuperior, XK_threesuperior,
    XK_acute, XK_mu, XK_paragraph, XK_periodcentered,
    XK_cedilla, XK_onesuperior, XK_masculine, XK_guillemotright,
    XK_onequarter, XK_onehalf, XK_threequarters, XK_questiondown,
    XK_Agrave, XK_Aacute, XK_Acircumflex, XK_Atilde,
    XK_Adiaeresis, XK_Aring, XK_AE, XK_Ccedilla,
    XK_Egrave, XK_Eacute, XK_Ecircumflex, XK_Ediaeresis,
    XK_Igrave, XK_Iacute, XK_Icircumflex, XK_Idiaeresis,
    XK_ETH, XK_Ntilde, XK_Ograve, XK_Oacute,
    XK_Ocircumflex, XK_Otilde, XK_Odiaeresis, XK_multiply,
    XK_Ooblique, XK_Ugrave, XK_Uacute, XK_Ucircumflex,
    XK_Udiaeresis, XK_Yacute, XK_THORN, XK_ssharp,
    XK_agrave, XK_aacute, XK_acircumflex, XK_atilde,
    XK_adiaeresis, XK_aring, XK_ae, XK_ccedilla,
    XK_egrave, XK_eacute, XK_ecircumflex, XK_ediaeresis,
    XK_igrave, XK_iacute, XK_icircumflex, XK_idiaeresis,
    XK_eth, XK_ntilde, XK_ograve, XK_oacute,
    XK_ocircumflex, XK_otilde, XK_odiaeresis, XK_division,
    XK_oslash, XK_ugrave, XK_uacute, XK_ucircumflex,
    XK_udiaeresis, XK_yacute, XK_thorn, XK_ydiaeresis
};

/*
 * Maps the AT keycodes to Linux keycodes
 */
static unsigned char at2lnx[NUM_KEYCODES] = {
    0x01, /* KEY_Escape */ 0x02,        /* KEY_1 */
    0x03, /* KEY_2 */ 0x04,     /* KEY_3 */
    0x05, /* KEY_4 */ 0x06,     /* KEY_5 */
    0x07, /* KEY_6 */ 0x08,     /* KEY_7 */
    0x09, /* KEY_8 */ 0x0a,     /* KEY_9 */
    0x0b, /* KEY_0 */ 0x0c,     /* KEY_Minus */
    0x0d, /* KEY_Equal */ 0x0e, /* KEY_BackSpace */
    0x0f, /* KEY_Tab */ 0x10,   /* KEY_Q */
    0x11, /* KEY_W */ 0x12,     /* KEY_E */
    0x13, /* KEY_R */ 0x14,     /* KEY_T */
    0x15, /* KEY_Y */ 0x16,     /* KEY_U */
    0x17, /* KEY_I */ 0x18,     /* KEY_O */
    0x19, /* KEY_P */ 0x1a,     /* KEY_LBrace */
    0x1b, /* KEY_RBrace */ 0x1c,        /* KEY_Enter */
    0x1d, /* KEY_LCtrl */ 0x1e, /* KEY_A */
    0x1f, /* KEY_S */ 0x20,     /* KEY_D */
    0x21, /* KEY_F */ 0x22,     /* KEY_G */
    0x23, /* KEY_H */ 0x24,     /* KEY_J */
    0x25, /* KEY_K */ 0x26,     /* KEY_L */
    0x27, /* KEY_SemiColon */ 0x28,     /* KEY_Quote */
    0x29, /* KEY_Tilde */ 0x2a, /* KEY_ShiftL */
    0x2b, /* KEY_BSlash */ 0x2c,        /* KEY_Z */
    0x2d, /* KEY_X */ 0x2e,     /* KEY_C */
    0x2f, /* KEY_V */ 0x30,     /* KEY_B */
    0x31, /* KEY_N */ 0x32,     /* KEY_M */
    0x33, /* KEY_Comma */ 0x34, /* KEY_Period */
    0x35, /* KEY_Slash */ 0x36, /* KEY_ShiftR */
    0x37, /* KEY_KP_Multiply */ 0x38,   /* KEY_Alt */
    0x39, /* KEY_Space */ 0x3a, /* KEY_CapsLock */
    0x3b, /* KEY_F1 */ 0x3c,    /* KEY_F2 */
    0x3d, /* KEY_F3 */ 0x3e,    /* KEY_F4 */
    0x3f, /* KEY_F5 */ 0x40,    /* KEY_F6 */
    0x41, /* KEY_F7 */ 0x42,    /* KEY_F8 */
    0x43, /* KEY_F9 */ 0x44,    /* KEY_F10 */
    0x45, /* KEY_NumLock */ 0x46,       /* KEY_ScrollLock */
    0x47, /* KEY_KP_7 */ 0x48,  /* KEY_KP_8 */
    0x49, /* KEY_KP_9 */ 0x4a,  /* KEY_KP_Minus */
    0x4b, /* KEY_KP_4 */ 0x4c,  /* KEY_KP_5 */
    0x4d, /* KEY_KP_6 */ 0x4e,  /* KEY_KP_Plus */
    0x4f, /* KEY_KP_1 */ 0x50,  /* KEY_KP_2 */
    0x51, /* KEY_KP_3 */ 0x52,  /* KEY_KP_0 */
    0x53, /* KEY_KP_Decimal */ 0x54,    /* KEY_SysReqest */
    0x00, /* 0x55 */ 0x56,      /* KEY_Less */
    0x57, /* KEY_F11 */ 0x58,   /* KEY_F12 */
    0x66, /* KEY_Home */ 0x67,  /* KEY_Up */
    0x68, /* KEY_PgUp */ 0x69,  /* KEY_Left */
    0x5d, /* KEY_Begin */ 0x6a, /* KEY_Right */
    0x6b, /* KEY_End */ 0x6c,   /* KEY_Down */
    0x6d, /* KEY_PgDown */ 0x6e,        /* KEY_Insert */
    0x6f, /* KEY_Delete */ 0x60,        /* KEY_KP_Enter */
    0x61, /* KEY_RCtrl */ 0x77, /* KEY_Pause */
    0x63, /* KEY_Print */ 0x62, /* KEY_KP_Divide */
    0x64, /* KEY_AltLang */ 0x65,       /* KEY_Break */
    0x00, /* KEY_LMeta */ 0x00, /* KEY_RMeta */
    0x7A, /* KEY_Menu/FOCUS_PF11 */ 0x00,       /* 0x6e */
    0x7B, /* FOCUS_PF12 */ 0x00,        /* 0x70 */
    0x00, /* 0x71 */ 0x00,      /* 0x72 */
    0x59, /* FOCUS_PF2 */ 0x78, /* FOCUS_PF9 */
    0x00, /* 0x75 */ 0x00,      /* 0x76 */
    0x5A, /* FOCUS_PF3 */ 0x5B, /* FOCUS_PF4 */
    0x5C, /* FOCUS_PF5 */ 0x5D, /* FOCUS_PF6 */
    0x5E, /* FOCUS_PF7 */ 0x5F, /* FOCUS_PF8 */
    0x7C, /* JAP_86 */ 0x79,    /* FOCUS_PF10 */
    0x00,                       /* 0x7f */
};

/** Create a private structure for use within this file. */
void *
kbdLinuxCreatePrivate(DeviceIntPtr pKeyboard)
{
    myPrivate *priv = calloc(1, sizeof(*priv));

    priv->fd = -1;
    priv->pKeyboard = pKeyboard;
    return priv;
}

/** Destroy a private structure. */
void
kbdLinuxDestroyPrivate(void *priv)
{
    free(priv);
}

/** Ring the bell.
 *
 * Note: we completely ignore the \a volume, since Linux's ioctl()
 * interface does not provide a way to control it.  If it did, the XBell
 * manpage tells how the actual volume is a function of the percent and
 * the (base) volume.
 *
 * Note that most of the other PC-based bell drivers compute the
 * duration for KDMKTONE as a function of the volume and the duration.
 * For some drivers, the duration is only measured in mS if the volume
 * is 50, and is scaled by the volume for other values.  This seems
 * confusing and possibly incorrect (the xset man page says that the
 * bell will be "as closely as it can to the user's specifications" --
 * if we ignore the volume and set the duration correctly, then we'll
 * get one parameter "wrong" -- but if we use the volume to scale the
 * duration, then we'll get both parameters "wrong"). */
void
kbdLinuxBell(DevicePtr pDev, int percent, int volume, int pitch, int duration)
{
    GETPRIV;

    if (duration && pitch) {
        ioctl(priv->fd, KDMKTONE, ((1193190 / pitch) & 0xffff)  /* Low bits specify cycle time */
              |(duration << 16));       /* High bits are duration in msec */
    }
}

/** Set the LEDs. */
void
kbdLinuxCtrl(DevicePtr pDev, KeybdCtrl * ctrl)
{
    GETPRIV;

    ioctl(priv->fd, KDSETLED, ctrl->leds & 0x07);
}

static int
kbdLinuxGetFreeVTNumber(void)
{
    int fd = -1;
    int vtno;
    int i;
    const char *tty0[] = { "/dev/tty0", "/dev/vc/0", NULL };

    for (i = 0; tty0[i]; i++)
        if ((fd = open(tty0[i], O_WRONLY, 0)) >= 0)
            break;
    if (fd < 0)
        FATAL1("kbdLinuxGetFreeVTNumber: Cannot open tty0 (%s)\n",
               strerror(errno));
    if (ioctl(fd, VT_OPENQRY, &vtno) < 0 || vtno < 0)
        FATAL0("kbdLinuxGetFreeVTNumber: Cannot find a free VT\n");
    return vtno;
}

static int
kbdLinuxOpenVT(int vtno)
{
    int fd = -1;
    int i;
    const char *vcs[] = { "/dev/vc/", "/dev/tty", NULL };
    char name[64];              /* RATS: Only used in snprintf */

    for (i = 0; vcs[i]; i++) {
        snprintf(name, sizeof(name), "%s%d", vcs[i], vtno);
        if ((fd = open(name, O_RDWR | O_NONBLOCK, 0)) >= 0)
            break;
    }
    if (fd < 0)
        FATAL2("kbdLinuxOpenVT: Cannot open VT %d (%s)\n",
               vtno, strerror(errno));
    return fd;
}

static int
kbdLinuxGetCurrentVTNumber(int fd)
{
    struct vt_stat vts;

    if (!ioctl(fd, VT_GETSTATE, &vts))
        return vts.v_active;
    return -1;
}

static int kbdLinuxActivate(int fd, int vtno, int setSig);

/** Currently unused hook called prior to an VT switch. */
void
kbdLinuxVTPreSwitch(void *p)
{
}

/** Currently unused hook called after returning from a VT switch. */
void
kbdLinuxVTPostSwitch(void *p)
{
}

/** Tell the operating system to switch to \a vt.  The \a switch_return
 * function is called with the \a switch_return_data when the VT is
 * switched back to the pre-switch VT (i.e., the user returns to the DMX
 * session). */
int
kbdLinuxVTSwitch(void *p, int vt,
                 void (*switch_return) (void *), void *switch_return_data)
{
    myPrivate *priv = p;

    if (priv->switched)
        FATAL0("kbdLinuxVTSwitch: already switched...\n");
    if (priv->vtno == vt)
        return 0;

    PRIV = priv;
    priv->switched = 0;         /* Will switch to 1 in handler */
    priv->switch_return = switch_return;
    priv->switch_return_data = switch_return_data;
    kbdLinuxActivate(priv->fd, vt, 0);
    return 1;
}

/* RATS: This function is only ever used to handle SIGUSR1. */
static void
kbdLinuxVTSignalHandler(int sig)
{
    myPrivate *priv = PRIV;

    signal(sig, kbdLinuxVTSignalHandler);
    if (priv) {
        ioctl(priv->fd, VT_RELDISP, VT_ACKACQ);
        priv->switched = !priv->switched;
        LOG2("kbdLinuxVTSignalHandler: got signal %d, switched = %d\n",
             sig, priv->switched);
        if (!priv->switched && priv->switch_return)
            priv->switch_return(priv->switch_return_data);
    }
}

static int
kbdLinuxActivate(int fd, int vtno, int setSig)
{
    int result;
    struct vt_mode VT;

    SYSCALL(result = ioctl(fd, VT_ACTIVATE, vtno));
    if (result)
        FATAL0("kbdLinuxActivate: VT_ACTIVATE failed\n");
    SYSCALL(result = ioctl(fd, VT_WAITACTIVE, vtno));
    if (result)
        FATAL0("kbdLinuxActivate: VT_WAITACTIVE failed\n");
    if (setSig) {
        SYSCALL(result = ioctl(fd, VT_GETMODE, &VT));
        if (result < 0)
            FATAL0("kbdLinuxActivate: VT_GETMODE failed\n");
        VT.mode = VT_PROCESS;
        VT.relsig = SIGUSR1;
        VT.acqsig = SIGUSR1;
        if (ioctl(fd, VT_SETMODE, &VT))
            FATAL0("kbdLinuxActivate: VT_SETMODE VT_PROCESS failed\n");
        signal(SIGUSR1, kbdLinuxVTSignalHandler);
    }
    return Success;
}

static void
kbdLinuxOpenConsole(DevicePtr pDev)
{
    GETPRIV;
    const char *msg = MESSAGE;

    if (priv->fd >= 0)
        return;
    priv->vtno = kbdLinuxGetFreeVTNumber();
    priv->fd = kbdLinuxOpenVT(priv->vtno);
    priv->vtcurrent = kbdLinuxGetCurrentVTNumber(priv->fd);
    LOG2("kbdLinuxOpenConsole: current VT %d; using free VT %d\n",
         priv->vtcurrent, priv->vtno);
    kbdLinuxActivate(priv->fd, priv->vtno, 1);
    ioctl(priv->fd, KDSETMODE, KD_GRAPHICS);    /* To turn off gpm */
    if (msg)
        write(priv->fd, msg, strlen(msg));
}

static void
kbdLinuxCloseConsole(DevicePtr pDev)
{
    GETPRIV;
    struct vt_mode VT;
    const char *msg = FINALMESSAGE;

    if (priv->fd < 0)
        return;

    ioctl(priv->fd, KDSETMODE, KD_TEXT);
    if (msg)
        write(priv->fd, msg, strlen(msg));
    if (ioctl(priv->fd, VT_GETMODE, &VT) != -1) {
        VT.mode = VT_AUTO;
        ioctl(priv->fd, VT_SETMODE, &VT);
    }

    LOG1("kbdLinuxCloseConsole: switching to VT %d\n", priv->vtcurrent);
    if (priv->vtcurrent >= 0)
        kbdLinuxActivate(priv->fd, priv->vtcurrent, 0);

    close(priv->fd);
    priv->fd = -1;
}

/** Initialize the \a pDev as a Linux keyboard. */
void
kbdLinuxInit(DevicePtr pDev)
{
    GETPRIV;

    if (priv->fd <= 0)
        kbdLinuxOpenConsole(pDev);

    ioctl(priv->fd, KDGKBMODE, &priv->kbdtrans);
    if (tcgetattr(priv->fd, &priv->kbdtty) < 0)
        FATAL1("kbdLinuxInit: tcgetattr failed (%s)\n", strerror(errno));
}

static int
kbdLinuxPrefix0Mapping(unsigned char *scanCode)
{
    /* Table from xfree86/common/xf86Events.c */
    switch (*scanCode) {
    case KEY_KP_7:
        *scanCode = KEY_Home;
        break;                  /* curs home */
    case KEY_KP_8:
        *scanCode = KEY_Up;
        break;                  /* curs up */
    case KEY_KP_9:
        *scanCode = KEY_PgUp;
        break;                  /* curs pgup */
    case KEY_KP_4:
        *scanCode = KEY_Left;
        break;                  /* curs left */
    case KEY_KP_5:
        *scanCode = KEY_Begin;
        break;                  /* curs begin */
    case KEY_KP_6:
        *scanCode = KEY_Right;
        break;                  /* curs right */
    case KEY_KP_1:
        *scanCode = KEY_End;
        break;                  /* curs end */
    case KEY_KP_2:
        *scanCode = KEY_Down;
        break;                  /* curs down */
    case KEY_KP_3:
        *scanCode = KEY_PgDown;
        break;                  /* curs pgdown */
    case KEY_KP_0:
        *scanCode = KEY_Insert;
        break;                  /* curs insert */
    case KEY_KP_Decimal:
        *scanCode = KEY_Delete;
        break;                  /* curs delete */
    case KEY_Enter:
        *scanCode = KEY_KP_Enter;
        break;                  /* keypad enter */
    case KEY_LCtrl:
        *scanCode = KEY_RCtrl;
        break;                  /* right ctrl */
    case KEY_KP_Multiply:
        *scanCode = KEY_Print;
        break;                  /* print */
    case KEY_Slash:
        *scanCode = KEY_KP_Divide;
        break;                  /* keyp divide */
    case KEY_Alt:
        *scanCode = KEY_AltLang;
        break;                  /* right alt */
    case KEY_ScrollLock:
        *scanCode = KEY_Break;
        break;                  /* curs break */
    case 0x5b:
        *scanCode = KEY_LMeta;
        break;
    case 0x5c:
        *scanCode = KEY_RMeta;
        break;
    case 0x5d:
        *scanCode = KEY_Menu;
        break;
    case KEY_F3:
        *scanCode = KEY_F13;
        break;
    case KEY_F4:
        *scanCode = KEY_F14;
        break;
    case KEY_F5:
        *scanCode = KEY_F15;
        break;
    case KEY_F6:
        *scanCode = KEY_F16;
        break;
    case KEY_F7:
        *scanCode = KEY_F17;
        break;
    case KEY_KP_Plus:
        *scanCode = KEY_KP_DEC;
        break;
        /*
         * Ignore virtual shifts (E0 2A, E0 AA, E0 36, E0 B6)
         */
    case 0x2A:
    case 0x36:
        return 1;
    default:
        /*
         * "Internet" keyboards are generating lots of new codes.
         * Let them pass.  There is little consistency between them,
         * so don't bother with symbolic names at this level.
         */
        scanCode += 0x78;
    }
    return 0;
}

static int
kbdLinuxPrefixMapping(myPrivate * priv, unsigned char *scanCode)
{
    int pressed = *scanCode & 0x80;
    unsigned char code = *scanCode & 0x7f;

    /* If we don't have a prefix, check for one */
    if (!priv->prefix) {
        switch (code) {
        case KEY_Prefix0:
        case KEY_Prefix1:
            priv->prefix = code;
            return 1;
        }
        return 0;               /* No change */
    }

    /* We have a prefix from the last scanCode */
    switch (priv->prefix) {
    case KEY_Prefix0:
        priv->prefix = 0;
        if (kbdLinuxPrefix0Mapping(&code))
            return 1;           /* Skip sequence */
        break;
    case KEY_Prefix1:
        priv->prefix = (code = KEY_LCtrl) ? KEY_LCtrl : 0;
        return 1;               /* Use new prefix */
    case KEY_LCtrl:
        priv->prefix = 0;
        if (code != KEY_NumLock)
            return 1;           /* Skip sequence */
        code = KEY_Pause;
        break;
    }

    *scanCode = code | (pressed ? 0x80 : 0x00);
    return 0;                   /* Use old scanCode */
}

static void
kbdLinuxConvert(DevicePtr pDev,
                unsigned char scanCode,
                ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    GETPRIV;
    XkbSrvInfoPtr xkbi = priv->pKeyboard->key->xkbInfo;
    int type;
    KeySym keySym = NoSymbol;
    int keyCode;
    int switching;

    /* Do special PC/AT prefix mapping -- may change scanCode! */
    if (kbdLinuxPrefixMapping(priv, &scanCode))
        return;

    type = (scanCode & 0x80) ? KeyRelease : KeyPress;
    keyCode = (scanCode & 0x7f) + MIN_KEYCODE;

    /* Handle repeats */

    if (keyCode >= xkbi->desc->min_key_code &&
        keyCode <= xkbi->desc->max_key_code) {

        int effectiveGroup = XkbGetEffectiveGroup(xkbi,
                                                  &xkbi->state,
                                                  scanCode);

        keySym = XkbKeySym(xkbi->desc, scanCode, effectiveGroup);
#if 0
        switch (keySym) {
        case XK_Num_Lock:
        case XK_Scroll_Lock:
        case XK_Shift_Lock:
        case XK_Caps_Lock:
            /* Ignore releases and all but first press */
            if (kbdLinuxModIgnore(priv, &xE, keySym))
                return;
            if (kbdLinuxKeyDown(priv, &xE))
                xE.u.u.type = KeyRelease;
            else
                xE.u.u.type = KeyPress;
            break;
        }
#endif

        /* If key is already down, ignore or autorepeat */
        if (type == KeyPress && kbdLinuxKeyDown(priv, keyCode)) {
            KbdFeedbackClassRec *feed = priv->pKeyboard->kbdfeed;

            /* No auto-repeat? */
            if ((feed && !feed->ctrl.autoRepeat)
                || priv->pKeyboard->key->xkbInfo->desc->map->modmap[keyCode]
                || (feed && !(feed->ctrl.autoRepeats[keyCode >> 3]
                              & (1 << (keyCode & 7)))))
                return;         /* Ignore */

            /* Do auto-repeat */
            enqueue(pDev, KeyRelease, keyCode, keySym, NULL, block);
            type = KeyPress;
        }

        /* If key is already up, ignore */
        if (type == KeyRelease && !kbdLinuxKeyDown(priv, keyCode))
            return;
    }

    switching = 0;
    if (checkspecial && type == KeyPress)
        switching = checkspecial(pDev, keySym);
    if (!switching) {
        if (enqueue)
            enqueue(pDev, type, keyCode, keySym, NULL, block);
        kbdLinuxKeyState(priv, type, keyCode);  /* Update our state bitmap */
    }
}

/** Read an event from the \a pDev device.  If the event is a motion
 * event, enqueue it with the \a motion function.  Otherwise, check for
 * special keys with the \a checkspecial function and enqueue the event
 * with the \a enqueue function.  The \a block type is passed to the
 * functions so that they may block SIGIO handling as appropriate to the
 * caller of this function. */
void
kbdLinuxRead(DevicePtr pDev,
             MOTIONPROC motion,
             ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    GETPRIV;
    unsigned char buf[256];     /* RATS: Only used in length-limited call */
    unsigned char *pt;
    int n;

    while ((n = read(priv->fd, buf, sizeof(buf))) > 0)
        for (pt = buf; n; --n, ++pt)
            kbdLinuxConvert(pDev, *pt, enqueue, checkspecial, block);
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
kbdLinuxOn(DevicePtr pDev)
{
    GETPRIV;
    struct termios nTty;

    ioctl(priv->fd, KDSKBMODE, K_RAW);

    nTty = priv->kbdtty;
    nTty.c_iflag = (IGNPAR | IGNBRK) & (~PARMRK) & (~ISTRIP);
    nTty.c_oflag = 0;
    nTty.c_cflag = CREAD | CS8;
    nTty.c_lflag = 0;
    nTty.c_cc[VTIME] = 0;
    nTty.c_cc[VMIN] = 1;
    cfsetispeed(&nTty, B9600);
    cfsetospeed(&nTty, B9600);
    if (tcsetattr(priv->fd, TCSANOW, &nTty) < 0)
        FATAL1("kbdLinuxOn: tcsetattr failed (%s)\n", strerror(errno));
    return priv->fd;
}

/** Turn \a pDev off (i.e., stop taking input from \a pDev). */
void
kbdLinuxOff(DevicePtr pDev)
{
    GETPRIV;

    ioctl(priv->fd, KDSKBMODE, priv->kbdtrans);
    tcsetattr(priv->fd, TCSANOW, &priv->kbdtty);
    kbdLinuxCloseConsole(pDev);
}

static void
kbdLinuxReadKernelMapping(int fd, KeySymsPtr pKeySyms)
{
    KeySym *k;
    int i;
    int maxkey;

    static unsigned char tbl[GLYPHS_PER_KEY] = {        /* RATS: Use ok */
        0,                      /* unshifted */
        1,                      /* shifted */
        0,                      /* modeswitch unshifted */
        0                       /* modeswitch shifted */
    };

    /*
     * Read the mapping from the kernel.
     * Since we're still using the XFree86 scancode->AT keycode mapping
     * routines, we need to convert the AT keycodes to Linux keycodes,
     * then translate the Linux keysyms into X keysyms.
     *
     * First, figure out which tables to use for the modeswitch columns
     * above, from the XF86Config fields.
     */
    tbl[2] = 8;                 /* alt */
    tbl[3] = tbl[2] | 1;

#if 00 /*BP*/
        k = map + GLYPHS_PER_KEY;
#else
    ErrorF("kbdLinuxReadKernelMapping() is broken/no-op'd\n");
    return;
#endif
    maxkey = NUM_AT2LNX;

    for (i = 0; i < maxkey; ++i) {
        struct kbentry kbe;
        int j;

        kbe.kb_index = at2lnx[i];

        for (j = 0; j < GLYPHS_PER_KEY; ++j, ++k) {
            unsigned short kval;

            *k = NoSymbol;

            kbe.kb_table = tbl[j];
            if (kbe.kb_index == 0 || ioctl(fd, KDGKBENT, &kbe))
                continue;

            kval = KVAL(kbe.kb_value);
            switch (KTYP(kbe.kb_value)) {
            case KT_LATIN:
            case KT_LETTER:
                *k = linux_to_x[kval];
                break;
            case KT_FN:
                if (kval <= 19)
                    *k = XK_F1 + kval;
                else
                    switch (kbe.kb_value) {
                    case K_FIND:
                        *k = XK_Home;   /* or XK_Find */
                        break;
                    case K_INSERT:
                        *k = XK_Insert;
                        break;
                    case K_REMOVE:
                        *k = XK_Delete;
                        break;
                    case K_SELECT:
                        *k = XK_End;    /* or XK_Select */
                        break;
                    case K_PGUP:
                        *k = XK_Prior;
                        break;
                    case K_PGDN:
                        *k = XK_Next;
                        break;
                    case K_HELP:
                        *k = XK_Help;
                        break;
                    case K_DO:
                        *k = XK_Execute;
                        break;
                    case K_PAUSE:
                        *k = XK_Pause;
                        break;
                    case K_MACRO:
                        *k = XK_Menu;
                        break;
                    default:
                        break;
                    }
                break;
            case KT_SPEC:
                switch (kbe.kb_value) {
                case K_ENTER:
                    *k = XK_Return;
                    break;
                case K_BREAK:
                    *k = XK_Break;
                    break;
                case K_CAPS:
                    *k = XK_Caps_Lock;
                    break;
                case K_NUM:
                    *k = XK_Num_Lock;
                    break;
                case K_HOLD:
                    *k = XK_Scroll_Lock;
                    break;
                case K_COMPOSE:
                    *k = XK_Multi_key;
                    break;
                default:
                    break;
                }
                break;
            case KT_PAD:
                switch (kbe.kb_value) {
                case K_PPLUS:
                    *k = XK_KP_Add;
                    break;
                case K_PMINUS:
                    *k = XK_KP_Subtract;
                    break;
                case K_PSTAR:
                    *k = XK_KP_Multiply;
                    break;
                case K_PSLASH:
                    *k = XK_KP_Divide;
                    break;
                case K_PENTER:
                    *k = XK_KP_Enter;
                    break;
                case K_PCOMMA:
                    *k = XK_KP_Separator;
                    break;
                case K_PDOT:
                    *k = XK_KP_Decimal;
                    break;
                case K_PPLUSMINUS:
                    *k = XK_KP_Subtract;
                    break;
                default:
                    if (kval <= 9)
                        *k = XK_KP_0 + kval;
                    break;
                }
                break;
            case KT_DEAD:
                /* KT_DEAD keys are for accelerated diacritical creation. */
                switch (kbe.kb_value) {
                case K_DGRAVE:
                    *k = XK_dead_grave;
                    break;
                case K_DACUTE:
                    *k = XK_dead_acute;
                    break;
                case K_DCIRCM:
                    *k = XK_dead_circumflex;
                    break;
                case K_DTILDE:
                    *k = XK_dead_tilde;
                    break;
                case K_DDIERE:
                    *k = XK_dead_diaeresis;
                    break;
                }
                break;
            case KT_CUR:
                switch (kbe.kb_value) {
                case K_DOWN:
                    *k = XK_Down;
                    break;
                case K_LEFT:
                    *k = XK_Left;
                    break;
                case K_RIGHT:
                    *k = XK_Right;
                    break;
                case K_UP:
                    *k = XK_Up;
                    break;
                }
                break;
            case KT_SHIFT:
                switch (kbe.kb_value) {
                case K_ALTGR:
                    *k = XK_Alt_R;
                    break;
                case K_ALT:
                    *k = (kbe.kb_index == 0x64 ? XK_Alt_R : XK_Alt_L);
                    break;
                case K_CTRL:
                    *k = (kbe.kb_index == 0x61 ? XK_Control_R : XK_Control_L);
                    break;
                case K_CTRLL:
                    *k = XK_Control_L;
                    break;
                case K_CTRLR:
                    *k = XK_Control_R;
                    break;
                case K_SHIFT:
                    *k = (kbe.kb_index == 0x36 ? XK_Shift_R : XK_Shift_L);
                    break;
                case K_SHIFTL:
                    *k = XK_Shift_L;
                    break;
                case K_SHIFTR:
                    *k = XK_Shift_R;
                    break;
                default:
                    break;
                }
                break;
            case KT_ASCII:
                /* KT_ASCII keys accumulate a 3 digit decimal number that
                 * gets emitted when the shift state changes. We can't
                 * emulate that.
                 */
                break;
            case KT_LOCK:
                if (kbe.kb_value == K_SHIFTLOCK)
                    *k = XK_Shift_Lock;
                break;
            default:
                break;
            }
        }

        if (k[-1] == k[-2])
            k[-1] = NoSymbol;
        if (k[-2] == k[-3])
            k[-2] = NoSymbol;
        if (k[-3] == k[-4])
            k[-3] = NoSymbol;
        if (k[-4] == k[-2] && k[-3] == k[-1])
            k[-2] = k[-1] = NoSymbol;
        if (k[-1] == k[-4] && k[-2] == k[-3]
            && k[-2] == NoSymbol)
            k[-1] = NoSymbol;
    }
}

static void
kbdLinuxGetMap(DevicePtr pDev, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    GETPRIV;
    KeySym *k, *mapCopy;
    char type;
    int i;

#if 00 /*BP*/
        mapCopy = malloc(sizeof(map));
    memcpy(mapCopy, map, sizeof(map));
#else
    ErrorF("kbdLinuxGetMap() is broken/no-op'd\n");
    return;
#endif

    kbdLinuxReadKernelMapping(priv->fd, pKeySyms);

    /* compute the modifier map */
    for (i = 0; i < MAP_LENGTH; i++)
        pModMap[i] = NoSymbol;  /* make sure it is restored */

    for (k = mapCopy, i = MIN_KEYCODE;
         i < NUM_KEYCODES + MIN_KEYCODE; i++, k += 4) {
        switch (*k) {
        case XK_Shift_L:
        case XK_Shift_R:
            pModMap[i] = ShiftMask;
            break;
        case XK_Control_L:
        case XK_Control_R:
            pModMap[i] = ControlMask;
            break;
        case XK_Caps_Lock:
            pModMap[i] = LockMask;
            break;
        case XK_Alt_L:
        case XK_Alt_R:
            pModMap[i] = AltMask;
            break;
        case XK_Num_Lock:
            pModMap[i] = NumLockMask;
            break;
        case XK_Scroll_Lock:
            pModMap[i] = ScrollLockMask;
            break;
        case XK_Kana_Lock:
        case XK_Kana_Shift:
            pModMap[i] = KanaMask;
            break;
        case XK_Mode_switch:
            pModMap[i] = AltLangMask;
            break;
        }
    }

    priv->kbdType = (ioctl(priv->fd, KDGKBTYPE, &type) < 0) ? KB_101 : type;

    pKeySyms->map = mapCopy;    /* Must be XFree'able */
    pKeySyms->mapWidth = GLYPHS_PER_KEY;
    pKeySyms->minKeyCode = MIN_KEYCODE;
    pKeySyms->maxKeyCode = MAX_KEYCODE;
}

/** Fill the \a info structure with information needed to initialize \a
 * pDev. */
void
kbdLinuxGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    info->keyboard = 1;
    info->keyClass = 1;
    kbdLinuxGetMap(pDev, &info->keySyms, info->modMap);
    info->focusClass = 1;
    info->kbdFeedbackClass = 1;
}
