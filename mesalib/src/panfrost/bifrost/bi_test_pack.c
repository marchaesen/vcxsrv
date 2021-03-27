/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler.h"

#ifdef NDEBUG

int
bi_test_packing_formats(void)
{
        /* stub */
        return 0;
}

#else

static void
bi_test_pack_format_1(void)
{
        /* Test case from the blob */
        struct bi_packed_tuple tuples[] = {
                { 0x2380cb1c02200000, 0x10e0 },
        };

        uint64_t header = 0x021000011800;

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        bi_pack_format(&result, 1, tuples, 1, header, 0, 0, true);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 16);
        assert(result_u64[0] == 0x80cb1c022000004a);
        assert(result_u64[1] == 0x10800008c000e023);
}

static void
bi_test_pack_format_2(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0x9380cb6044000044, 0xf65 },
                { 0xaf8721a05c000081, 0x1831 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        bi_pack_format(&result, 0, tuples, 2, 0x52800011800, 0, 0, false);
        bi_pack_format(&result, 2, tuples, 2, 0x52800011800, 0, 0, false);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 32);
        assert(result_u64[0] == 0x80cb604400004429);
        assert(result_u64[1] == 0x29400008c0076593);
        assert(result_u64[2] == 0x8721a05c00008103);
        assert(result_u64[3] == 0x60000000000031af);
}

static void
bi_test_pack_format_3(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0x93805b8040000000, 0xf65 },
                { 0x93886db05c000000, 0xf65 },
                { 0xb380cb180c000080, 0x18b1 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        bi_pack_format(&result, 0, tuples, 3, 0x3100000000, 0, 0, true);
        bi_pack_format(&result, 3, tuples, 3, 0x3100000000, 0, 0, true);
        bi_pack_format(&result, 4, tuples, 3, 0x3100000000, 0, 0, true);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 48);
        assert(result_u64[0] == 0x805b804000000029);
        assert(result_u64[1] == 0x188000000076593);
        assert(result_u64[2] == 0x886db05c00000021);
        assert(result_u64[3] == 0x58c0600004076593);
        assert(result_u64[4] == 0x44);
        assert(result_u64[5] == 0x60002c6ce0300000);
}

static void
bi_test_pack_format_4(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0xad8c87004000005f, 0x2f18 },
                { 0xad8c87385c00004f, 0x2f18 },
                { 0xad8c87385c00006e, 0x2f18 },
                { 0xb380cb182c000080, 0x18b1 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        uint64_t EC0 = (0x10000001ff000000) >> 4;

        bi_pack_format(&result, 0, tuples, 4, 0x3100000000, EC0, 0, false);
        bi_pack_format(&result, 3, tuples, 4, 0x3100000000, EC0, 0, false);
        bi_pack_format(&result, 6, tuples, 4, 0x3100000000, EC0, 0, false);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 48);
        assert(result_u64[0] == 0x8c87004000005f2d);
        assert(result_u64[1] == 0x1880000000718ad);
        assert(result_u64[2] == 0x8c87385c00004f25);
        assert(result_u64[3] == 0x39c2e000037718ad);
        assert(result_u64[4] == 0x80cb182c00008005);
        assert(result_u64[5] == 0xac01c62b6320b1b3);
}

static void
bi_test_pack_format_5(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0x9380688040000000, 0xf65 },
                { 0xd4057300c000040, 0xf26 },
                { 0x1f80cb1858000000, 0x19ab },
                { 0x937401f85c000000, 0xf65 },
                { 0xb380cb180c000080, 0x18a1 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        uint64_t EC0 = (0x183f800000) >> 4;

        bi_pack_format(&result, 0, tuples, 5, 0x3100000000, EC0, 0, true);
        bi_pack_format(&result, 3, tuples, 5, 0x3100000000, EC0, 0, true);
        bi_pack_format(&result, 7, tuples, 5, 0x3100000000, EC0, 0, true);
        bi_pack_format(&result, 8, tuples, 5, 0x3100000000, EC0, 0, true);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 64);
        assert(result_u64[0] == 0x8068804000000029);
        assert(result_u64[1] == 0x188000000076593);
        assert(result_u64[2] == 0x4057300c00004021);
        assert(result_u64[3] == 0x58c2c0000007260d);
        assert(result_u64[4] == 0x7401f85c0000008b);
        assert(result_u64[5] == 0x6ac7e0376593);
        assert(result_u64[6] == 0x80cb180c00008053);
        assert(result_u64[7] == 0x183f80a1b3);
}

