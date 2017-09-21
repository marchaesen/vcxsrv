/* Copyright © 2015 Uli Schlachter
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors or their
 * institutions shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization from the authors.
 */

#include "xcb_errors.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && ((__GNUC__ * 100 + __GNUC_MINOR__) >= 203)
#define ATTRIBUTE_PRINTF(x,y) __attribute__((__format__(__printf__,x,y)))
#else
#define ATTRIBUTE_PRINTF(x,y)
#endif

#define SKIP 77

static int check_strings(const char *expected, const char *actual,
		const char *format, ...) ATTRIBUTE_PRINTF(3, 4);

static int check_strings(const char *expected, const char *actual,
		const char *format, ...)
{
	va_list ap;

	if (expected == NULL && actual == NULL)
		return 0;
	if (expected != NULL && actual != NULL && strcmp(expected, actual) == 0)
		return 0;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return 1;
}

static int check_request(xcb_errors_context_t *ctx, uint8_t opcode, const char *expected)
{
	const char *actual = xcb_errors_get_name_for_major_code(ctx, opcode);
	return check_strings(expected, actual, "For opcode %d: Expected %s, got %s\n",
			opcode, expected, actual);
}

static int check_error(xcb_errors_context_t *ctx, uint8_t error,
		const char *expected, const char *expected_extension)
{
	const char *actual, *actual_extension, *tmp;
	int ret = 0;

	actual = xcb_errors_get_name_for_error(ctx, error, &actual_extension);
	ret |= check_strings(expected_extension, actual_extension,
			"For error %d: Expected ext %s, got %s\n",
			error, expected_extension, actual_extension);
	ret |= check_strings(expected, actual,
			"For error %d: Expected %s, got %s\n",
			error, expected, actual);

	tmp = xcb_errors_get_name_for_error(ctx, error, NULL);
	ret |= check_strings(actual, tmp,
			"For error %d: Passing NULL made a difference: %s vs %s\n",
			error, actual, tmp);
	return ret;
}

static int check_event(xcb_errors_context_t *ctx, uint8_t event,
		const char *expected, const char *expected_extension)
{
	const char *actual, *actual_extension, *tmp;
	int ret = 0;

	actual = xcb_errors_get_name_for_core_event(ctx, event, &actual_extension);
	ret |= check_strings(expected_extension, actual_extension,
			"For event %d: Expected ext %s, got %s\n",
			event, expected_extension, actual_extension);
	ret |= check_strings(expected, actual,
			"For event %d: Expected %s, got %s\n",
			event, expected, actual);

	tmp = xcb_errors_get_name_for_core_event(ctx, event, NULL);
	ret |= check_strings(actual, tmp,
			"For event %d: Passing NULL made a difference: %s vs %s\n",
			event, actual, tmp);

	tmp = xcb_errors_get_name_for_core_event(ctx, event | 0x80, NULL);
	ret |= check_strings(expected, tmp,
			"For event %d|0x80: Expected %s, got %s\n",
			event, expected, tmp);

	/* The wire_event we construct isn't a proper GE event */
	if (event != XCB_GE_GENERIC) {
		xcb_generic_event_t wire_event = {
			.response_type = event
		};

		actual = xcb_errors_get_name_for_xcb_event(ctx, &wire_event, &actual_extension);
		ret |= check_strings(expected_extension, actual_extension,
				"For xcb wire event %d: Expected ext %s, got %s\n",
				event, expected_extension, actual_extension);
		ret |= check_strings(expected, actual,
				"For xcb wire event %d: Expected %s, got %s\n",
				event, expected, actual);

		tmp = xcb_errors_get_name_for_xcb_event(ctx, &wire_event, NULL);
		ret |= check_strings(actual, tmp,
				"For xcb wire event %d: Passing NULL made a difference: %s vs %s\n",
				event, actual, tmp);

		wire_event.response_type |= 0x80;
		tmp = xcb_errors_get_name_for_xcb_event(ctx, &wire_event, NULL);
		ret |= check_strings(expected, tmp,
				"For xcb wire event %d|0x80: Expected %s, got %s\n",
				event, expected, tmp);
	}
	return ret;
}

