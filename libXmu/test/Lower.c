/*
 * Copyright (c) 2022, 2023, Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Test code for functions in src/Lower.c */
#include "config.h"
#include <X11/Xmu/CharSet.h>
#include <X11/Xmu/SysUtil.h>
#define  XK_LATIN1
#include <X11/keysymdef.h>
#include <glib.h>
#include <string.h>

static const char upper[] = {
    XK_space, XK_exclam, XK_quotedbl, XK_numbersign, XK_dollar, XK_percent,
    XK_ampersand, XK_apostrophe, XK_quoteright, XK_parenleft, XK_parenright,
    XK_asterisk, XK_plus, XK_comma, XK_minus, XK_period, XK_slash,
    XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9,
    XK_colon, XK_semicolon, XK_less, XK_equal, XK_greater, XK_question, XK_at,
    XK_A, XK_B, XK_C, XK_D, XK_E, XK_F, XK_G, XK_H, XK_I, XK_J, XK_K, XK_L, XK_M,
    XK_N, XK_O, XK_P, XK_Q, XK_R, XK_S, XK_T, XK_U, XK_V, XK_W, XK_X, XK_Y, XK_Z,
    XK_bracketleft, XK_backslash, XK_bracketright, XK_asciicircum, XK_underscore,
    XK_grave, XK_quoteleft, XK_braceleft, XK_bar, XK_braceright, XK_asciitilde,
    XK_exclamdown, XK_cent, XK_sterling, XK_currency, XK_yen, XK_brokenbar,
    XK_section, XK_diaeresis, XK_copyright, XK_ordfeminine, XK_guillemotleft,
    XK_notsign, XK_hyphen, XK_registered, XK_macron, XK_degree, XK_plusminus,
    XK_twosuperior, XK_threesuperior, XK_acute, XK_mu, XK_paragraph,
    XK_periodcentered, XK_cedilla, XK_onesuperior, XK_masculine,
    XK_guillemotright, XK_onequarter, XK_onehalf, XK_threequarters,
    XK_questiondown, XK_multiply, XK_division,
    XK_Agrave, XK_Aacute, XK_Acircumflex, XK_Atilde, XK_Adiaeresis, XK_Aring,
    XK_AE, XK_Ccedilla, XK_Egrave, XK_Eacute, XK_Ecircumflex, XK_Ediaeresis,
    XK_Igrave, XK_Iacute, XK_Icircumflex, XK_Idiaeresis, XK_ETH, XK_Ntilde,
    XK_Ograve, XK_Oacute, XK_Ocircumflex, XK_Otilde, XK_Odiaeresis, XK_Oslash,
    XK_Ooblique, XK_Ugrave, XK_Uacute, XK_Ucircumflex, XK_Udiaeresis, XK_Yacute,
    XK_THORN, XK_ydiaeresis, '\0'
};

static const char lower[] = {
    XK_space, XK_exclam, XK_quotedbl, XK_numbersign, XK_dollar, XK_percent,
    XK_ampersand, XK_apostrophe, XK_quoteright, XK_parenleft, XK_parenright,
    XK_asterisk, XK_plus, XK_comma, XK_minus, XK_period, XK_slash,
    XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9,
    XK_colon, XK_semicolon, XK_less, XK_equal, XK_greater, XK_question, XK_at,
    XK_a, XK_b, XK_c, XK_d, XK_e, XK_f, XK_g, XK_h, XK_i, XK_j, XK_k, XK_l, XK_m,
    XK_n, XK_o, XK_p, XK_q, XK_r, XK_s, XK_t, XK_u, XK_v, XK_w, XK_x, XK_y, XK_z,
    XK_bracketleft, XK_backslash, XK_bracketright, XK_asciicircum, XK_underscore,
    XK_grave, XK_quoteleft, XK_braceleft, XK_bar, XK_braceright, XK_asciitilde,
    XK_exclamdown, XK_cent, XK_sterling, XK_currency, XK_yen, XK_brokenbar,
    XK_section, XK_diaeresis, XK_copyright, XK_ordfeminine, XK_guillemotleft,
    XK_notsign, XK_hyphen, XK_registered, XK_macron, XK_degree, XK_plusminus,
    XK_twosuperior, XK_threesuperior, XK_acute, XK_mu, XK_paragraph,
    XK_periodcentered, XK_cedilla, XK_onesuperior, XK_masculine,
    XK_guillemotright, XK_onequarter, XK_onehalf, XK_threequarters,
    XK_questiondown, XK_multiply, XK_division,
    XK_agrave, XK_aacute, XK_acircumflex, XK_atilde, XK_adiaeresis, XK_aring,
    XK_ae, XK_ccedilla, XK_egrave, XK_eacute, XK_ecircumflex, XK_ediaeresis,
    XK_igrave, XK_iacute, XK_icircumflex, XK_idiaeresis, XK_eth, XK_ntilde,
    XK_ograve, XK_oacute, XK_ocircumflex, XK_otilde, XK_odiaeresis, XK_oslash,
    XK_ooblique, XK_ugrave, XK_uacute, XK_ucircumflex, XK_udiaeresis, XK_yacute,
    XK_thorn, XK_ydiaeresis, '\0'
};

