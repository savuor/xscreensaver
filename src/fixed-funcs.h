/* xscreensaver, Copyright (c) 1992-2014 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#ifndef __FIXED_FUNCS_H__
#define __FIXED_FUNCS_H__

#include "precomp.h"

// from resources.h
extern double get_float_resource (char*);

// from xshm.h

extern XImage *create_xshm_image (unsigned int depth,
                                  int format,
                                  unsigned int width, unsigned int height);
extern Bool put_xshm_image (XImage *image,
                            int src_x, int src_y, int dest_x, int dest_y,
                            unsigned int width, unsigned int height);
extern void destroy_xshm_image (XImage *image);

// from analogtv-cli.c
int dummy_XClearArea (Window win, int x, int y, unsigned int w, unsigned int h, Bool exp);
int dummy_XClearWindow (Window window);
XImage * custom_XCreateImage (unsigned int depth,
                    int format, int offset, char *data,
                    unsigned int width, unsigned int height,
                    int bitmap_pad, int bytes_per_line);
int custom_XDestroyImage (XImage *ximage);
Status custom_XGetWindowAttributes (Window w, XWindowAttributes *xgwa);
int custom_XPutImage (XImage *image,
                        int src_x, int src_y, int dest_x, int dest_y,
                        unsigned int w, unsigned int h);
int custom_XQueryColor (Colormap cmap, XColor *color);
int custom_XQueryColors (Colormap cmap, XColor *c, int n);
int dummy_XSetWindowBackground (Window win, unsigned long bg);


#endif /* __FIXED_FUNCS_H__ */