static int check_xge_event(xcb_errors_context_t *ctx, uint8_t major_code,
		uint16_t event_type, const char *expected, const char *expected_extension)
{
	xcb_ge_generic_event_t wire_event = {
		.response_type = XCB_GE_GENERIC,
		.extension = major_code,
		.event_type = event_type
	};
	const char *actual, *actual_extension, *tmp;
	int ret = 0;

	actual = xcb_errors_get_name_for_xge_event(ctx, major_code, event_type);
	ret |= check_strings(expected, actual,
			"For xge event (%d, %d): Expected %s, got %s\n",
			major_code, event_type, expected, actual);

	actual = xcb_errors_get_name_for_xcb_event(ctx, (void *) &wire_event, &actual_extension);
	ret |= check_strings(expected_extension, actual_extension,
			"For xcb xge wire event %d: Expected ext %s, got %s\n",
			event_type, expected_extension, actual_extension);
	ret |= check_strings(expected, actual,
			"For xcb xge wire event %d: Expected %s, got %s\n",
			event_type, expected, actual);

	tmp = xcb_errors_get_name_for_xcb_event(ctx, (void *) &wire_event, NULL);
	ret |= check_strings(actual, tmp,
			"For xcb xge wire event %d: Passing NULL made a difference: %s vs %s\n",
			event_type, actual, tmp);
	return ret;
}

static int check_xkb_event(xcb_errors_context_t *ctx, uint8_t major_code, uint8_t first_event,
		uint16_t event_type, const char *expected)
{
	xcb_generic_event_t wire_event = {
		.response_type = first_event,
		.pad0 = event_type
	};
	const char *actual, *actual_extension, *tmp;
	int ret = 0;

	actual = xcb_errors_get_name_for_xge_event(ctx, major_code, event_type);
	ret |= check_strings(expected, actual,
			"For xkb event (%d, %d): Expected %s, got %s\n",
			major_code, event_type, expected, actual);

	actual = xcb_errors_get_name_for_xcb_event(ctx, &wire_event, &actual_extension);
	ret |= check_strings("xkb", actual_extension,
			"For xcb xkb wire event %d: Expected ext xkb, got %s\n",
			event_type, actual_extension);
	ret |= check_strings(expected, actual,
			"For xcb xkb wire event %d: Expected %s, got %s\n",
			event_type, expected, actual);

	tmp = xcb_errors_get_name_for_xcb_event(ctx, &wire_event, NULL);
	ret |= check_strings(actual, tmp,
			"For xcb xkb wire event %d: Passing NULL made a difference: %s vs %s\n",
			event_type, actual, tmp);
	return ret;
}

static int check_minor(xcb_errors_context_t *ctx, uint8_t major, uint16_t minor, const char *expected)
{
	const char *actual = xcb_errors_get_name_for_minor_code(ctx, major, minor);
	return check_strings(expected, actual, "For minor (%d, %d): Expected %s, got %s\n",
			major, minor, expected, actual);
}

static int test_error_connection(void)
{
	xcb_errors_context_t *ctx;
	xcb_connection_t *c;
	int err = 0;

	c = xcb_connect("does-not-exist", NULL);
	if (!xcb_connection_has_error(c)) {
		fprintf(stderr, "Failed to create an error connection\n");
		err = 1;
	}

	if (xcb_errors_context_new(c, &ctx) == 0) {
		fprintf(stderr, "Successfully created context for error connection\n");
		err = 1;
	}

	xcb_errors_context_free(ctx);
	xcb_disconnect(c);

	return err;
}