static void
bi_test_pack_format_6(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0xad8c870068000048, 0x2f18 },
                { 0xad8c87385c000050, 0x2f18 },
                { 0xad8c87385c00006a, 0x2f18 },
                { 0xad8c87385c000074, 0x2f18 },
                { 0xad8c87385c000020, 0x2f18 },
                { 0xad8c87385c000030, 0x2f18 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        uint64_t EC0 = (0x345678912345670) >> 4;

        bi_pack_format(&result, 0, tuples, 6, 0x60000011800, EC0, 0, false);
        bi_pack_format(&result, 3, tuples, 6, 0x60000011800, EC0, 0, false);
        bi_pack_format(&result, 5, tuples, 6, 0x60000011800, EC0, 0, false);
        bi_pack_format(&result, 9, tuples, 6, 0x60000011800, EC0, 0, false);
        bi_pack_format(&result, 10, tuples, 6, 0x60000011800, EC0, 0, false);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 80);
        assert(result_u64[0] == 0x8c8700680000482d);
        assert(result_u64[1] == 0x30000008c00718ad);
        assert(result_u64[2] == 0x8c87385c00005025);
        assert(result_u64[3] == 0x39c2e000035718ad);
        assert(result_u64[4] == 0x8c87385c00007401);
        assert(result_u64[5] == 0xb401c62b632718ad);
        assert(result_u64[6] == 0x8c87385c00002065);
        assert(result_u64[7] == 0x39c2e000018718ad);
        assert(result_u64[8] == 0x3456789123456706);
        assert(result_u64[9] == 0xa001c62b63200000);
}

static void
bi_test_pack_format_7(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0x9020074040000083, 0xf65 },
                { 0x90000d4058100080, 0xf65 },
                { 0x90000a3058700082, 0xf65 },
                { 0x9020074008114581, 0xf65 },
                { 0x90000d0058000080, 0xf65 },
                { 0x9000083058700082, 0xf65 },
                { 0x2380cb199ac38400, 0x327a },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        bi_pack_format(&result, 0, tuples, 7, 0x3000100000, 0, 0, true);
        bi_pack_format(&result, 3, tuples, 7, 0x3000100000, 0, 0, true);
        bi_pack_format(&result, 5, tuples, 7, 0x3000100000, 0, 0, true);
        bi_pack_format(&result, 9, tuples, 7, 0x3000100000, 0, 0, true);
        bi_pack_format(&result, 11, tuples, 7, 0x3000100000, 0, 0, true);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 80);
        assert(result_u64[0] == 0x2007404000008329);
        assert(result_u64[1] == 0x180008000076590);
        assert(result_u64[2] == 0xd405810008021);
        assert(result_u64[3] == 0x5182c38004176590);
        assert(result_u64[4] == 0x2007400811458101);
        assert(result_u64[5] == 0x2401d96400076590);
        assert(result_u64[6] == 0xd005800008061);
        assert(result_u64[7] == 0x4182c38004176590);
        assert(result_u64[8] == 0x80cb199ac3840047);
        assert(result_u64[9] == 0x3801d96400027a23);
}

static void
bi_test_pack_format_8(void)
{
        struct bi_packed_tuple tuples[] = {
                { 0x442087037a2f8643, 0x3021 },
                { 0x84008d0586100043, 0x200 },
                { 0x7c008d0028014543, 0x0 },
                { 0x1c00070058200081, 0x1980 },
                { 0x1600dd878320400, 0x200 },
                { 0x49709c1b08308900, 0x200 },
                { 0x6c2007807881ca00, 0x40 },
                { 0x8d70fc0d94900083, 0x800 },
        };

        struct util_dynarray result;
        util_dynarray_init(&result, NULL);

        uint64_t EC0 = (0x32e635d0) >> 4;

        bi_pack_format(&result, 0, tuples, 8, 0x61001311800, EC0, 0, true);
        bi_pack_format(&result, 3, tuples, 8, 0x61001311800, EC0, 0, true);
        bi_pack_format(&result, 5, tuples, 8, 0x61001311800, EC0, 0, true);
        bi_pack_format(&result, 9, tuples, 8, 0x61001311800, EC0, 0, true);
        bi_pack_format(&result, 12, tuples, 8, 0x61001311800, EC0, 0, true);
        bi_pack_format(&result, 13, tuples, 8, 0x61001311800, EC0, 0, true);
        uint64_t *result_u64 = (uint64_t *) result.data;

        assert(result.size == 96);
        assert(result_u64[0] == 0x2087037a2f86432e);
        assert(result_u64[1] == 0x30800988c0002144);
        assert(result_u64[2] == 0x8d058610004320);
        assert(result_u64[3] == 0x6801400a2a1a0084);
        assert(result_u64[4] == 0x7005820008101);
        assert(result_u64[5] == 0xc00001f0021801c);
        assert(result_u64[6] == 0x600dd87832040060);
        assert(result_u64[7] == 0xe0d8418448020001);
        assert(result_u64[8] == 0x2007807881ca00c0);
        assert(result_u64[9] == 0xc6ba80125c20406c);
        assert(result_u64[10] == 0x70fc0d9490008359);
        assert(result_u64[11] == 0x32e0008d);
}

int
bi_test_packing_formats(void)
{
        bi_test_pack_format_1();
        bi_test_pack_format_2();
        bi_test_pack_format_3();
        bi_test_pack_format_4();
        bi_test_pack_format_5();
        bi_test_pack_format_6();
        bi_test_pack_format_7();
        bi_test_pack_format_8();

        return 0;
}

#endif
