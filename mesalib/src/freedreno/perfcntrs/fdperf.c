/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * All Rights Reserved.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <inttypes.h>
#include <libconfig.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>

#include "drm/freedreno_drmif.h"
#include "drm/freedreno_ringbuffer.h"

#include "util/os_file.h"

#include "freedreno_dt.h"
#include "freedreno_perfcntr.h"

#define MAX_CNTR_PER_GROUP 24
#define REFRESH_MS         500

static struct {
   int refresh_ms;
   bool dump;
} options = {
   .refresh_ms = REFRESH_MS,
   .dump = false,
};

/* NOTE first counter group should always be CP, since we unconditionally
 * use CP counter to measure the gpu freq.
 */

struct counter_group {
   const struct fd_perfcntr_group *group;

   struct {
      const struct fd_perfcntr_counter *counter;
      uint16_t select_val;
      bool is_gpufreq_counter;
   } counter[MAX_CNTR_PER_GROUP];

   /* name of currently selected counters (for UI): */
   const char *label[MAX_CNTR_PER_GROUP];

   uint64_t value[MAX_CNTR_PER_GROUP];
   uint64_t value_delta[MAX_CNTR_PER_GROUP];

   uint64_t sample_time[MAX_CNTR_PER_GROUP];
   uint64_t sample_time_delta[MAX_CNTR_PER_GROUP];
};

static struct {
   void *io;
   uint32_t min_freq;
   uint32_t max_freq;
   /* per-generation table of counters: */
   unsigned ngroups;
   struct counter_group *groups;
   /* drm device (for writing select regs via ring): */
   struct fd_device *dev;
   struct fd_pipe *pipe;
   const struct fd_dev_id *dev_id;
   struct fd_submit *submit;
   struct fd_ringbuffer *ring;
} dev;

static void config_save(void);
static void config_restore(void);
static void restore_counter_groups(void);

/*
 * helpers
 */

static uint64_t
gettime_us(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
}