static int test_randr(xcb_connection_t *c, xcb_errors_context_t *ctx)
{
	struct xcb_query_extension_reply_t *reply;
	int err = 0;

	reply = xcb_query_extension_reply(c,
			xcb_query_extension(c, strlen("RANDR"), "RANDR"), NULL);
	if (!reply || !reply->present) {
		fprintf(stderr, "RANDR not supported by display\n");
		free(reply);
		return SKIP;
	}

	err |= check_request(ctx, reply->major_opcode, "RandR");
	err |= check_error(ctx, reply->first_error + 0, "BadOutput", "RandR");
	err |= check_error(ctx, reply->first_error + 3, "BadProvider", "RandR");
	err |= check_event(ctx, reply->first_event + 0, "ScreenChangeNotify", "RandR");
	err |= check_event(ctx, reply->first_event + 1, "Notify", "RandR");
	err |= check_minor(ctx, reply->major_opcode, 0, "QueryVersion");
	err |= check_minor(ctx, reply->major_opcode, 1, "Unknown (1)");
	err |= check_minor(ctx, reply->major_opcode, 33, "GetProviderInfo");
	err |= check_minor(ctx, reply->major_opcode, 41, "GetProviderProperty");
	err |= check_minor(ctx, reply->major_opcode, 1337, NULL);
	err |= check_minor(ctx, reply->major_opcode, 0xffff, NULL);

	free(reply);
	return err;
}

static int test_xfixes(xcb_connection_t *c, xcb_errors_context_t *ctx)
{
	struct xcb_query_extension_reply_t *reply;
	int err = 0;

	reply = xcb_query_extension_reply(c,
			xcb_query_extension(c, strlen("XFIXES"), "XFIXES"), NULL);
	if (!reply || !reply->present) {
		fprintf(stderr, "XFIXES not supported by display\n");
		free(reply);
		return SKIP;
	}

	err |= check_request(ctx, reply->major_opcode, "XFixes");
	err |= check_error(ctx, reply->first_error + 0, "BadRegion", "XFixes");
	err |= check_event(ctx, reply->first_event + 0, "SelectionNotify", "XFixes");
	err |= check_event(ctx, reply->first_event + 1, "CursorNotify", "XFixes");
	err |= check_minor(ctx, reply->major_opcode, 0, "QueryVersion");
	err |= check_minor(ctx, reply->major_opcode, 32, "DeletePointerBarrier");
	err |= check_minor(ctx, reply->major_opcode, 1337, NULL);
	err |= check_minor(ctx, reply->major_opcode, 0xffff, NULL);

	free(reply);
	return err;
}

static int test_xinput(xcb_connection_t *c, xcb_errors_context_t *ctx)
{
	struct xcb_query_extension_reply_t *reply;
	int err = 0;

	reply = xcb_query_extension_reply(c,
			xcb_query_extension(c, strlen("XInputExtension"), "XInputExtension"), NULL);
	if (!reply || !reply->present) {
		fprintf(stderr, "XInputExtension not supported by display\n");
		free(reply);
		return SKIP;
	}

	err |= check_request(ctx, reply->major_opcode, "Input");
	err |= check_error(ctx, reply->first_error + 0, "Device", "Input");
	err |= check_error(ctx, reply->first_error + 4, "Class", "Input");
	err |= check_event(ctx, reply->first_event + 0, "DeviceValuator", "Input");
	err |= check_event(ctx, reply->first_event + 16, "DevicePropertyNotify", "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 0, "Unknown (0)", "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 1, "DeviceChanged", "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 26, "BarrierLeave", "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 27, NULL, "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 1337, NULL, "Input");
	err |= check_xge_event(ctx, reply->major_opcode, 0xffff, NULL, "Input");
	err |= check_minor(ctx, reply->major_opcode, 0, "Unknown (0)");
	err |= check_minor(ctx, reply->major_opcode, 1, "GetExtensionVersion");
	err |= check_minor(ctx, reply->major_opcode, 47, "XIQueryVersion");
	err |= check_minor(ctx, reply->major_opcode, 61, "XIBarrierReleasePointer");
	err |= check_minor(ctx, reply->major_opcode, 62, NULL);
	err |= check_minor(ctx, reply->major_opcode, 1337, NULL);
	err |= check_minor(ctx, reply->major_opcode, 0xffff, NULL);

	free(reply);
	return err;
}

static int test_xkb(xcb_connection_t *c, xcb_errors_context_t *ctx)
{
	struct xcb_query_extension_reply_t *reply;
	int err = 0;

	reply = xcb_query_extension_reply(c,
			xcb_query_extension(c, strlen("XKEYBOARD"), "XKEYBOARD"), NULL);
	if (!reply || !reply->present) {
		fprintf(stderr, "XKB not supported by display\n");
		free(reply);
		return SKIP;
	}

	err |= check_request(ctx, reply->major_opcode, "xkb");
	err |= check_error(ctx, reply->first_error + 0, "Keyboard", "xkb");
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 0, "NewKeyboardNotify");
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 1, "MapNotify");
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 11, "ExtensionDeviceNotify");
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 12, NULL);
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 1337, NULL);
	err |= check_xkb_event(ctx, reply->major_opcode, reply->first_event, 0xffff, NULL);

	free(reply);
	return err;
}

