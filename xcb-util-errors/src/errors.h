#ifndef __ERRORS_H__
#define __ERRORS_H__

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

struct static_extension_info_t {
	uint16_t num_minor;
	uint16_t num_xge_events;
	uint8_t num_events;
	uint8_t num_errors;
	const char *strings_minor;
	const char *strings_xge_events;
	const char *strings_events;
	const char *strings_errors;
	const char *name;
};

extern const struct static_extension_info_t xproto_info;

int register_extensions(xcb_errors_context_t *ctx, xcb_connection_t *conn);
int register_extension(xcb_errors_context_t *ctx, xcb_connection_t *conn,
		xcb_query_extension_cookie_t cookie,
		const struct static_extension_info_t *static_info);

#endif /* __ERRORS_H__ */