static void
sleep_us(uint32_t us)
{
   const struct timespec ts = {
      .tv_sec = us / 1000000,
      .tv_nsec = (us % 1000000) * 1000,
   };
   clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static uint64_t
delta(uint64_t a, uint64_t b)
{
   /* deal with rollover: */
   if (a > b)
      return 0xffffffffffffffffull - a + b;
   else
      return b - a;
}

static void
find_device(void)
{
   int ret;

   dev.dev = fd_device_open();
   if (!dev.dev)
      err(1, "could not open drm device");

   dev.pipe = fd_pipe_new(dev.dev, FD_PIPE_3D);

   dev.dev_id = fd_pipe_dev_id(dev.pipe);
   if (!fd_dev_info_raw(dev.dev_id))
      err(1, "unknown device");

   printf("device: %s\n", fd_dev_name(dev.dev_id));

   /* try MAX_FREQ first as that will work regardless of old dt
    * dt bindings vs upstream bindings:
    */
   uint64_t val;
   ret = fd_pipe_get_param(dev.pipe, FD_MAX_FREQ, &val);
   if (ret) {
      printf("falling back to parsing DT bindings for freq\n");
      if (!fd_dt_find_freqs(&dev.min_freq, &dev.max_freq))
         err(1, "could not find GPU freqs");
   } else {
      dev.min_freq = 0;
      dev.max_freq = val;
   }

   printf("min_freq=%u, max_freq=%u\n", dev.min_freq, dev.max_freq);

   dev.io = fd_dt_find_io();
   if (!dev.io) {
      err(1, "could not map device");
   }

   fd_pipe_set_param(dev.pipe, FD_SYSPROF, 1);
}

/*
 * perf-monitor
 */

static void
flush_ring(void)
{
   if (!dev.submit)
      return;

   struct fd_fence *fence = fd_submit_flush(dev.submit, -1, false);

   if (!fence)
      errx(1, "submit failed");

   fd_fence_flush(fence);
   fd_fence_del(fence);
   fd_ringbuffer_del(dev.ring);
   fd_submit_del(dev.submit);

   dev.ring = NULL;
   dev.submit = NULL;
}

static void
select_counter(struct counter_group *group, int ctr, int countable_val)
{
   assert(ctr < group->group->num_counters);

   unsigned countable_idx = UINT32_MAX;
   for (unsigned i = 0; i < group->group->num_countables; i++) {
      if (countable_val != group->group->countables[i].selector)
         continue;

      countable_idx = i;
      break;
   }

   if (countable_idx >= group->group->num_countables)
      return;

   group->label[ctr] = group->group->countables[countable_idx].name;
   group->counter[ctr].select_val = countable_val;

   if (!dev.submit) {
      dev.submit = fd_submit_new(dev.pipe);
      dev.ring = fd_submit_new_ringbuffer(
         dev.submit, 0x1000, FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE);
   }

   /* bashing select register directly while gpu is active will end
    * in tears.. so we need to write it via the ring:
    *
    * TODO it would help startup time, if gpu is loaded, to batch
    * all the initial writes and do a single flush.. although that
    * makes things more complicated for capturing inital sample value
    */
   struct fd_ringbuffer *ring = dev.ring;
   switch (fd_dev_gen(dev.dev_id)) {
   case 2:
   case 3:
   case 4:
      OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
      OUT_RING(ring, 0x00000000);

      if (group->group->counters[ctr].enable) {
         OUT_PKT0(ring, group->group->counters[ctr].enable, 1);
         OUT_RING(ring, 0);
      }

      if (group->group->counters[ctr].clear) {
         OUT_PKT0(ring, group->group->counters[ctr].clear, 1);
         OUT_RING(ring, 1);

         OUT_PKT0(ring, group->group->counters[ctr].clear, 1);
         OUT_RING(ring, 0);
      }

      OUT_PKT0(ring, group->group->counters[ctr].select_reg, 1);
      OUT_RING(ring, countable_val);

      if (group->group->counters[ctr].enable) {
         OUT_PKT0(ring, group->group->counters[ctr].enable, 1);
         OUT_RING(ring, 1);
      }

      break;
   case 5:
   case 6:
   case 7:
      OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);

      if (group->group->counters[ctr].enable) {
         OUT_PKT4(ring, group->group->counters[ctr].enable, 1);
         OUT_RING(ring, 0);
      }

      if (group->group->counters[ctr].clear) {
         OUT_PKT4(ring, group->group->counters[ctr].clear, 1);
         OUT_RING(ring, 1);

         OUT_PKT4(ring, group->group->counters[ctr].clear, 1);
         OUT_RING(ring, 0);
      }

      OUT_PKT4(ring, group->group->counters[ctr].select_reg, 1);
      OUT_RING(ring, countable_val);

      if (group->group->counters[ctr].enable) {
         OUT_PKT4(ring, group->group->counters[ctr].enable, 1);
         OUT_RING(ring, 1);
      }

      break;
   }
}

static uint64_t load_counter_value(struct counter_group *group, int ctr)
{
   /* We can read the counter register value as an uint64_t, as long as the
    * lo/hi addresses are neighboring and the lo address is 8-byte-aligned.
    * This currently holds for all counters exposed in perfcounter groups.
    */
   const struct fd_perfcntr_counter *counter = group->counter[ctr].counter;
   assert(counter->counter_reg_lo + 1 == counter->counter_reg_hi);
   assert(!((counter->counter_reg_lo * 4) % 8));
   return *((uint64_t *) (dev.io + counter->counter_reg_lo * 4));
}

static void
resample_counter(struct counter_group *group, int ctr, uint64_t sample_time)
{
   uint64_t previous_value = group->value[ctr];
   group->value[ctr] = load_counter_value(group, ctr);
   group->value_delta[ctr] = delta(previous_value, group->value[ctr]);

   uint64_t previous_sample_time = group->sample_time[ctr];
   group->sample_time[ctr] = sample_time;
   group->sample_time_delta[ctr] = delta(previous_sample_time, sample_time);
}

/* sample all the counters: */
static void
resample(void)
{
   static uint64_t last_time;
   uint64_t current_time = gettime_us();

   if ((current_time - last_time) < (options.refresh_ms * 1000 / 2))
      return;

   last_time = current_time;

   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];
      for (unsigned j = 0; j < group->group->num_counters; j++) {
         resample_counter(group, j, current_time);
      }
   }
}

/*
 * The UI
 */

#define COLOR_GROUP_HEADER 1
#define COLOR_FOOTER       2
#define COLOR_INVERSE      3

static int w, h;
static int ctr_width;
static int max_rows, current_cntr = 1;

