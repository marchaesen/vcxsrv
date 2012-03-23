/*
 * Copyright © 1999 Keith Packard
 * XKB integration © 2006 Nokia Corporation, author: Tomas Frydrych <tf@o-hand.com>
 *
 * LinuxKeyboardRead() XKB code based on xf86KbdLnx.c:
 * Copyright © 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright © 1994-2001 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "kdrive.h"
#include <linux/keyboard.h>
#include <linux/kd.h>
#define XK_PUBLISHING
#include <X11/keysym.h>
#include <termios.h>
#include <sys/ioctl.h>

extern int LinuxConsoleFd;

static const KeySym linux_to_x[256] = {
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
 * Getting a keycode from scancode
 *
 * With XKB
 * --------
 *
 * We have to enqueue keyboard events using standard X keycodes which correspond
 * to AT scancode + 8; this means that we need to translate the Linux scancode
 * provided by the kernel to an AT scancode -- this translation is not linear
 * and requires that we use a LUT.
 *
 *
 * Without XKB
 * -----------
 *
 * We can use custom keycodes, which makes things simpler; we define our custom
 * keycodes as Linux scancodes + KD_KEY_OFFSET
*/

/*
   This LUT translates AT scancodes into Linux ones -- the keymap we create
   for the core X keyboard protocol has to be AT-scancode based so that it
   corresponds to the Xkb keymap.
*/
#if 0
static unsigned char at2lnx[] = {
    0x0,                        /* no valid scancode */
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

#define NUM_AT_KEYS (sizeof(at2lnx)/sizeof(at2lnx[0]))
#define LNX_KEY_INDEX(n) n < NUM_AT_KEYS ? at2lnx[n] : 0

static unsigned char tbl[KD_MAX_WIDTH] = {
    0,
    1 << KG_SHIFT,
    (1 << KG_ALTGR),
    (1 << KG_ALTGR) | (1 << KG_SHIFT)
};
#endif

static void
readKernelMapping(KdKeyboardInfo * ki)
{
#if 0
    KeySym *k;
    int i, j;
    struct kbentry kbe;
    int minKeyCode, maxKeyCode;
    int row;
    int fd;

    if (!ki)
        return;

    fd = LinuxConsoleFd;

    minKeyCode = NR_KEYS;
    maxKeyCode = 0;
    row = 0;
    ki->keySyms.mapWidth = KD_MAX_WIDTH;
    for (i = 0; i < NR_KEYS && row < KD_MAX_LENGTH; ++i) {
        kbe.kb_index = LNX_KEY_INDEX(i);

        k = ki->keySyms.map + row * ki->keySyms.mapWidth;

        for (j = 0; j < ki->keySyms.mapWidth; ++j) {
            unsigned short kval;

            k[j] = NoSymbol;

            kbe.kb_table = tbl[j];
            kbe.kb_value = 0;
            if (ioctl(fd, KDGKBENT, &kbe))
                continue;

            kval = KVAL(kbe.kb_value);
            switch (KTYP(kbe.kb_value)) {
            case KT_LATIN:
            case KT_LETTER:
                k[j] = linux_to_x[kval];
                break;

            case KT_FN:
                if (kval <= 19)
                    k[j] = XK_F1 + kval;
                else
                    switch (kbe.kb_value) {
                    case K_FIND:
                        k[j] = XK_Home; /* or XK_Find */
                        break;
                    case K_INSERT:
                        k[j] = XK_Insert;
                        break;
                    case K_REMOVE:
                        k[j] = XK_Delete;
                        break;
                    case K_SELECT:
                        k[j] = XK_End;  /* or XK_Select */
                        break;
                    case K_PGUP:
                        k[j] = XK_Prior;
                        break;
                    case K_PGDN:
                        k[j] = XK_Next;
                        break;
                    case K_HELP:
                        k[j] = XK_Help;
                        break;
                    case K_DO:
                        k[j] = XK_Execute;
                        break;
                    case K_PAUSE:
                        k[j] = XK_Pause;
                        break;
                    case K_MACRO:
                        k[j] = XK_Menu;
                        break;
                    default:
                        break;
                    }
                break;

            case KT_SPEC:
                switch (kbe.kb_value) {
                case K_ENTER:
                    k[j] = XK_Return;
                    break;
                case K_BREAK:
                    k[j] = XK_Break;
                    break;
                case K_CAPS:
                    k[j] = XK_Caps_Lock;
                    break;
                case K_NUM:
                    k[j] = XK_Num_Lock;
                    break;
                case K_HOLD:
                    k[j] = XK_Scroll_Lock;
                    break;
                case K_COMPOSE:
                    k[j] = XK_Multi_key;
                    break;
                default:
                    break;
                }
                break;

            case KT_PAD:
                switch (kbe.kb_value) {
                case K_PPLUS:
                    k[j] = XK_KP_Add;
                    break;
                case K_PMINUS:
                    k[j] = XK_KP_Subtract;
                    break;
                case K_PSTAR:
                    k[j] = XK_KP_Multiply;
                    break;
                case K_PSLASH:
                    k[j] = XK_KP_Divide;
                    break;
                case K_PENTER:
                    k[j] = XK_KP_Enter;
                    break;
                case K_PCOMMA:
                    k[j] = XK_KP_Separator;
                    break;
                case K_PDOT:
                    k[j] = XK_KP_Decimal;
                    break;
                case K_PPLUSMINUS:
                    k[j] = XK_KP_Subtract;
                    break;
                default:
                    if (kval <= 9)
                        k[j] = XK_KP_0 + kval;
                    break;
                }
                break;

                /*
                 * KT_DEAD keys are for accelerated diacritical creation.
                 */
            case KT_DEAD:
                switch (kbe.kb_value) {
                case K_DGRAVE:
                    k[j] = XK_dead_grave;
                    break;
                case K_DACUTE:
                    k[j] = XK_dead_acute;
                    break;
                case K_DCIRCM:
                    k[j] = XK_dead_circumflex;
                    break;
                case K_DTILDE:
                    k[j] = XK_dead_tilde;
                    break;
                case K_DDIERE:
                    k[j] = XK_dead_diaeresis;
                    break;
                }
                break;

            case KT_CUR:
                switch (kbe.kb_value) {
                case K_DOWN:
                    k[j] = XK_Down;
                    break;
                case K_LEFT:
                    k[j] = XK_Left;
                    break;
                case K_RIGHT:
                    k[j] = XK_Right;
                    break;
                case K_UP:
                    k[j] = XK_Up;
                    break;
                }
                break;

            case KT_SHIFT:
                switch (kbe.kb_value) {
                case K_ALTGR:
                    k[j] = XK_Mode_switch;
                    break;
                case K_ALT:
                    k[j] = (kbe.kb_index == 0x64 ? XK_Alt_R : XK_Alt_L);
                    break;
                case K_CTRL:
                    k[j] = (kbe.kb_index == 0x61 ? XK_Control_R : XK_Control_L);
                    break;
                case K_CTRLL:
                    k[j] = XK_Control_L;
                    break;
                case K_CTRLR:
                    k[j] = XK_Control_R;
                    break;
                case K_SHIFT:
                    k[j] = (kbe.kb_index == 0x36 ? XK_Shift_R : XK_Shift_L);
                    break;
                case K_SHIFTL:
                    k[j] = XK_Shift_L;
                    break;
                case K_SHIFTR:
                    k[j] = XK_Shift_R;
                    break;
                default:
                    break;
                }
                break;

                /*
                 * KT_ASCII keys accumulate a 3 digit decimal number that gets
                 * emitted when the shift state changes. We can't emulate that.
                 */
            case KT_ASCII:
                break;

            case KT_LOCK:
                if (kbe.kb_value == K_SHIFTLOCK)
                    k[j] = XK_Shift_Lock;
                break;

#ifdef KT_X
            case KT_X:
                /* depends on new keyboard symbols in file linux/keyboard.h */
                if (kbe.kb_value == K_XMENU)
                    k[j] = XK_Menu;
                if (kbe.kb_value == K_XTELEPHONE)
                    k[j] = XK_telephone;
                break;
#endif
#ifdef KT_XF
            case KT_XF:
                /* special linux keysyms which map directly to XF86 keysyms */
                k[j] = (kbe.kb_value & 0xFF) + 0x1008FF00;
                break;
#endif

            default:
                break;
            }
            if (i < minKeyCode)
                minKeyCode = i;
            if (i > maxKeyCode)
                maxKeyCode = i;
        }

        if (minKeyCode == NR_KEYS)
            continue;

        if (k[3] == k[2])
            k[3] = NoSymbol;
        if (k[2] == k[1])
            k[2] = NoSymbol;
        if (k[1] == k[0])
            k[1] = NoSymbol;
        if (k[0] == k[2] && k[1] == k[3])
            k[2] = k[3] = NoSymbol;
        if (k[3] == k[0] && k[2] == k[1] && k[2] == NoSymbol)
            k[3] = NoSymbol;
        row++;
    }
    ki->minScanCode = minKeyCode;
    ki->maxScanCode = maxKeyCode;
#endif
}

/*
 * We need these to handle extended scancodes correctly (I could just use the
 * numbers below, but this makes the code more readable
 */

/* The prefix codes */
#define KEY_Prefix0      /* special               0x60  */   96
#define KEY_Prefix1      /* special               0x61  */   97

/* The raw scancodes */
#define KEY_Enter        /* Enter                 0x1c  */   28
#define KEY_LCtrl        /* Ctrl(left)            0x1d  */   29
#define KEY_Slash        /* / (Slash)   ?         0x35  */   53
#define KEY_KP_Multiply  /* *                     0x37  */   55
#define KEY_Alt          /* Alt(left)             0x38  */   56
#define KEY_F3           /* F3                    0x3d  */   61
#define KEY_F4           /* F4                    0x3e  */   62
#define KEY_F5           /* F5                    0x3f  */   63
#define KEY_F6           /* F6                    0x40  */   64
#define KEY_F7           /* F7                    0x41  */   65
#define KEY_ScrollLock   /* ScrollLock            0x46  */   70
#define KEY_KP_7         /* 7           Home      0x47  */   71
#define KEY_KP_8         /* 8           Up        0x48  */   72
#define KEY_KP_9         /* 9           PgUp      0x49  */   73
#define KEY_KP_Minus     /* - (Minus)             0x4a  */   74
#define KEY_KP_4         /* 4           Left      0x4b  */   75
#define KEY_KP_5         /* 5                     0x4c  */   76
#define KEY_KP_6         /* 6           Right     0x4d  */   77
#define KEY_KP_Plus      /* + (Plus)              0x4e  */   78
#define KEY_KP_1         /* 1           End       0x4f  */   79
#define KEY_KP_2         /* 2           Down      0x50  */   80
#define KEY_KP_3         /* 3           PgDown    0x51  */   81
#define KEY_KP_0         /* 0           Insert    0x52  */   82
#define KEY_KP_Decimal   /* . (Decimal) Delete    0x53  */   83
#define KEY_Home         /* Home                  0x59  */   89
#define KEY_Up           /* Up                    0x5a  */   90
#define KEY_PgUp         /* PgUp                  0x5b  */   91
#define KEY_Left         /* Left                  0x5c  */   92
#define KEY_Begin        /* Begin                 0x5d  */   93
#define KEY_Right        /* Right                 0x5e  */   94
#define KEY_End          /* End                   0x5f  */   95
#define KEY_Down         /* Down                  0x60  */   96
#define KEY_PgDown       /* PgDown                0x61  */   97
#define KEY_Insert       /* Insert                0x62  */   98
#define KEY_Delete       /* Delete                0x63  */   99
#define KEY_KP_Enter     /* Enter                 0x64  */  100
#define KEY_RCtrl        /* Ctrl(right)           0x65  */  101
#define KEY_Pause        /* Pause                 0x66  */  102
#define KEY_Print        /* Print                 0x67  */  103
#define KEY_KP_Divide    /* Divide                0x68  */  104
#define KEY_AltLang      /* AtlLang(right)        0x69  */  105
#define KEY_Break        /* Break                 0x6a  */  106
#define KEY_LMeta        /* Left Meta             0x6b  */  107
#define KEY_RMeta        /* Right Meta            0x6c  */  108
#define KEY_Menu         /* Menu                  0x6d  */  109
#define KEY_F13          /* F13                   0x6e  */  110
#define KEY_F14          /* F14                   0x6f  */  111
#define KEY_F15          /* F15                   0x70  */  112
#define KEY_F16          /* F16                   0x71  */  113
#define KEY_F17          /* F17                   0x72  */  114
#define KEY_KP_DEC       /* KP_DEC                0x73  */  115

static void
LinuxKeyboardRead(int fd, void *closure)
{
    unsigned char buf[256], *b;
    int n;
    unsigned char prefix = 0, scancode = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        b = buf;
        while (n--) {
            /*
             * With xkb we use RAW mode for reading the console, which allows us
             * process extended scancodes.
             *
             * See if this is a prefix extending the following keycode
             */
            if (!prefix && ((b[0] & 0x7f) == KEY_Prefix0)) {
                prefix = KEY_Prefix0;
                /* swallow this up */
                b++;
                continue;
            }
            else if (!prefix && ((b[0] & 0x7f) == KEY_Prefix1)) {
                prefix = KEY_Prefix1;
                /* swallow this up */
                b++;
                continue;
            }
            scancode = b[0] & 0x7f;

            switch (prefix) {
                /* from xf86Events.c */
            case KEY_Prefix0:
            {
                switch (scancode) {
                case KEY_KP_7:
                    scancode = KEY_Home;
                    break;      /* curs home */
                case KEY_KP_8:
                    scancode = KEY_Up;
                    break;      /* curs up */
                case KEY_KP_9:
                    scancode = KEY_PgUp;
                    break;      /* curs pgup */
                case KEY_KP_4:
                    scancode = KEY_Left;
                    break;      /* curs left */
                case KEY_KP_5:
                    scancode = KEY_Begin;
                    break;      /* curs begin */
                case KEY_KP_6:
                    scancode = KEY_Right;
                    break;      /* curs right */
                case KEY_KP_1:
                    scancode = KEY_End;
                    break;      /* curs end */
                case KEY_KP_2:
                    scancode = KEY_Down;
                    break;      /* curs down */
                case KEY_KP_3:
                    scancode = KEY_PgDown;
                    break;      /* curs pgdown */
                case KEY_KP_0:
                    scancode = KEY_Insert;
                    break;      /* curs insert */
                case KEY_KP_Decimal:
                    scancode = KEY_Delete;
                    break;      /* curs delete */
                case KEY_Enter:
                    scancode = KEY_KP_Enter;
                    break;      /* keypad enter */
                case KEY_LCtrl:
                    scancode = KEY_RCtrl;
                    break;      /* right ctrl */
                case KEY_KP_Multiply:
                    scancode = KEY_Print;
                    break;      /* print */
                case KEY_Slash:
                    scancode = KEY_KP_Divide;
                    break;      /* keyp divide */
                case KEY_Alt:
                    scancode = KEY_AltLang;
                    break;      /* right alt */
                case KEY_ScrollLock:
                    scancode = KEY_Break;
                    break;      /* curs break */
                case 0x5b:
                    scancode = KEY_LMeta;
                    break;
                case 0x5c:
                    scancode = KEY_RMeta;
                    break;
                case 0x5d:
                    scancode = KEY_Menu;
                    break;
                case KEY_F3:
                    scancode = KEY_F13;
                    break;
                case KEY_F4:
                    scancode = KEY_F14;
                    break;
                case KEY_F5:
                    scancode = KEY_F15;
                    break;
                case KEY_F6:
                    scancode = KEY_F16;
                    break;
                case KEY_F7:
                    scancode = KEY_F17;
                    break;
                case KEY_KP_Plus:
                    scancode = KEY_KP_DEC;
                    break;
                    /* Ignore virtual shifts (E0 2A, E0 AA, E0 36, E0 B6) */
                case 0x2A:
                case 0x36:
                    b++;
                    prefix = 0;
                    continue;
                default:
                    /*
                     * "Internet" keyboards are generating lots of new
                     * codes.  Let them pass.  There is little consistency
                     * between them, so don't bother with symbolic names at
                     * this level.
                     */
                    scancode += 0x78;
                }
                break;
            }

            case KEY_Prefix1:
            {
                /* we do no handle these */
                b++;
                prefix = 0;
                continue;
            }

            default:           /* should not happen */
            case 0:            /* do nothing */
                ;
            }

            prefix = 0;
            KdEnqueueKeyboardEvent(closure, scancode, b[0] & 0x80);
            b++;
        }
    }
}