static int test_valid_connection(void)
{
	xcb_errors_context_t *ctx;
	xcb_connection_t *c;
	int err = 0;

	c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) {
		fprintf(stderr, "Failed to connect to X11 server (%d)\n",
				xcb_connection_has_error(c));
		return SKIP;
	}
	if (xcb_errors_context_new(c, &ctx) < 0) {
		fprintf(stderr, "Failed to initialize util-errors\n");
		xcb_disconnect(c);
		return 1;
	}

	err |= check_request(ctx, XCB_CREATE_WINDOW, "CreateWindow");
	err |= check_request(ctx, XCB_NO_OPERATION, "NoOperation");
	err |= check_request(ctx, 126, "Unknown (126)");
	err |= check_request(ctx, 0xff, "Unknown (255)");
	err |= check_minor(ctx, XCB_CREATE_WINDOW, 0, NULL);
	err |= check_minor(ctx, XCB_CREATE_WINDOW, 42, NULL);
	err |= check_minor(ctx, XCB_CREATE_WINDOW, 0xffff, NULL);

	err |= check_error(ctx, XCB_REQUEST, "Request", NULL);
	err |= check_error(ctx, XCB_IMPLEMENTATION, "Implementation", NULL);
	err |= check_error(ctx, 18, "Unknown (18)", NULL);
	err |= check_error(ctx, 127, "Unknown (127)", NULL);
	err |= check_error(ctx, 0xff, "Unknown (255)", NULL);

	err |= check_event(ctx, XCB_KEY_PRESS, "KeyPress", NULL);
	err |= check_event(ctx, XCB_KEY_RELEASE, "KeyRelease", NULL);
	err |= check_event(ctx, XCB_GE_GENERIC, "GeGeneric", NULL);
	err |= check_event(ctx, 36, "Unknown (36)", NULL);
	err |= check_event(ctx, 127, "Unknown (127)", NULL);

	err |= test_randr(c, ctx);
	err |= test_xinput(c, ctx);
	err |= test_xkb(c, ctx);
	err |= test_xfixes(c, ctx);

	xcb_errors_context_free(ctx);
	xcb_disconnect(c);
	return err;
}

static int test_NULL_context(void)
{
	int err = 0;
	const char *msg = "xcb-errors API misuse: context argument is NULL";

	xcb_errors_context_free(NULL);
	err |= check_strings(msg, xcb_errors_get_name_for_major_code(NULL, 0),
			"xcb_errors_get_name_for_major_code(NULL, 0) does not behave correctly");
	err |= check_strings(msg, xcb_errors_get_name_for_minor_code(NULL, 0, 0),
			"xcb_errors_get_name_for_minor_code(NULL, 0, 0) does not behave correctly");
	err |= check_strings(msg, xcb_errors_get_name_for_core_event(NULL, 0, NULL),
			"xcb_errors_get_name_for_core_event(NULL, 0, NULL) does not behave correctly");
	err |= check_strings(msg, xcb_errors_get_name_for_xge_event(NULL, 0, 0),
			"xcb_errors_get_name_for_xge_event(NULL, 0, 0) does not behave correctly");
	err |= check_strings(msg, xcb_errors_get_name_for_xcb_event(NULL, NULL, NULL),
			"xcb_errors_get_name_for_xcb_event(NULL, NULL, NULL) does not behave correctly");
	err |= check_strings(msg, xcb_errors_get_name_for_error(NULL, 0, NULL),
			"xcb_errors_get_name_for_xcb_error(NULL, 0, NULL) does not behave correctly");

	return err;
}

int main(void)
{
	int err = 0;
	err |= test_error_connection();
	err |= test_valid_connection();
	err |= test_NULL_context();
	return err;
}
