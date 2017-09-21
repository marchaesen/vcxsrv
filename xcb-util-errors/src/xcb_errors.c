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
#include "errors.h"
#include <stdlib.h>
#include <string.h>

#define CHECK_CONTEXT(ctx) do { \
	if ((ctx) == NULL) \
		return "xcb-errors API misuse: context argument is NULL"; \
} while (0)

struct extension_info_t {
	struct extension_info_t *next;
	const struct static_extension_info_t *static_info;
	uint8_t major_opcode;
	uint8_t first_event;
	uint8_t first_error;
};

struct xcb_errors_context_t {
	struct extension_info_t *extensions;
};

static const char *get_strings_entry(const char *strings, unsigned int index) {
	while (index-- > 0)
		strings += strlen(strings) + 1;
	return strings;
}

int register_extension(xcb_errors_context_t *ctx, xcb_connection_t *conn,
		xcb_query_extension_cookie_t cookie,
		const struct static_extension_info_t *static_info)
{
	struct extension_info_t *info;
	xcb_query_extension_reply_t *reply;

	info = calloc(1, sizeof(*info));
	reply = xcb_query_extension_reply(conn, cookie, NULL);

	if (!info || !reply || !reply->present) {
		int not_present = reply && !reply->present;
		free(info);
		free(reply);
		if (not_present)
			return 0;
		return -1;
	}

	info->static_info = static_info;
	info->major_opcode = reply->major_opcode;
	info->first_event = reply->first_event;
	info->first_error = reply->first_error;

	info->next = ctx->extensions;
	ctx->extensions = info;
	free(reply);

	return 0;
}

int xcb_errors_context_new(xcb_connection_t *conn, xcb_errors_context_t **c)
{
	xcb_errors_context_t *ctx = NULL;

	if ((*c = calloc(1, sizeof(*c))) == NULL)
		goto error_out;

	ctx = *c;
	ctx->extensions = NULL;

	if (register_extensions(ctx, conn) != 0)
		goto error_out;

	return 0;

error_out:
	xcb_errors_context_free(ctx);
	*c = NULL;
	return -1;
}

void xcb_errors_context_free(xcb_errors_context_t *ctx)
{
	struct extension_info_t *info;

	if (ctx == NULL)
		return;

	info = ctx->extensions;
	while (info) {
		struct extension_info_t *prev = info;
		info = info->next;
		free(prev);
	}

	free(ctx);
}

const char *xcb_errors_get_name_for_major_code(xcb_errors_context_t *ctx,
		uint8_t major_code)
{
	struct extension_info_t *info;

	CHECK_CONTEXT(ctx);

	info = ctx->extensions;
	while (info && info->major_opcode != major_code)
		info = info->next;

	if (info == NULL)
		return get_strings_entry(xproto_info.strings_minor, major_code);

	return info->static_info->name;
}

const char *xcb_errors_get_name_for_minor_code(xcb_errors_context_t *ctx,
		uint8_t major_code,
		uint16_t minor_code)
{
	struct extension_info_t *info;

	CHECK_CONTEXT(ctx);

	info = ctx->extensions;
	while (info && info->major_opcode != major_code)
		info = info->next;

	if (info == NULL || minor_code >= info->static_info->num_minor)
		return NULL;

	return get_strings_entry(info->static_info->strings_minor, minor_code);
}

const char *xcb_errors_get_name_for_xge_event(xcb_errors_context_t *ctx,
		uint8_t major_code, uint16_t event_type)
{
	struct extension_info_t *info;

	CHECK_CONTEXT(ctx);

	info = ctx->extensions;
	while (info && info->major_opcode != major_code)
		info = info->next;

	if (info == NULL || event_type >= info->static_info->num_xge_events)
		return NULL;

	return get_strings_entry(info->static_info->strings_xge_events, event_type);
}

const char *xcb_errors_get_name_for_core_event(xcb_errors_context_t *ctx,
		uint8_t event_code, const char **extension)
{
	struct extension_info_t *best = NULL;
	struct extension_info_t *next;

	event_code &= 0x7f;
	if (extension)
		*extension = NULL;

	CHECK_CONTEXT(ctx);

	/* Find the extension with the largest first_event <= event_code. Thanks
	 * to this we do the right thing if the server only supports an older
	 * version of some extension which had less events.
	 */
	next = ctx->extensions;
	while (next) {
		struct extension_info_t *current = next;
		next = next->next;

		if (current->first_event > event_code)
			continue;
		if (best != NULL && best->first_event > current->first_event)
			continue;
		best = current;
	}

	if (best == NULL || best->first_event == 0
			|| event_code - best->first_event >= best->static_info->num_events) {
		/* Nothing found */
		return get_strings_entry(xproto_info.strings_events, event_code);
	}

	if (extension)
		*extension = best->static_info->name;
	return get_strings_entry(best->static_info->strings_events, event_code - best->first_event);
}

const char *xcb_errors_get_name_for_error(xcb_errors_context_t *ctx,
		uint8_t error_code, const char **extension)
{
	struct extension_info_t *best = NULL;
	struct extension_info_t *next;

	if (extension)
		*extension = NULL;

	CHECK_CONTEXT(ctx);

	/* Find the extension with the largest first_error <= error_code. Thanks
	 * to this we do the right thing if the server only supports an older
	 * version of some extension which had less events.
	 */
	next = ctx->extensions;
	while (next) {
		struct extension_info_t *current = next;
		next = next->next;

		if (current->first_error > error_code)
			continue;
		if (best != NULL && best->first_error > current->first_error)
			continue;
		best = current;
	}

	if (best == NULL || best->first_error == 0
			|| error_code - best->first_error >= best->static_info->num_errors) {
		/* Nothing found */
		return get_strings_entry(xproto_info.strings_errors, error_code);
	}

	if (extension)
		*extension = best->static_info->name;
	return get_strings_entry(best->static_info->strings_errors, error_code - best->first_error);
}

const char *xcb_errors_get_name_for_xcb_event(xcb_errors_context_t *ctx,
		xcb_generic_event_t *event, const char **extension)
{
	struct extension_info_t *xkb;
	uint8_t response_type;

	if (extension)
		*extension = NULL;

	CHECK_CONTEXT(ctx);

	/* Find the xkb extension, if present */
	xkb = ctx->extensions;
	while (xkb != NULL && strcmp(xkb->static_info->name, "xkb") != 0)
		xkb = xkb->next;

	response_type = event->response_type & 0x7f;
	if (response_type == XCB_GE_GENERIC) {
		/* XGE offers extension's major code and event sub-type. */
		xcb_ge_generic_event_t *ge = (void *) event;
		if (extension)
			*extension = xcb_errors_get_name_for_major_code(ctx,
					ge->extension);
		return xcb_errors_get_name_for_xge_event(ctx,
				ge->extension, ge->event_type);
	}
	if (xkb != NULL && xkb->first_event != 0
			&& response_type == xkb->first_event) {
		/* There is no nice struct that defines the common fields for
		 * XKB events, but the event type is always in the second byte.
		 * In xcb_generic_event_t, this is the pad0 field.
		 */
		if (extension)
			*extension = xkb->static_info->name;
		return xcb_errors_get_name_for_xge_event(ctx,
				xkb->major_opcode, event->pad0);
	}
	/* Generic case, decide only based on the response_type. */
	return xcb_errors_get_name_for_core_event(ctx, response_type, extension);
}