static void
redraw_footer(WINDOW *win)
{
   char footer[128];
   int n = snprintf(footer, sizeof(footer), " fdperf: %s (%.2fMHz..%.2fMHz)",
                    fd_dev_name(dev.dev_id), ((float)dev.min_freq) / 1000000.0,
                    ((float)dev.max_freq) / 1000000.0);

   wmove(win, h - 1, 0);
   wattron(win, COLOR_PAIR(COLOR_FOOTER));
   waddstr(win, footer);
   whline(win, ' ', w - n);
   wattroff(win, COLOR_PAIR(COLOR_FOOTER));
}

static void
redraw_group_header(WINDOW *win, int row, const char *name)
{
   wmove(win, row, 0);
   wattron(win, A_BOLD);
   wattron(win, COLOR_PAIR(COLOR_GROUP_HEADER));
   waddstr(win, name);
   whline(win, ' ', w - strlen(name));
   wattroff(win, COLOR_PAIR(COLOR_GROUP_HEADER));
   wattroff(win, A_BOLD);
}

static void
redraw_counter_label(WINDOW *win, int row, const char *name, bool selected)
{
   int n = strlen(name);
   assert(n <= ctr_width);
   wmove(win, row, 0);
   whline(win, ' ', ctr_width - n);
   wmove(win, row, ctr_width - n);
   if (selected)
      wattron(win, COLOR_PAIR(COLOR_INVERSE));
   waddstr(win, name);
   if (selected)
      wattroff(win, COLOR_PAIR(COLOR_INVERSE));
   waddstr(win, ": ");
}

static void
redraw_counter_value_cycles(WINDOW *win, float val)
{
   char str[32];
   int x = getcurx(win);
   int valwidth = w - x;
   int barwidth, n;

   /* convert to fraction of max freq: */
   val = val / (float)dev.max_freq;

   /* figure out percentage-bar width: */
   barwidth = (int)(val * valwidth);

   /* sometimes things go over 100%.. idk why, could be
    * things running faster than base clock, or counter
    * summing up cycles in multiple cores?
    */
   barwidth = MIN2(barwidth, valwidth - 1);

   n = snprintf(str, sizeof(str), "%.2f%%", 100.0 * val);
   wattron(win, COLOR_PAIR(COLOR_INVERSE));
   waddnstr(win, str, barwidth);
   if (barwidth > n) {
      whline(win, ' ', barwidth - n);
      wmove(win, getcury(win), x + barwidth);
   }
   wattroff(win, COLOR_PAIR(COLOR_INVERSE));
   if (barwidth < n)
      waddstr(win, str + barwidth);
   whline(win, ' ', w - getcurx(win));
}

static void
redraw_counter_value(WINDOW *win, int row, struct counter_group *group, int ctr)
{
   char str[32];
   int n = snprintf(str, sizeof(str), "%" PRIu64 " ", group->value_delta[ctr]);

   whline(win, ' ', 24 - n);
   wmove(win, row, getcurx(win) + 24 - n);
   waddstr(win, str);

   /* quick hack, if the label has "CYCLE" in the name, it is
    * probably a cycle counter ;-)
    * Perhaps add more info in rnndb schema to know how to
    * treat individual counters (ie. which are cycles, and
    * for those we want to present as a percentage do we
    * need to scale the result.. ie. is it running at some
    * multiple or divisor of core clk, etc)
    *
    * TODO it would be much more clever to get this from xml
    * Also.. in some cases I think we want to know how many
    * units the counter is counting for, ie. if a320 has 2x
    * shader as a306 we might need to scale the result..
    */
   if (strstr(group->label[ctr], "CYCLE") ||
       strstr(group->label[ctr], "BUSY") || strstr(group->label[ctr], "IDLE")) {
      float cycles_val = (float) group->value_delta[ctr] * 1000000.0 /
                         (float) group->sample_time_delta[ctr];
      redraw_counter_value_cycles(win, cycles_val);
   } else {
      whline(win, ' ', w - getcurx(win));
   }
}

static void
redraw_counter(WINDOW *win, int row, struct counter_group *group, int ctr,
               bool selected)
{
   redraw_counter_label(win, row, group->label[ctr], selected);
   redraw_counter_value(win, row, group, ctr);
}

static void
redraw_gpufreq_counter(WINDOW *win, int row)
{
   redraw_counter_label(win, row, "Freq (MHz)", false);

   struct counter_group *group = &dev.groups[0];
   float freq_val = (float) group->value_delta[0] / (float) group->sample_time_delta[0];

   char str[32];
   snprintf(str, sizeof(str), "%.2f", freq_val);

   waddstr(win, str);
   whline(win, ' ', w - getcurx(win));
}

