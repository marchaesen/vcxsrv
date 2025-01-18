/*
 * Copyright © 2024 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef SCREENSHOT_PARAMS_H
#define SCREENSHOT_PARAMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define SCREENSHOT_PARAMS                               \
   SCREENSHOT_PARAM_BOOL(comms)                         \
   SCREENSHOT_PARAM_CUSTOM(control)                     \
   SCREENSHOT_PARAM_CUSTOM(frames)                      \
   SCREENSHOT_PARAM_CUSTOM(log_type)                    \
   SCREENSHOT_PARAM_CUSTOM(output_dir)                  \
   SCREENSHOT_PARAM_CUSTOM(region)                      \
   SCREENSHOT_PARAM_CUSTOM(help)

#define LARGE_BUFFER_SIZE 16384  // 16 KB for large input strings
#define STANDARD_BUFFER_SIZE 256

enum screenshot_param_enabled {
#define SCREENSHOT_PARAM_BOOL(name) SCREENSHOT_PARAM_ENABLED_##name,
#define SCREENSHOT_PARAM_CUSTOM(name)
   SCREENSHOT_PARAMS
#undef SCREENSHOT_PARAM_BOOL
#undef SCREENSHOT_PARAM_CUSTOM
   SCREENSHOT_PARAM_ENABLED_MAX
};

enum LogType {
   DEBUG,
   ERROR,
   INFO,
   NO_PREFIX, // Don't prefix the log with text
   REQUIRED, // Non-error logs that must be printed for user
   WARN
};

extern enum LogType LOG_TYPE;

struct frame_node {
   uint32_t frame_num;
   struct frame_node *next;
};

/* List should be sorted into ascending order, in terms of frame_node data */
struct frame_list {
   uint32_t size;
   bool all_frames;
   struct frame_node *head;
};

/*
   Regions use percentages of an image to create a subimage
   Ex: startX% = 0.25, startY% = 0.25, endX% = 0.60, endY% = 0.75
   Original image size: width = 1920, height = 1080
   startX = 480, startY = 270, endX = 1152, endY = 810
   Resulting coordinates: ((480, 270), (1152, 270), (480, 810), (1152, 810))
*/
struct ImageRegion {
   float startX, startY, endX, endY;
   bool useImageRegion;
};

void remove_node(struct frame_list *, struct frame_node *, struct frame_node *);
void destroy_frame_list(struct frame_list *);
struct ImageRegion getRegionFromInput(const char *);

void LOG(enum LogType, const char *, ...);

struct screenshot_params {
   bool enabled[SCREENSHOT_PARAM_ENABLED_MAX];
   struct frame_list *frames;
   struct ImageRegion region;
   const char *control;
   enum LogType log_type;
   const char *output_dir;
   bool help;
};

const extern char *screenshot_param_names[];

void parse_screenshot_env(struct screenshot_params *params,
                       const char *env);

#ifdef __cplusplus
}
#endif

#endif /* SCREENSHOT_PARAMS_H */
