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

#include "precomp.hpp"

#include "screenhackI.hpp"
#include "ximage-loader.hpp"

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

  assert(filename);

  pb = gdk_pixbuf_new_from_file (filename, &gerr);
  if (!pb)
  {
    fprintf (stderr, "%s: %s\n", progname, gerr->message);
    return 0;
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
  data2 = (char*) malloc (ximage->bytes_per_line * ximage->height);
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

XImage *
file_to_ximage ( const char *filename)
{
  XImage *ximage = make_ximage ( filename, 0, 0);
  flip_ximage (ximage);
  return ximage;
}
