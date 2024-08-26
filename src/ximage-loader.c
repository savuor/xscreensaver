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

extern Pixmap file_to_pixmap (Display *, Window, const char *filename,
                              int *width_ret, int *height_ret,
                              Pixmap *mask_ret);

extern Pixmap image_data_to_pixmap (Display *, Window, 
                                    const unsigned char *image_data,
                                    unsigned long data_size,
                                    int *width_ret, int *height_ret,
                                    Pixmap *mask_ret);

/* This XImage has RGBA data, which is what OpenGL code typically expects.
   Also it is upside down: the origin is at the bottom left of the image.
   X11 typically expects 0RGB as it has no notion of alpha, only 1-bit masks.
   With X11 code, you should probably use the _pixmap routines instead.
 */
extern XImage *image_data_to_ximage (Display *, Visual *,
                                     const unsigned char *image_data,
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
make_ximage (Display *dpy, Visual *visual, const char *filename,
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
      if (dpy)
        {
          /* Turns out gdk-pixbuf works even if you don't have display
             connection, which is good news for analogtv-cli. */
        }
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

    image = custom_XCreateImage (dpy, visual, 32, ZPixmap, 0, 0, w, h, 32, 0);
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

/* Given a bitmask, returns the position and width of the field.
 */
static void
decode_mask (unsigned long mask, unsigned long *pos_ret,
             unsigned long *size_ret)
{
  int i;
  for (i = 0; i < 32; i++)
    if (mask & (1L << i))
      {
        int j = 0;
        *pos_ret = i;
        for (; i < 32; i++, j++)
          if (! (mask & (1L << i)))
            break;
        *size_ret = j;
        return;
      }
}


/* Loads the image to a Pixmap and optional 1-bit mask.
 */
static Pixmap
make_pixmap (Display *dpy, Window window,
             const char *filename,
             const unsigned char *image_data, unsigned long data_size,
             int *width_ret, int *height_ret, Pixmap *mask_ret)
{
  XWindowAttributes xgwa;
  XImage *in, *out, *mask = 0;
  Pixmap pixmap;
  XGCValues gcv;
  GC gc;
  int x, y;

  unsigned long crpos=0, cgpos=0, cbpos=0, capos=0; /* bitfield positions */
  unsigned long srpos=0, sgpos=0, sbpos=0;
  unsigned long srmsk=0, sgmsk=0, sbmsk=0;
  unsigned long srsiz=0, sgsiz=0, sbsiz=0;

# ifdef HAVE_JWXYZ
  /* BlackPixel has alpha: 0xFF000000. */
  unsigned long black = BlackPixelOfScreen (DefaultScreenOfDisplay (dpy));
#else
  unsigned long black = 0;
# endif

  XGetWindowAttributes (dpy, window, &xgwa);

  in = make_ximage (dpy, xgwa.visual, filename, image_data, data_size);
  if (!in) return 0;

  /* Create a new image in the depth and bit-order of the server. */
  out = custom_XCreateImage (dpy, xgwa.visual, xgwa.depth, ZPixmap, 0, 0,
                      in->width, in->height, 8, 0);

  out->bitmap_bit_order = in->bitmap_bit_order;
  out->byte_order = in->byte_order;

  out->bitmap_bit_order = BitmapBitOrder (dpy);
  out->byte_order = ImageByteOrder (dpy);

  out->data = (char *) malloc (out->height * out->bytes_per_line);
  if (!out->data) abort();

  if (mask_ret)
    {
      mask = custom_XCreateImage (dpy, xgwa.visual, 1, XYPixmap, 0, 0,
                           in->width, in->height, 8, 0);
      mask->byte_order = in->byte_order;
      mask->data = (char *) malloc (mask->height * mask->bytes_per_line);
    }

  /* Find the server's color masks.
   */
  srmsk = out->red_mask;
  sgmsk = out->green_mask;
  sbmsk = out->blue_mask;

  if (!(srmsk && sgmsk && sbmsk)) abort();  /* No server color masks? */

  decode_mask (srmsk, &srpos, &srsiz);
  decode_mask (sgmsk, &sgpos, &sgsiz);
  decode_mask (sbmsk, &sbpos, &sbsiz);

  /* 'in' is RGBA in client endianness.  Convert to what the server wants. */
  if (bigendian())
    crpos = 24, cgpos = 16, cbpos =  8, capos =  0;
  else
    crpos =  0, cgpos =  8, cbpos = 16, capos = 24;

  for (y = 0; y < in->height; y++)
    for (x = 0; x < in->width; x++)
      {
        unsigned long p = XGetPixel (in, x, y);
        unsigned char a = (p >> capos) & 0xFF;
        unsigned char b = (p >> cbpos) & 0xFF;
        unsigned char g = (p >> cgpos) & 0xFF;
        unsigned char r = (p >> crpos) & 0xFF;
        XPutPixel (out, x, y, ((r << srpos) |
                               (g << sgpos) |
                               (b << sbpos) |
                               black));
        if (mask)
          XPutPixel (mask, x, y, (a ? 1 : 0));
      }

  custom_XDestroyImage (in);
  in = 0;

  pixmap = dummy_XCreatePixmap (dpy, window, out->width, out->height, xgwa.depth);
  gc = dummy_XCreateGC (dpy, pixmap, 0, &gcv);
  XPutImage (dpy, pixmap, gc, out, 0, 0, 0, 0, out->width, out->height);
  XFreeGC (dpy, gc);

  if (mask)
    {
      Pixmap p2 = dummy_XCreatePixmap (dpy, window, mask->width, mask->height, 1);
      gcv.foreground = 1;
      gcv.background = 0;
      gc = dummy_XCreateGC (dpy, p2, GCForeground|GCBackground, &gcv);
      XPutImage (dpy, p2, gc, mask, 0, 0, 0, 0, mask->width, mask->height);
      XFreeGC (dpy, gc);
      custom_XDestroyImage (mask);
      mask = 0;
      *mask_ret = p2;
    }

  if (width_ret)  *width_ret  = out->width;
  if (height_ret) *height_ret = out->height;

  custom_XDestroyImage (out);

  return pixmap;
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


Pixmap
image_data_to_pixmap (Display *dpy, Window window, 
                      const unsigned char *image_data, unsigned long data_size,
                      int *width_ret, int *height_ret,
                      Pixmap *mask_ret)
{
  return make_pixmap (dpy, window, 0, image_data, data_size,
                      width_ret, height_ret, mask_ret);
}

Pixmap
file_to_pixmap (Display *dpy, Window window, const char *filename,
                int *width_ret, int *height_ret,
                Pixmap *mask_ret)
{
  return make_pixmap (dpy, window, filename, 0, 0,
                      width_ret, height_ret, mask_ret);
}


/* This XImage has RGBA data, which is what OpenGL code typically expects.
   Also it is upside down: the origin is at the bottom left of the image.
   X11 typically expects 0RGB as it has no notion of alpha, only 1-bit masks.
   With X11 code, you should probably use the _pixmap routines instead.
 */
XImage *
image_data_to_ximage (Display *dpy, Visual *visual,
                      const unsigned char *image_data,
                      unsigned long data_size)
{
  XImage *ximage = make_ximage (dpy, visual, 0, image_data, data_size);
  flip_ximage (ximage);
  return ximage;
}

XImage *
file_to_ximage (Display *dpy, Visual *visual, const char *filename)
{
  XImage *ximage = make_ximage (dpy, visual, filename, 0, 0);
  flip_ximage (ximage);
  return ximage;
}
