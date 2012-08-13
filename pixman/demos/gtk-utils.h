#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "pixman.h"

void show_image (pixman_image_t *image);

GdkPixbuf *pixbuf_from_argb32 (uint32_t *bits,
                               int width,
                               int height,
                               int stride);