static int LinuxKbdTrans;
static struct termios LinuxTermios;

static Status
LinuxKeyboardEnable(KdKeyboardInfo * ki)
{
    struct termios nTty;
    unsigned char buf[256];
    int n;
    int fd;

    if (!ki)
        return !Success;

    fd = LinuxConsoleFd;
    ki->driverPrivate = (void *) (intptr_t) fd;

    ioctl(fd, KDGKBMODE, &LinuxKbdTrans);
    tcgetattr(fd, &LinuxTermios);
    ioctl(fd, KDSKBMODE, K_RAW);
    nTty = LinuxTermios;
    nTty.c_iflag = (IGNPAR | IGNBRK) & (~PARMRK) & (~ISTRIP);
    nTty.c_oflag = 0;
    nTty.c_cflag = CREAD | CS8;
    nTty.c_lflag = 0;
    nTty.c_cc[VTIME] = 0;
    nTty.c_cc[VMIN] = 1;
    cfsetispeed(&nTty, 9600);
    cfsetospeed(&nTty, 9600);
    tcsetattr(fd, TCSANOW, &nTty);
    /*
     * Flush any pending keystrokes
     */
    while ((n = read(fd, buf, sizeof(buf))) > 0);
    KdRegisterFd(fd, LinuxKeyboardRead, ki);
    return Success;
}

static void
LinuxKeyboardDisable(KdKeyboardInfo * ki)
{
    int fd;

    if (!ki)
        return;

    fd = (int) (intptr_t) ki->driverPrivate;

    KdUnregisterFd(ki, fd, FALSE);
    ioctl(fd, KDSKBMODE, LinuxKbdTrans);
    tcsetattr(fd, TCSANOW, &LinuxTermios);
}

static Status
LinuxKeyboardInit(KdKeyboardInfo * ki)
{
    if (!ki)
        return !Success;

    free(ki->path);
    ki->path = strdup("console");
    free(ki->name);
    ki->name = strdup("Linux console keyboard");

    readKernelMapping(ki);

    return Success;
}

static void
LinuxKeyboardLeds(KdKeyboardInfo * ki, int leds)
{
    if (!ki)
        return;

    ioctl((int) (intptr_t) ki->driverPrivate, KDSETLED, leds & 7);
}

KdKeyboardDriver LinuxKeyboardDriver = {
    "keyboard",
    .Init = LinuxKeyboardInit,
    .Enable = LinuxKeyboardEnable,
    .Leds = LinuxKeyboardLeds,
    .Disable = LinuxKeyboardDisable,
};
