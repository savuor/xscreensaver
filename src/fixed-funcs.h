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

// from resources.h
extern Bool get_boolean_resource (Display*,char*,char*);
extern int get_integer_resource (Display*,char*,char*);
extern double get_float_resource (Display*,char*,char*);
extern unsigned int get_pixel_resource (Display*,Colormap,char*,char*);

// from visual.h
extern int visual_depth (Screen *, Visual *);
extern int visual_pixmap_depth (Screen *, Visual *);
extern int visual_class (Screen *, Visual *);
extern int visual_cells (Screen *, Visual *);
extern int screen_number (Screen *);

extern void visual_rgb_masks (Screen *screen, Visual *visual,
                              unsigned long *red_mask,
                              unsigned long *green_mask,
                              unsigned long *blue_mask);

// from xshm.h

# include <sys/ipc.h>
# include <sys/shm.h>
# include <X11/extensions/XShm.h>

extern XImage *create_xshm_image (Display *dpy, Visual *visual,
                                  unsigned int depth,
                                  int format, XShmSegmentInfo *shm_info,
                                  unsigned int width, unsigned int height);
extern Bool put_xshm_image (Display *dpy, Drawable d, GC gc, XImage *image,
                            int src_x, int src_y, int dest_x, int dest_y,
                            unsigned int width, unsigned int height,
                            XShmSegmentInfo *shm_info);
extern void destroy_xshm_image (Display *dpy, XImage *image,
                                XShmSegmentInfo *shm_info);

#endif /* __FIXED_FUNCS_H__ */