static void
redraw(WINDOW *win)
{
   static int scroll = 0;
   int max, row = 0;

   w = getmaxx(win);
   h = getmaxy(win);

   max = h - 3;

   if ((current_cntr - scroll) > (max - 1)) {
      scroll = current_cntr - (max - 1);
   } else if ((current_cntr - 1) < scroll) {
      scroll = current_cntr - 1;
   }

   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];
      unsigned j = 0;

      if (group->counter[0].is_gpufreq_counter)
         j++;

      if (j < group->group->num_counters) {
         if ((scroll <= row) && ((row - scroll) < max))
            redraw_group_header(win, row - scroll, group->group->name);
         row++;
      }

      for (; j < group->group->num_counters; j++) {
         if ((scroll <= row) && ((row - scroll) < max))
            redraw_counter(win, row - scroll, group, j, row == current_cntr);
         row++;
      }
   }

   /* convert back to physical (unscrolled) offset: */
   row = max;

   redraw_group_header(win, row, "Status");
   row++;

   /* Draw GPU freq row: */
   redraw_gpufreq_counter(win, row);
   row++;

   redraw_footer(win);

   refresh();
}

static struct counter_group *
current_counter(int *ctr)
{
   int n = 0;

   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];
      unsigned j = 0;

      if (group->counter[0].is_gpufreq_counter)
         j++;

      /* account for group header: */
      if (j < group->group->num_counters) {
         /* cannot select group header.. return null to indicate this
          * main_ui():
          */
         if (n == current_cntr)
            return NULL;
         n++;
      }

      for (; j < group->group->num_counters; j++) {
         if (n == current_cntr) {
            if (ctr)
               *ctr = j;
            return group;
         }
         n++;
      }
   }

   assert(0);
   return NULL;
}

