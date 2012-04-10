/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
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

#include <X11/Intrinsic.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

static const char *program_name;

/* Just a long string of characters to pull from */
const char test_chars[] =
    "|000 nul|001 soh|002 stx|003 etx|004 eot|005 enq|006 ack|007 bel|"
    "|010 bs |011 ht |012 nl |013 vt |014 np |015 cr |016 so |017 si |"
    "|020 dle|021 dc1|022 dc2|023 dc3|024 dc4|025 nak|026 syn|027 etb|"
    "|030 can|031 em |032 sub|033 esc|034 fs |035 gs |036 rs |037 us |"
    "|040 sp |041  ! |042  \" |043  # |044  $ |045  % |046  & |047  ' |"
    "|050  ( |051  ) |052  * |053  + |054  , |055  - |056  . |057  / |"
    "|060  0 |061  1 |062  2 |063  3 |064  4 |065  5 |066  6 |067  7 |"
    "|070  8 |071  9 |072  : |073  ; |074  < |075  = |076  > |077  ? |"
    "|100  @ |101  A |102  B |103  C |104  D |105  E |106  F |107  G |"
    "|110  H |111  I |112  J |113  K |114  L |115  M |116  N |117  O |"
    "|120  P |121  Q |122  R |123  S |124  T |125  U |126  V |127  W |"
    "|130  X |131  Y |132  Z |133  [ |134  \\ |135  ] |136  ^ |137  _ |"
    "|140  ` |141  a |142  b |143  c |144  d |145  e |146  f |147  g |"
    "|150  h |151  i |152  j |153  k |154  l |155  m |156  n |157  o |"
    "|160  p |161  q |162  r |163  s |164  t |165  u |166  v |167  w |"
    "|170  x |171  y |172  z |173  { |174  | |175  } |176  ~ |177 del|"
    "| 00 nul| 01 soh| 02 stx| 03 etx| 04 eot| 05 enq| 06 ack| 07 bel|"
    "| 08 bs | 09 ht | 0a nl | 0b vt | 0c np | 0d cr | 0e so | 0f si |"
    "| 10 dle| 11 dc1| 12 dc2| 13 dc3| 14 dc4| 15 nak| 16 syn| 17 etb|"
    "| 18 can| 19 em | 1a sub| 1b esc| 1c fs | 1d gs | 1e rs | 1f us |"
    "| 20 sp | 21  ! | 22  \" | 23  # | 24  $ | 25  % | 26  & | 27  ' |"
    "| 28  ( | 29  ) | 2a  * | 2b  + | 2c  , | 2d  - | 2e  . | 2f  / |"
    "| 30  0 | 31  1 | 32  2 | 33  3 | 34  4 | 35  5 | 36  6 | 37  7 |"
    "| 38  8 | 39  9 | 3a  : | 3b  ; | 3c  < | 3d  = | 3e  > | 3f  ? |"
    "| 40  @ | 41  A | 42  B | 43  C | 44  D | 45  E | 46  F | 47  G |"
    "| 48  H | 49  I | 4a  J | 4b  K | 4c  L | 4d  M | 4e  N | 4f  O |"
    "| 50  P | 51  Q | 52  R | 53  S | 54  T | 55  U | 56  V | 57  W |"
    "| 58  X | 59  Y | 5a  Z | 5b  [ | 5c  \\ | 5d  ] | 5e  ^ | 5f  _ |"
    "| 60  ` | 61  a | 62  b | 63  c | 64  d | 65  e | 66  f | 67  g |"
    "| 68  h | 69  i | 6a  j | 6b  k | 6c  l | 6d  m | 6e  n | 6f  o |"
    "| 70  p | 71  q | 72  r | 73  s | 74  t | 75  u | 76  v | 77  w |"
    "| 78  x | 79  y | 7a  z | 7b  { | 7c  | | 7d  } | 7e  ~ | 7f del|";


/* Test a simple short string & int */
static void test_XtAsprintf_short(void)
{
    char snbuf[1024];
    char *asbuf;
    gint32 r = g_test_rand_int();
    int snlen, aslen;

    snlen = snprintf(snbuf, sizeof(snbuf), "%s: %d\n", program_name, r);
    aslen = XtAsprintf(&asbuf, "%s: %d\n", program_name, r);

    g_assert(asbuf != NULL);
    g_assert(snlen == aslen);
    g_assert(strcmp(snbuf, asbuf) == 0);
    g_assert(asbuf[aslen] == '\0');
}

/* Test a string long enough to be past the 256 character limit that
   makes XtAsprintf re-run snprintf after allocating memory */
static void test_XtAsprintf_long(void)
{
    char *asbuf;
    int aslen;
    gint r1 = g_test_rand_int_range(0, 256);
    gint r2 = g_test_rand_int_range(1024, sizeof(test_chars) - r1);

    aslen = XtAsprintf(&asbuf, "%.*s", r2, test_chars + r1);

    g_assert(asbuf != NULL);
    g_assert(aslen == r2);
    g_assert(strncmp(asbuf, test_chars + r1, r2) == 0);
    g_assert(asbuf[aslen] == '\0');
}

int main(int argc, char** argv)
{
    program_name = argv[0];

    g_test_init(&argc, &argv, NULL);
    g_test_bug_base("https://bugzilla.freedesktop.org/show_bug.cgi?id=");

    g_test_add_func("/Alloc/XtAsprintf/short", test_XtAsprintf_short);
    g_test_add_func("/Alloc/XtAsprintf/long", test_XtAsprintf_long);

    return g_test_run();
}
