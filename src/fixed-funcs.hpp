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

#define DoRed			(1<<0)
#define DoGreen			(1<<1)
#define DoBlue			(1<<2)

// #define StaticGray		0
// #define GrayScale		1
// #define StaticColor		2
// #define PseudoColor		3
#define TrueColor		4
//#define DirectColor		5

#define LSBFirst		0
#define MSBFirst		1

struct XColor {
  unsigned long pixel;
  unsigned short red, green, blue;
  char flags;  /* do_red, do_green, do_blue */
  char pad;
};

struct XImage
{
    int width, height;		/* size of image */

    char *data;			/* pointer to image data */
    int byte_order;		/* data byte order, LSBFirst, MSBFirst */
    int bitmap_bit_order;	/* LSBFirst, MSBFirst */
    int depth;			/* depth of image */
    int bytes_per_line;		/* accelarator to next line */
    int bits_per_pixel;		/* bits per pixel (ZPixmap) */
    unsigned long red_mask;	/* bits in z arrangment */
    unsigned long green_mask;
    unsigned long blue_mask;
//  XPointer obdata;		/* hook for the object routines to hang on */
    struct funcs {		/* image manipulation routines */
	unsigned long (*get_pixel)  (XImage *, int, int);
	int (*put_pixel)            (XImage *, int, int, unsigned long);
    } f;
};

struct XWindowAttributes {
    int x, y;			/* location of window */
    int width, height;		/* width and height of window */
    int depth;          	/* depth of window */
};

// from resources.h
extern double get_float_resource (char*);

// from xshm.h

extern unsigned long
XGetPixel (XImage *ximage, int x, int y);

extern int
XPutPixel (XImage *ximage, int x, int y, unsigned long pixel);

// from analogtv-cli.c
XImage * custom_XCreateImage (unsigned int depth, int offset, char *data,
                    unsigned int width, unsigned int height,
                    int bytes_per_line);
int custom_XDestroyImage (XImage *ximage);
int custom_XGetWindowAttributes (XWindowAttributes *xgwa);
int custom_XPutImage (XImage *image,
                        int src_x, int src_y, int dest_x, int dest_y,
                        unsigned int w, unsigned int h);
int custom_XQueryColor (XColor *color);
int custom_XQueryColors (XColor *c, int n);

// from screenhackI.h

extern bool mono_p;

extern const char *progname;

#endif /* __FIXED_FUNCS_H__ */
