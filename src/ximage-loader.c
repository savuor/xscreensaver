/* ximage-loader.c --- converts image files or data to XImages or Pixmap.
 * xscreensaver, Copyright © 1998-2022 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#include "precomp.h"

#include "screenhackI.h"
#include "ximage-loader.h"

/* This XImage has RGBA data, which is what OpenGL code typically expects.
   Also it is upside down: the origin is at the bottom left of the image.
   X11 typically expects 0RGB as it has no notion of alpha, only 1-bit masks.
   With X11 code, you should probably use the _pixmap routines instead.
 */
extern XImage *image_data_to_ximage (const unsigned char *image_data,
                                     unsigned long data_size);


static Bool
bigendian (void)
{
  union { int i; char c[sizeof(int)]; } u;
  u.i = 1;
  return !u.c[0];
}


/* Loads the image to an XImage, RGBA -- GDK Pixbuf version.
 */
static XImage *
make_ximage (const char *filename,
             const unsigned char *image_data, unsigned long data_size)
{
  GdkPixbuf *pb;
  static int initted = 0;
  GError *gerr = NULL;

  if (!initted)
    {
# if !GLIB_CHECK_VERSION(2, 36 ,0)
      g_type_init ();
# endif
      initted = 1;
    }

  if (filename)
    {
      pb = gdk_pixbuf_new_from_file (filename, &gerr);
      if (!pb)
        {
          fprintf (stderr, "%s: %s\n", progname, gerr->message);
          return 0;
        }
    }
  else
    {
      GInputStream *s =
        g_memory_input_stream_new_from_data (image_data, data_size, 0);
      pb = gdk_pixbuf_new_from_stream (s, 0, &gerr);

      g_input_stream_close (s, NULL, NULL);
      /* #### valgrind on xflame says there's a small leak in s? */
      g_object_unref (s);

      if (! pb)
        {
          /* fprintf (stderr, "%s: GDK unable to parse image data: %s\n",
                   progname, (gerr ? gerr->message : "?")); */
          return 0;
        }
    }

  if (!pb) abort();

  {
    XImage *image;
    int w = gdk_pixbuf_get_width (pb);
    int h = gdk_pixbuf_get_height (pb);
    guchar *row = gdk_pixbuf_get_pixels (pb);
    int stride = gdk_pixbuf_get_rowstride (pb);
    int chan = gdk_pixbuf_get_n_channels (pb);
    int x, y;

    image = custom_XCreateImage (32, ZPixmap, 0, 0, w, h, 32, 0);
    image->data = (char *) malloc(h * image->bytes_per_line);

    /* Set the bit order in the XImage structure to whatever the
       local host's native bit order is.
    */
    image->bitmap_bit_order =
      image->byte_order =
      (bigendian() ? MSBFirst : LSBFirst);

    if (!image->data)
      {
        fprintf (stderr, "%s: out of memory (%d x %d)\n", progname, w, h);
        return 0;
      }

    for (y = 0; y < h; y++)
      {
        guchar *i = row;
        for (x = 0; x < w; x++)
          {
            unsigned long rgba = 0;
            switch (chan) {
            case 1:
              rgba = ((0xFF << 24) |
                      (*i   << 16) |
                      (*i   <<  8) |
                       *i);
              i++;
              break;
            case 3:
              rgba = ((0xFF << 24) |
                      (i[2] << 16) |
                      (i[1] <<  8) |
                      i[0]);
              i += 3;
              break;
            case 4:
              rgba = ((i[3] << 24) |
                      (i[2] << 16) |
                      (i[1] <<  8) |
                      i[0]);
              i += 4;
              break;
            default:
              abort();
              break;
            }
            XPutPixel (image, x, y, rgba);
          }
        row += stride;
      }

    /* #### valgrind on xflame says there's a small leak in pb? */
    g_object_unref (pb);
    return image;
  }
}


/* Textures are upside down, so invert XImages before returning them.
 */
static void
flip_ximage (XImage *ximage)
{
  char *data2, *in, *out;
  int y;

  if (!ximage) return;
  data2 = malloc (ximage->bytes_per_line * ximage->height);
  if (!data2) abort();
  in = ximage->data;
  out = data2 + ximage->bytes_per_line * (ximage->height - 1);
  for (y = 0; y < ximage->height; y++)
    {
      memcpy (out, in, ximage->bytes_per_line);
      in  += ximage->bytes_per_line;
      out -= ximage->bytes_per_line;
    }
  free (ximage->data);
  ximage->data = data2;
}

/* This XImage has RGBA data, which is what OpenGL code typically expects.
   Also it is upside down: the origin is at the bottom left of the image.
   X11 typically expects 0RGB as it has no notion of alpha, only 1-bit masks.
   With X11 code, you should probably use the _pixmap routines instead.
 */
XImage *
image_data_to_ximage (const unsigned char *image_data,
                      unsigned long data_size)
{
  XImage *ximage = make_ximage ( 0, image_data, data_size);
  flip_ximage (ximage);
  return ximage;
}

XImage *
file_to_ximage ( const char *filename)
{
  XImage *ximage = make_ximage ( filename, 0, 0);
  flip_ximage (ximage);
  return ximage;
}