static const char mixed[] = {
    XK_space, XK_exclam, XK_quotedbl, XK_numbersign, XK_dollar, XK_percent,
    XK_ampersand, XK_apostrophe, XK_quoteright, XK_parenleft, XK_parenright,
    XK_asterisk, XK_plus, XK_comma, XK_minus, XK_period, XK_slash,
    XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9,
    XK_colon, XK_semicolon, XK_less, XK_equal, XK_greater, XK_question, XK_at,
    XK_a, XK_b, XK_c, XK_d, XK_e, XK_f, XK_g, XK_h, XK_i, XK_j, XK_k, XK_l, XK_m,
    XK_N, XK_O, XK_P, XK_Q, XK_R, XK_S, XK_T, XK_U, XK_V, XK_W, XK_X, XK_Y, XK_Z,
    XK_bracketleft, XK_backslash, XK_bracketright, XK_asciicircum, XK_underscore,
    XK_grave, XK_quoteleft, XK_braceleft, XK_bar, XK_braceright, XK_asciitilde,
    XK_exclamdown, XK_cent, XK_sterling, XK_currency, XK_yen, XK_brokenbar,
    XK_section, XK_diaeresis, XK_copyright, XK_ordfeminine, XK_guillemotleft,
    XK_notsign, XK_hyphen, XK_registered, XK_macron, XK_degree, XK_plusminus,
    XK_twosuperior, XK_threesuperior, XK_acute, XK_mu, XK_paragraph,
    XK_periodcentered, XK_cedilla, XK_onesuperior, XK_masculine,
    XK_guillemotright, XK_onequarter, XK_onehalf, XK_threequarters,
    XK_questiondown, XK_multiply, XK_division,
    XK_agrave, XK_aacute, XK_acircumflex, XK_atilde, XK_adiaeresis, XK_aring,
    XK_AE, XK_Ccedilla, XK_Egrave, XK_Eacute, XK_Ecircumflex, XK_Ediaeresis,
    XK_igrave, XK_iacute, XK_icircumflex, XK_idiaeresis, XK_eth, XK_ntilde,
    XK_Ograve, XK_Oacute, XK_Ocircumflex, XK_Otilde, XK_Odiaeresis, XK_Oslash,
    XK_ooblique, XK_ugrave, XK_uacute, XK_ucircumflex, XK_udiaeresis, XK_yacute,
    XK_THORN, XK_ydiaeresis, '\0'
};

#define DATA_LEN sizeof(upper)

static void
test_XmuCopyISOLatin1Lowered(void)
{
    char buf[DATA_LEN];

    XmuCopyISOLatin1Lowered(buf, upper);
    g_assert_cmpstr(buf, ==, lower);

    XmuCopyISOLatin1Lowered(buf, lower);
    g_assert_cmpstr(buf, ==, lower);

    XmuCopyISOLatin1Lowered(buf, mixed);
    g_assert_cmpstr(buf, ==, lower);
}

