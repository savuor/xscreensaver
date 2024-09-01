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

#include "precomp.hpp"


// from jwxyz

struct XColor {
  unsigned long pixel;
  unsigned short red, green, blue;
};

struct XImage
{
    XImage() :
        width(),
        height(),
        data(),
        bytes_per_line()
    { }
    int width, height;		/* size of image */

    char *data;			/* pointer to image data */
    // int byte_order;		/* always LSBFirst */
    // int bitmap_bit_order;	/* always LSBFirst */
    // int depth;			/* always 32 */
    int bytes_per_line;		/* accelarator to next line */
    // int bits_per_pixel;		/* always 32 */
    // unsigned long red_mask  ; /* always 0x00FF0000L */
    // unsigned long green_mask; /* always 0x0000FF00L */
    // unsigned long blue_mask ; /* always 0x000000FFL */
};

// from resources.h
extern double get_float_resource (char*);

// from analogtv-cli.c
int custom_XPutImage (XImage *image,
                        int src_x, int src_y, int dest_x, int dest_y,
                        unsigned int w, unsigned int h);

// from screenhackI.h

extern bool mono_p;

extern const char *progname;

#endif /* __FIXED_FUNCS_H__ */
