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

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "screenshot_params.h"

#include "util/os_socket.h"

enum LogType LOG_TYPE = REQUIRED;

static const char *print_log_type(enum LogType log_type) {
   switch(log_type)
   {
      case(DEBUG):
         return "DEBUG";
      case(ERROR):
         return "ERROR";
      case(INFO):
         return "INFO";
      case(NO_PREFIX):
         return "NO_PREFIX";
      case(REQUIRED):
         return "REQUIRED";
      case(WARN):
         return "WARN";
      default:
         /* Don't show log type*/
         return "";
   }
}

void LOG(enum LogType log_type, const char *format, ...) {
   FILE *file_type;
   va_list args;
   if (log_type == WARN || log_type == ERROR) {
      file_type = stderr;
   } else {
      file_type = stdout;
   }
   if (log_type == DEBUG && LOG_TYPE != DEBUG) {
      return;
   } else if (log_type == INFO && (LOG_TYPE != INFO && LOG_TYPE != DEBUG)) {
      return;
   }
   if (log_type != NO_PREFIX)
      fprintf(file_type, "mesa-screenshot: %s: ", print_log_type(log_type));
   va_start(args, format);
   vfprintf(file_type, format, args);
   va_end(args);
}

static const char *
parse_control(const char *str)
{
   static char control_str[64];
   if (strlen(str) > 63) {
      LOG(ERROR, "control string too long. Must be < 64 chars\n");
      return NULL;
   }
   strcpy(control_str, str);

   return control_str;
}

/* Inserts frame nodes in ascending order */
static void insert_frame(struct frame_list *list, uint32_t new_frame_num)
{
   struct frame_node *new_node, *curr, *next;
   new_node = (struct frame_node*)malloc(sizeof(struct frame_node));
   new_node->frame_num = new_frame_num;
   new_node->next = NULL;
   curr = list->head;

   /* Empty list */
   if (list->head == NULL)
      list->head = new_node;
   /* Insert as new head of list */
   else if (list->head->frame_num > new_frame_num) {
      list->head = new_node;
      new_node->next = curr;
   /* Traverse list & insert frame number in correct, ascending location */
   } else {
      while (curr != NULL) {
         if (curr->frame_num == new_frame_num) {
            free(new_node);
            return; // Avoid inserting duplicates
         }
         next = curr->next;
         if (next) {
            if (next->frame_num > new_frame_num) {
               curr->next = new_node;
               new_node->next = next;
               break;
            }
         } else {
            curr->next = new_node;
            break;
         }
         curr = curr->next;
      }
   }
   list->size++;
}

void remove_node(struct frame_list *list,
                 struct frame_node *prev,
                 struct frame_node *node) {
   if (node) {
      if (prev)
         prev->next = node->next;
      else {
         list->head = node->next;
      }
      free(node);
      list->size--;
   } else
      LOG(ERROR, "Encountered null node while removing from frame list\n");
}

void destroy_frame_list(struct frame_list *list)
{
   struct frame_node *curr, *prev;
   if (!list || !list->head)
      return;
   else {
      curr = list->head;
      while (curr != NULL) {
         prev = curr;
         curr = curr->next;
         free(prev);
      }
   }
}

static unsigned
parse_unsigned(const char *str)
{
   return strtol(str, NULL, 0);
}

static bool is_frame_delimiter(char c)
{
   return c == 0 ||  c == '/' || c == '-';
}