static void
counter_dialog(void)
{
   WINDOW *dialog;
   struct counter_group *group;
   int cnt = 0, current = 0, scroll;

   /* figure out dialog size: */
   int dh = h / 2;
   int dw = ctr_width + 2;

   group = current_counter(&cnt);

   /* find currently selected idx (note there can be discontinuities
    * so the selected value does not map 1:1 to current idx)
    */
   uint32_t selected = group->counter[cnt].select_val;
   for (int i = 0; i < group->group->num_countables; i++) {
      if (group->group->countables[i].selector == selected) {
         current = i;
         break;
      }
   }

   /* scrolling offset, if dialog is too small for all the choices: */
   scroll = 0;

   dialog = newwin(dh, dw, (h - dh) / 2, (w - dw) / 2);
   box(dialog, 0, 0);
   wrefresh(dialog);
   keypad(dialog, true);

   while (true) {
      int max = MIN2(dh - 2, group->group->num_countables);
      int selector = -1;

      if ((current - scroll) >= (dh - 3)) {
         scroll = current - (dh - 3);
      } else if (current < scroll) {
         scroll = current;
      }

      for (int i = 0; i < max; i++) {
         int n = scroll + i;
         wmove(dialog, i + 1, 1);
         if (n == current) {
            assert(n < group->group->num_countables);
            selector = group->group->countables[n].selector;
            wattron(dialog, COLOR_PAIR(COLOR_INVERSE));
         }
         if (n < group->group->num_countables)
            waddstr(dialog, group->group->countables[n].name);
         whline(dialog, ' ', dw - getcurx(dialog) - 1);
         if (n == current)
            wattroff(dialog, COLOR_PAIR(COLOR_INVERSE));
      }

      assert(selector >= 0);

      switch (wgetch(dialog)) {
      case KEY_UP:
         current = MAX2(0, current - 1);
         break;
      case KEY_DOWN:
         current = MIN2(group->group->num_countables - 1, current + 1);
         break;
      case KEY_LEFT:
      case KEY_ENTER:
         /* select new sampler */
         select_counter(group, cnt, selector);
         flush_ring();
         config_save();
         goto out;
      case 'q':
         goto out;
      default:
         /* ignore */
         break;
      }

      resample();
   }

out:
   wborder(dialog, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
   delwin(dialog);
}

static void
scroll_cntr(int amount)
{
   if (amount < 0) {
      current_cntr = MAX2(1, current_cntr + amount);
      if (current_counter(NULL) == NULL) {
         current_cntr = MAX2(1, current_cntr - 1);
      }
   } else {
      current_cntr = MIN2(max_rows - 1, current_cntr + amount);
      if (current_counter(NULL) == NULL)
         current_cntr = MIN2(max_rows - 1, current_cntr + 1);
   }
}

static void
main_ui(void)
{
   WINDOW *mainwin;
   uint64_t last_time = gettime_us();

   /* Run an initial sample to set up baseline counter values. */
   resample();

   /* curses setup: */
   mainwin = initscr();
   if (!mainwin)
      goto out;

   cbreak();
   wtimeout(mainwin, options.refresh_ms);
   noecho();
   keypad(mainwin, true);
   curs_set(0);
   start_color();
   init_pair(COLOR_GROUP_HEADER, COLOR_WHITE, COLOR_GREEN);
   init_pair(COLOR_FOOTER, COLOR_WHITE, COLOR_BLUE);
   init_pair(COLOR_INVERSE, COLOR_BLACK, COLOR_WHITE);

   while (true) {
      switch (wgetch(mainwin)) {
      case KEY_UP:
         scroll_cntr(-1);
         break;
      case KEY_DOWN:
         scroll_cntr(+1);
         break;
      case KEY_NPAGE: /* page-down */
         /* TODO figure out # of rows visible? */
         scroll_cntr(+15);
         break;
      case KEY_PPAGE: /* page-up */
         /* TODO figure out # of rows visible? */
         scroll_cntr(-15);
         break;
      case KEY_RIGHT:
         counter_dialog();
         break;
      case 'q':
         goto out;
         break;
      default:
         /* ignore */
         break;
      }
      resample();
      redraw(mainwin);

      /* restore the counters every 0.5s in case the GPU has suspended,
       * in which case the current selected countables will have reset:
       */
      uint64_t t = gettime_us();
      if (delta(last_time, t) > 500000) {
         restore_counter_groups();
         flush_ring();
         last_time = t;
      }
   }

   /* restore settings.. maybe we need an atexit()??*/
out:
   delwin(mainwin);
   endwin();
   refresh();
}

static void
dump_counters(void)
{
   resample();
   sleep_us(options.refresh_ms * 1000);
   resample();

   for (unsigned i = 0; i < dev.ngroups; i++) {
      const struct counter_group *group = &dev.groups[i];
      for (unsigned j = 0; j < group->group->num_counters; j++) {
         const char *label = group->label[j];
         float val = (float) group->value_delta[j] * 1000000.0 /
                     (float) group->sample_time_delta[j];

         int n = printf("%s: ", label) - 2;
         while (n++ < ctr_width)
            fputc(' ', stdout);

         n = printf("%" PRIu64, group->value_delta[j]);
         while (n++ < 24)
            fputc(' ', stdout);

         if (strstr(label, "CYCLE") ||
             strstr(label, "BUSY") ||
             strstr(label, "IDLE")) {
            val = val / dev.max_freq * 100.0f;
            printf(" %.2f%%\n", val);
         } else {
            printf("\n");
         }
      }
   }
}

static void
restore_counter_groups(void)
{
   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];

      for (unsigned j = 0; j < group->group->num_counters; j++) {
         /* This should also write the CP_ALWAYS_COUNT selectable value into
          * the reserved CP counter we use for GPU frequency measurement,
          * avoiding someone else writing a different value there.
          */
         select_counter(group, j, group->counter[j].select_val);
      }
   }
}

static void
setup_counter_groups(const struct fd_perfcntr_group *groups)
{
   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];

      group->group = &groups[i];

      max_rows += group->group->num_counters + 1;

      /* We reserve the first counter of the CP group (first in the list) for
       * measuring GPU frequency that's displayed in the footer.
       */
      if (i == 0) {
         /* We won't be displaying the private counter alongside others. We
          * also won't be displaying the group header if we're taking over
          * the only counter (e.g. on a2xx).
          */
         max_rows--;
         if (groups[0].num_counters < 2)
            max_rows--;

         /* Enforce the CP_ALWAYS_COUNT countable for this counter. */
         unsigned always_count_index = UINT32_MAX;
         for (unsigned i = 0; i < groups[0].num_countables; ++i) {
            if (strcmp(groups[0].countables[i].name, "PERF_CP_ALWAYS_COUNT"))
               continue;

            always_count_index = i;
            break;
         }

         if (always_count_index < groups[0].num_countables) {
            group->counter[0].select_val = groups[0].countables[always_count_index].selector;
            group->counter[0].is_gpufreq_counter = true;
         }
      }

      for (unsigned j = 0; j < group->group->num_counters; j++) {
         group->counter[j].counter = &group->group->counters[j];

         if (!group->counter[j].is_gpufreq_counter)
            group->counter[j].select_val = j;
      }

      for (unsigned j = 0; j < group->group->num_countables; j++) {
         ctr_width =
            MAX2(ctr_width, strlen(group->group->countables[j].name) + 1);
      }
   }
}

