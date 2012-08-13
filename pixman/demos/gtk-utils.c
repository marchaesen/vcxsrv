#include <gtk/gtk.h>
#include <config.h>
#include "../test/utils.h"
#include "gtk-utils.h"

GdkPixbuf *
pixbuf_from_argb32 (uint32_t *bits,
		    int width,
		    int height,
		    int stride)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE,
					8, width, height);
    int p_stride = gdk_pixbuf_get_rowstride (pixbuf);
    guint32 *p_bits = (guint32 *)gdk_pixbuf_get_pixels (pixbuf);
    int i;

    for (i = 0; i < height; ++i)
    {
	uint32_t *src_row = &bits[i * (stride / 4)];
	uint32_t *dst_row = p_bits + i * (p_stride / 4);

	a8r8g8b8_to_rgba_np (dst_row, src_row, width);
    }

    return pixbuf;
}

static gboolean
on_expose (GtkWidget *widget, GdkEventExpose *expose, gpointer data)
{
    GdkPixbuf *pixbuf = data;
    
    gdk_draw_pixbuf (widget->window, NULL,
		     pixbuf, 0, 0, 0, 0,
		     gdk_pixbuf_get_width (pixbuf),
		     gdk_pixbuf_get_height (pixbuf),
		     GDK_RGB_DITHER_NONE,
		     0, 0);
    
    return TRUE;
}

void
show_image (pixman_image_t *image)
{
    GtkWidget *window;
    GdkPixbuf *pixbuf;
    int width, height;
    int argc;
    char **argv;
    char *arg0 = g_strdup ("pixman-test-program");
    pixman_format_code_t format;
    pixman_image_t *copy;

    argc = 1;
    argv = (char **)&arg0;

    gtk_init (&argc, &argv);
    
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    width = pixman_image_get_width (image);
    height = pixman_image_get_height (image);

    gtk_window_set_default_size (GTK_WINDOW (window), width, height);

    format = pixman_image_get_format (image);

    /* Three cases:
     *
     *  - image is a8r8g8b8_sRGB: we will display without modification
     *    under the assumption that the monitor is sRGB
     *
     *  - image is a8r8g8b8: we will display without modification
     *    under the assumption that whoever created the image
     *    probably did it wrong by using sRGB inputs
     *
     *  - other: we will convert to a8r8g8b8 under the assumption that
     *    whoever created the image probably did it wrong.
     */
    switch (format)
    {
    case PIXMAN_a8r8g8b8_sRGB:
    case PIXMAN_a8r8g8b8:
	copy = pixman_image_ref (image);
	break;

    default:
	copy = pixman_image_create_bits (PIXMAN_a8r8g8b8,
					 width, height, NULL, -1);
	pixman_image_composite32 (PIXMAN_OP_SRC,
				  image, NULL, copy,
				  0, 0, 0, 0, 0, 0,
				  width, height);
	break;
    }

    pixbuf = pixbuf_from_argb32 (pixman_image_get_data (copy),
				 width, height,
				 pixman_image_get_stride (copy));
    
    g_signal_connect (window, "expose_event", G_CALLBACK (on_expose), pixbuf);
    g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
    
    gtk_widget_show (window);
    
    gtk_main ();
}