static struct frame_list *
parse_frames(const char *str)
{
   int32_t range_start;
   uint32_t range_counter, range_interval, range_end;
   range_start = -1;
   range_counter = 0;
   uint32_t range_delimit_count = 0;
   range_interval = 1;
   char *prev_delim = NULL;
   char str_buf[STANDARD_BUFFER_SIZE] = {0};
   char *str_buf_ptr;
   str_buf_ptr = str_buf;
   struct frame_list *list = (struct frame_list*)malloc(sizeof(struct frame_list));
   list->size = 0;
   list->all_frames = false;

   if (!strcmp(str, "all")) {
      /* Don't bother counting, we want all frames */
      list->all_frames = true;
   } else {
      while (*str != 0) { // Still string left to parse
         for (; !is_frame_delimiter(*str); str++, str_buf_ptr++) {
            if (!isdigit(*str))
            {
               LOG(ERROR, "mesa-screenshot: syntax error: unexpected non-digit "
                          "'%c' while parsing the frame numbers\n", *str);
               destroy_frame_list(list);
               return NULL;
            }
            *str_buf_ptr = *str;
         }
         if (strlen(str_buf) == 0) {
            LOG(ERROR, "mesa-screenshot: syntax error: empty string given in frame range\n");
            return NULL;
         } else if (strlen(str_buf) > 0 && *str == '/') {
            if (prev_delim && *prev_delim == '-') {
               LOG(ERROR, "mesa-screenshot: syntax error: detected invalid individual " \
                          "frame selection (/) after range selection (-)\n");
               return NULL;
            }
            LOG(DEBUG, "Adding frame: %u\n", parse_unsigned(str_buf));
            insert_frame(list, parse_unsigned(str_buf));
         } else if (strlen(str_buf) > 0 && (*str == '-' || *str == 0 )) {
            if (range_delimit_count < 1) {
               LOG(DEBUG, "Range start set\n");
               range_start = parse_unsigned(str_buf);
               range_delimit_count++;
            } else if(range_delimit_count < 2) {
               LOG(DEBUG, "Range counter set\n");
               range_counter = parse_unsigned(str_buf);
               range_delimit_count++;
            } else {
               LOG(DEBUG, "Range interval set\n");
               range_interval = parse_unsigned(str_buf);
               break;
            }
            if (*str == 0) {
               break;
            }
            prev_delim = (char *)str;
         }
         str++;
         /* Reset buffer for next set of numbers */
         memset(str_buf, '\0', sizeof(str_buf));
         str_buf_ptr = str_buf;
      }
      range_end = range_start + (range_counter * range_interval);
      if (range_start >= 0) {
         int i = range_start;
         do {
            insert_frame(list, i);
            i += range_interval;
         } while (i < range_end);
      }
   }
   LOG(INFO, "frame range: ");
   if (list->all_frames) {
      LOG(NO_PREFIX, "all");
   } else {
      for (struct frame_node *iter = list->head; iter != NULL; iter = iter->next) {
         LOG(NO_PREFIX, "%u", iter->frame_num);
         if(iter->next) {
            LOG(NO_PREFIX, ", ");
         }
      }
   }
   LOG(NO_PREFIX, "\n");
   return list;
}

struct ImageRegion getRegionFromInput(const char *str) {
   struct ImageRegion region;
   region.useImageRegion = false;
   region.startX = 0;
   region.startY = 0;
   region.endX = 1;
   region.endY = 1;
   /* Expected form is a tuple of four float entries, representing a percentage,
      so need to attempt to convert the values to floating point type and ensure
      the values are in the range 0.00 <= x <= 1.00.

      An example of proper input would be:
      "0.20/0.20/0.75/0.60"
   */
   if (strlen(str) == 0) {
      LOG(ERROR, "Region input was empty!\n");
      return region;
   }
   errno = 0;
   float dimensions[] = {0, 0, 1, 1};
   char *dup = strdup(str);
   char *token = strtok(dup, "/");
   char *endptr;
   int i;
   for (i = 0; i < 4; i++, token = strtok(NULL, "/")) {
      if (!token) {
         LOG(ERROR, "Four region entries were not detected!\n");
         break;
      }
      dimensions[i] = strtof(token, &endptr);
      if (errno || endptr == token) {
         LOG(ERROR, "Found non-float in region description: %s\n", token, errno);
         break;
      }
      if (dimensions[i] < 0 || 1 < dimensions[i] ) {
         LOG(ERROR, "Found invalid region value, region value must be between 0 and 1: %f\n", dimensions[i]);
         break;
      }
   }
   if (i == 4) {
      if (dimensions[0] < dimensions[2] && dimensions[1] < dimensions[3]) {
         region.startX = dimensions[0];
         region.startY = dimensions[1];
         region.endX = dimensions[2];
         region.endY = dimensions[3];
         region.useImageRegion = true;
      } else {
         LOG(ERROR, "Region end values need to be greater than region start values!\n");
      }
   }
   free(dup);
   return region;
}

static struct ImageRegion parse_region(const char *str)
{
   return getRegionFromInput(str);
}

