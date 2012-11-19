#include <check.h>
#include <string.h>
#include <stdlib.h>
#include "check_suites.h"
#include "xcb.h"
#include "xcbext.h"

/* xcb_parse_display tests {{{ */

typedef enum test_type_t {
	TEST_ARGUMENT, TEST_ENVIRONMENT, TEST_END
} test_type_t;
static const char *const test_string[] = { "", "via $DISPLAY " };

static void parse_display_pass(const char *name, const char *host, const int display, const int screen)
{
	int success;
	char *got_host;
	int got_display, got_screen;
	const char *argument = 0;
	test_type_t test_type;

	for(test_type = TEST_ARGUMENT; test_type != TEST_END; test_type++)
	{
		if(test_type == TEST_ARGUMENT)
		{
			argument = name;
			putenv("DISPLAY=");
		}
		else if(test_type == TEST_ENVIRONMENT)
		{
			argument = 0;
			setenv("DISPLAY", name, 1);
		}

		got_host = (char *) -1;
		got_display = got_screen = -42;
		mark_point();
		success = xcb_parse_display(argument, &got_host, &got_display, &got_screen);
		fail_unless(success, "unexpected parse failure %sfor '%s'", test_string[test_type], name);
		fail_unless(strcmp(host, got_host) == 0, "parse %sproduced unexpected hostname '%s' for '%s': expected '%s'", test_string[test_type], got_host, name, host);
		fail_unless(display == got_display, "parse %sproduced unexpected display '%d' for '%s': expected '%d'", test_string[test_type], got_display, name, display);
		fail_unless(screen == got_screen, "parse %sproduced unexpected screen '%d' for '%s': expected '%d'", test_string[test_type], got_screen, name, screen);

		got_host = (char *) -1;
		got_display = got_screen = -42;
		mark_point();
		success = xcb_parse_display(argument, &got_host, &got_display, 0);
		fail_unless(success, "unexpected screenless parse failure %sfor '%s'", test_string[test_type], name);
		fail_unless(strcmp(host, got_host) == 0, "screenless parse %sproduced unexpected hostname '%s' for '%s': expected '%s'", test_string[test_type], got_host, name, host);
		fail_unless(display == got_display, "screenless parse %sproduced unexpected display '%d' for '%s': expected '%d'", test_string[test_type], got_display, name, display);
	}
	putenv("DISPLAY=");
}

static void parse_display_fail(const char *name)
{
	int success;
	char *got_host;
	int got_display, got_screen;
	const char *argument = 0;
	test_type_t test_type;

	for(test_type = TEST_ARGUMENT; test_type != TEST_END; test_type++)
	{
		if(test_type == TEST_ARGUMENT)
		{
			argument = name;
			putenv("DISPLAY=");
		}
		else if(test_type == TEST_ENVIRONMENT)
		{
			if (!name) break;
			argument = 0;
			setenv("DISPLAY", name, 1);
		}

		got_host = (char *) -1;
		got_display = got_screen = -42;
		mark_point();
		success = xcb_parse_display(argument, &got_host, &got_display, &got_screen);
		fail_unless(!success, "unexpected parse success %sfor '%s'", test_string[test_type], name);
		fail_unless(got_host == (char *) -1, "host changed on parse failure %sfor '%s': got %p", test_string[test_type], name, got_host);
		fail_unless(got_display == -42, "display changed on parse failure %sfor '%s': got %d", test_string[test_type], name, got_display);
		fail_unless(got_screen == -42, "screen changed on parse failure %sfor '%s': got %d", test_string[test_type], name, got_screen);

		got_host = (char *) -1;
		got_display = got_screen = -42;
		mark_point();
		success = xcb_parse_display(argument, &got_host, &got_display, 0);
		fail_unless(!success, "unexpected screenless parse success %sfor '%s'", test_string[test_type], name);
		fail_unless(got_host == (char *) -1, "host changed on parse failure %sfor '%s': got %p", test_string[test_type], name, got_host);
		fail_unless(got_display == -42, "display changed on parse failure %sfor '%s': got %d", test_string[test_type], name, got_display);
	}
	putenv("DISPLAY=");
}

START_TEST(parse_display_unix)
{
	parse_display_pass(":0", "", 0, 0);
	parse_display_pass(":1", "", 1, 0);
	parse_display_pass(":0.1", "", 0, 1);
}
END_TEST

START_TEST(parse_display_ip)
{
	parse_display_pass("x.org:0", "x.org", 0, 0);
	parse_display_pass("expo:0", "expo", 0, 0);
	parse_display_pass("bigmachine:1", "bigmachine", 1, 0);
	parse_display_pass("hydra:0.1", "hydra", 0, 1);
}
END_TEST