/*
 * configuration / persistence
 */

static config_t cfg;
static config_setting_t *setting;

static void
config_sanitize_device_name(char *name)
{
   /* libconfig names allow alphanumeric characters, dashes, underscores and
    * asterisks. Anything else in the device name (most commonly spaces and
    * plus characters) should be converted to underscores.
    */
   for (char *s = name; *s; ++s) {
      if (isalnum(*s) || *s == '-' || *s == '_' || *s == '*')
         continue;
      *s = '_';
   }
}

static void
config_save(void)
{
   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];
      config_setting_t *sect =
         config_setting_get_member(setting, group->group->name);

      for (unsigned j = 0; j < group->group->num_counters; j++) {
         /* Don't save the GPU frequency measurement counter. */
         if (group->counter[j].is_gpufreq_counter)
            continue;

         char name[] = "counter0000";
         sprintf(name, "counter%d", j);
         config_setting_t *s = config_setting_lookup(sect, name);
         config_setting_set_int(s, group->counter[j].select_val);
      }
   }

   config_write_file(&cfg, "fdperf.cfg");
}

static void
config_restore(void)
{
   config_init(&cfg);

   /* Read the file. If there is an error, report it and exit. */
   if (!config_read_file(&cfg, "fdperf.cfg")) {
      warn("could not restore settings");
   }

   config_setting_t *root = config_root_setting(&cfg);

   /* per device settings: */
   char device_name[64];
   snprintf(device_name, sizeof(device_name), "%s", fd_dev_name(dev.dev_id));
   config_sanitize_device_name(device_name);
   setting = config_setting_get_member(root, device_name);
   if (!setting)
      setting = config_setting_add(root, device_name, CONFIG_TYPE_GROUP);
   if (!setting)
      return;

   for (unsigned i = 0; i < dev.ngroups; i++) {
      struct counter_group *group = &dev.groups[i];
      config_setting_t *sect =
         config_setting_get_member(setting, group->group->name);

      if (!sect) {
         sect =
            config_setting_add(setting, group->group->name, CONFIG_TYPE_GROUP);
      }

      for (unsigned j = 0; j < group->group->num_counters; j++) {
         /* Don't restore the GPU frequency measurement counter. */
         if (group->counter[j].is_gpufreq_counter)
            continue;

         char name[] = "counter0000";
         sprintf(name, "counter%d", j);
         config_setting_t *s = config_setting_lookup(sect, name);
         if (!s) {
            config_setting_add(sect, name, CONFIG_TYPE_INT);
            continue;
         }
         select_counter(group, j, config_setting_get_int(s));
      }
   }
}

static void
print_usage(const char *argv0)
{
   fprintf(stderr,
           "Usage: %s [OPTION]...\n"
           "\n"
           "  -r <N>     refresh every N milliseconds\n"
           "  -d         dump counters and exit\n"
           "  -h         show this message\n",
           argv0);
   exit(2);
}

static void
parse_options(int argc, char **argv)
{
   int c;

   while ((c = getopt(argc, argv, "r:d")) != -1) {
      switch (c) {
      case 'r':
         options.refresh_ms = atoi(optarg);
         break;
      case 'd':
         options.dump = true;
         break;
      default:
         print_usage(argv[0]);
         break;
      }
   }
}

/*
 * main
 */

int
main(int argc, char **argv)
{
   parse_options(argc, argv);

   find_device();

   const struct fd_perfcntr_group *groups;
   groups = fd_perfcntrs(dev.dev_id, &dev.ngroups);
   if (!groups) {
      errx(1, "no perfcntr support");
   }

   dev.groups = calloc(dev.ngroups, sizeof(struct counter_group));

   setlocale(LC_NUMERIC, "en_US.UTF-8");

   setup_counter_groups(groups);
   restore_counter_groups();
   config_restore();
   flush_ring();

   if (options.dump)
      dump_counters();
   else
      main_ui();

   return 0;
}