static bool
parse_help(const char *str)
{
   LOG(NO_PREFIX, "Layer params using VK_LAYER_MESA_SCREENSHOT_CONFIG=\n");
#define SCREENSHOT_PARAM_BOOL(name)                \
   LOG(NO_PREFIX, "\t%s=0|1\n", #name);
#define SCREENSHOT_PARAM_CUSTOM(name)
   SCREENSHOT_PARAMS
#undef SCREENSHOT_PARAM_BOOL
#undef SCREENSHOT_PARAM_CUSTOM
   LOG(NO_PREFIX, "\tlog_type=info|debug (if no selection, no logs besides errors are given)\n");
   LOG(NO_PREFIX, "\toutput_dir='/path/to/dir'\n");
   LOG(NO_PREFIX, "\tframes=Individual frames, separated by '/', followed by " \
                  "a range setup, separated by '-', <range start>-<range count>-<range interval>\n" \
                  "\tFor example '1/5/7/15-4-5' = [1,5,7,15,20,25,30]\n" \
                  "\tframes='all' will select all frames.");

   return true;
}

static enum LogType
parse_log_type(const char *str)
{
   if(!strcmp(str, "info")) {
      return INFO;
   } else if (!strcmp(str, "debug")) {
      return DEBUG;
   } else {
      /* Required logs only */
      return REQUIRED;
   }
}

/* TODO: Improve detection of proper directory path */
static const char *
parse_output_dir(const char *str)
{
   static char output_dir[LARGE_BUFFER_SIZE];
   strcpy(output_dir, str);
   uint32_t last_char_index = strlen(str)-1;
   // Ensure we're in bounds and the last character is '/'
   if (last_char_index > 0 &&
       str[last_char_index] != '/' &&
       last_char_index < LARGE_BUFFER_SIZE-1) {
      output_dir[last_char_index+1] = '/';
   }
   DIR *dir = opendir(output_dir);
   assert(dir);
   closedir(dir);

   return output_dir;
}

static bool is_delimiter(char c)
{
   return c == 0 || c == ',' || c == ':' || c == ';' || c == '=';
}

static int
parse_string(const char *s, char *out_param, char *out_value)
{
   int i = 0;

   for (; !is_delimiter(*s); s++, out_param++, i++)
      *out_param = *s;

   *out_param = 0;

   if (*s == '=') {
      s++;
      i++;
      for (; !is_delimiter(*s); s++, out_value++, i++)
         *out_value = *s;
   } else
      *(out_value++) = '1';
   *out_value = 0;

   if (*s && is_delimiter(*s)) {
      s++;
      i++;
   }

   if (*s && !i) {
      LOG(ERROR, "mesa-screenshot: syntax error: unexpected '%c' (%i) while "
                 "parsing a string\n", *s, *s);
   }
   return i;
}

const char *screenshot_param_names[] = {
#define SCREENSHOT_PARAM_BOOL(name) #name,
#define SCREENSHOT_PARAM_CUSTOM(name)
   SCREENSHOT_PARAMS
#undef SCREENSHOT_PARAM_BOOL
#undef SCREENSHOT_PARAM_CUSTOM
};

void
parse_screenshot_env(struct screenshot_params *params,
                  const char *env)
{

   if (!env)
      return;

   uint32_t num;
   const char *itr = env;
   char key[STANDARD_BUFFER_SIZE], value[LARGE_BUFFER_SIZE];

   memset(params, 0, sizeof(*params));

   params->control    = "mesa_screenshot";
   params->frames     = NULL;
   params->output_dir = NULL;
   params->region.useImageRegion = false;

   /* Loop once first until log options found (if they exist) */
   while ((num = parse_string(itr, key, value)) != 0) {
      itr += num;
      if (!strcmp("log_type", key)) {
         LOG_TYPE = parse_log_type(value);
         break;
      }
   }
   /* Reset the iterator */
   itr = env;

   while ((num = parse_string(itr, key, value)) != 0) {
      itr += num;
      if (!strcmp("log_type", key)) {
         /* Skip if matched again*/
         continue;
      }
#define SCREENSHOT_PARAM_BOOL(name)                                        \
      if (!strcmp(#name, key)) {                                           \
         params->enabled[SCREENSHOT_PARAM_ENABLED_##name] =                \
            strtol(value, NULL, 0);                                        \
         continue;                                                         \
      }
#define SCREENSHOT_PARAM_CUSTOM(name)              \
      if (!strcmp(#name, key)) {                   \
         params->name = parse_##name(value);       \
         continue;                                 \
      }
      SCREENSHOT_PARAMS
#undef SCREENSHOT_PARAM_BOOL
#undef SCREENSHOT_PARAM_CUSTOM
      LOG(ERROR, "Unknown option '%s'\n", key);
   }
}