static void
test_XmuCopyISOLatin1Uppered(void)
{
    char buf[DATA_LEN];

    XmuCopyISOLatin1Uppered(buf, upper);
    g_assert_cmpstr(buf, ==, upper);

    XmuCopyISOLatin1Uppered(buf, lower);
    g_assert_cmpstr(buf, ==, upper);

    XmuCopyISOLatin1Uppered(buf, mixed);
    g_assert_cmpstr(buf, ==, upper);
}

static void
test_XmuNCopyISOLatin1Lowered(void)
{
    char buf[DATA_LEN];

    XmuNCopyISOLatin1Lowered(buf, upper, DATA_LEN);
    g_assert_cmpstr(buf, ==, lower);

    XmuNCopyISOLatin1Lowered(buf, lower, DATA_LEN);
    g_assert_cmpstr(buf, ==, lower);

    XmuNCopyISOLatin1Lowered(buf, mixed, DATA_LEN);
    g_assert_cmpstr(buf, ==, lower);
}

static void
test_XmuNCopyISOLatin1Uppered(void)
{
    char buf[DATA_LEN];

    XmuNCopyISOLatin1Uppered(buf, upper, DATA_LEN);
    g_assert_cmpstr(buf, ==, upper);

    XmuNCopyISOLatin1Uppered(buf, lower, DATA_LEN);
    g_assert_cmpstr(buf, ==, upper);

    XmuNCopyISOLatin1Uppered(buf, mixed, DATA_LEN);
    g_assert_cmpstr(buf, ==, upper);
}

static void
test_XmuCompareISOLatin1(void)
{
    int cmp;

    cmp = XmuCompareISOLatin1(upper, lower);
    g_assert_cmpint(cmp, ==, 0);

    cmp = XmuCompareISOLatin1(upper, mixed);
    g_assert_cmpint(cmp, ==, 0);

    cmp = XmuCompareISOLatin1(lower, mixed);
    g_assert_cmpint(cmp, ==, 0);

    cmp = XmuCompareISOLatin1(upper + 1, lower);
    g_assert_cmpint(cmp, >, 0);

    cmp = XmuCompareISOLatin1(mixed, lower + 1);
    g_assert_cmpint(cmp, <, 0);
}

static void
test_XmuSnprintf(void)
{
    char buf[DATA_LEN];
    int ret;

    g_assert_cmpint(DATA_LEN, >, 40);
    ret = XmuSnprintf(buf, 40, "%s", upper);
    g_assert_cmpint(ret, ==, sizeof(upper) - 1);
    g_assert_cmpint(buf[39], ==, 0);
    g_assert_cmpmem(buf, 39, upper, 39);

    ret = XmuSnprintf(buf, sizeof(buf), "%s", upper);
    g_assert_cmpint(ret, ==, sizeof(upper) - 1);
    g_assert_cmpstr(buf, ==, upper);

    ret = XmuSnprintf(buf, sizeof(buf), "%d", 12345678);
    g_assert_cmpint(ret, ==, 8);
    g_assert_cmpstr(buf, ==, "12345678");
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_assert_cmpuint(sizeof(upper), ==, sizeof(lower));
    g_assert_cmpuint(sizeof(upper), ==, sizeof(mixed));

    g_test_add_func("/Lower/XmuCopyISOLatin1Lowered",
                    test_XmuCopyISOLatin1Lowered);
    g_test_add_func("/Lower/XmuCopyISOLatin1Uppered",
                    test_XmuCopyISOLatin1Uppered);

    g_test_add_func("/Lower/XmuNCopyISOLatin1Lowered",
                    test_XmuNCopyISOLatin1Lowered);
    g_test_add_func("/Lower/XmuNCopyISOLatin1Uppered",
                    test_XmuNCopyISOLatin1Uppered);

    g_test_add_func("/Lower/XmuCompareISOLatin1",
                    test_XmuCompareISOLatin1);
    g_test_add_func("/Lower/XmuSnprintf",
                    test_XmuSnprintf);


    return g_test_run();
}