START_TEST(parse_display_ipv4)
{
	parse_display_pass("198.112.45.11:0", "198.112.45.11", 0, 0);
	parse_display_pass("198.112.45.11:0.1", "198.112.45.11", 0, 1);
}
END_TEST

START_TEST(parse_display_ipv6)
{
	parse_display_pass(":::0", "::", 0, 0);
	parse_display_pass("1:::0", "1::", 0, 0);
	parse_display_pass("::1:0", "::1", 0, 0);
	parse_display_pass("::1:0.1", "::1", 0, 1);
	parse_display_pass("::127.0.0.1:0", "::127.0.0.1", 0, 0);
	parse_display_pass("::ffff:127.0.0.1:0", "::ffff:127.0.0.1", 0, 0);
	parse_display_pass("2002:83fc:d052::1:0", "2002:83fc:d052::1", 0, 0);
	parse_display_pass("2002:83fc:d052::1:0.1", "2002:83fc:d052::1", 0, 1);
	parse_display_pass("[::]:0", "[::]", 0, 0);
	parse_display_pass("[1::]:0", "[1::]", 0, 0);
	parse_display_pass("[::1]:0", "[::1]", 0, 0);
	parse_display_pass("[::1]:0.1", "[::1]", 0, 1);
	parse_display_pass("[::127.0.0.1]:0", "[::127.0.0.1]", 0, 0);
	parse_display_pass("[::ffff:127.0.0.1]:0", "[::ffff:127.0.0.1]", 0, 0);
	parse_display_pass("[2002:83fc:d052::1]:0", "[2002:83fc:d052::1]", 0, 0);
	parse_display_pass("[2002:83fc:d052::1]:0.1", "[2002:83fc:d052::1]", 0, 1);
}
END_TEST

START_TEST(parse_display_decnet)
{
	parse_display_pass("myws::0", "myws:", 0, 0);
	parse_display_pass("big::1", "big:", 1, 0);
	parse_display_pass("hydra::0.1", "hydra:", 0, 1);
}
END_TEST

START_TEST(parse_display_negative)
{
	parse_display_fail(0);
	parse_display_fail("");
	parse_display_fail(":");
	parse_display_fail("::");
	parse_display_fail(":::");
	parse_display_fail(":.");
	parse_display_fail(":a");
	parse_display_fail(":a.");
	parse_display_fail(":0.");
	parse_display_fail(":.a");
	parse_display_fail(":.0");
	parse_display_fail(":0.a");
	parse_display_fail(":0.0.");

	parse_display_fail("127.0.0.1");
	parse_display_fail("127.0.0.1:");
	parse_display_fail("127.0.0.1::");
	parse_display_fail("::127.0.0.1");
	parse_display_fail("::127.0.0.1:");
	parse_display_fail("::127.0.0.1::");
	parse_display_fail("::ffff:127.0.0.1");
	parse_display_fail("::ffff:127.0.0.1:");
	parse_display_fail("::ffff:127.0.0.1::");
	parse_display_fail("localhost");
	parse_display_fail("localhost:");
	parse_display_fail("localhost::");
}
END_TEST

/* }}} */

static void popcount_eq(uint32_t bits, int count)
{
	fail_unless(xcb_popcount(bits) == count, "unexpected popcount(%08x) != %d", bits, count);
}

START_TEST(popcount)
{
	uint32_t mask;
	int count;

	for (mask = 0xffffffff, count = 32; count >= 0; mask >>= 1, --count) {
		popcount_eq(mask, count);
	}
	for (mask = 0x80000000; mask; mask >>= 1) {
		popcount_eq(mask, 1);
	}
	for (mask = 0x80000000; mask > 1; mask >>= 1) {
		popcount_eq(mask | 1, 2);
	}
}
END_TEST

Suite *public_suite(void)
{
	Suite *s = suite_create("Public API");
	putenv("DISPLAY=");
	suite_add_test(s, parse_display_unix, "xcb_parse_display unix");
	suite_add_test(s, parse_display_ip, "xcb_parse_display ip");
	suite_add_test(s, parse_display_ipv4, "xcb_parse_display ipv4");
	suite_add_test(s, parse_display_ipv6, "xcb_parse_display ipv6");
	suite_add_test(s, parse_display_decnet, "xcb_parse_display decnet");
	suite_add_test(s, parse_display_negative, "xcb_parse_display negative");
	suite_add_test(s, popcount, "xcb_popcount");
	return s;
}
