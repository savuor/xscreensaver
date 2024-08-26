/* xscreensaver, Copyright © 1992-2023 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

/* Found in Don Hopkins' .plan file:
 *
 *   The color situation is a total flying circus.  The X approach to
 *   device independence is to treat everything like a MicroVax framebuffer
 *   on acid.  A truely portable X application is required to act like the
 *   persistent customer in the Monty Python ``Cheese Shop'' sketch.  Even
 *   the simplest applications must answer many difficult questions, like:
 *
 *   WHAT IS YOUR DISPLAY?
 *       display = XOpenDisplay("unix:0");
 *   WHAT IS YOUR ROOT?
 *       root = RootWindow(display, DefaultScreen(display));
 *   AND WHAT IS YOUR WINDOW?
 *       win = XCreateSimpleWindow(display, root, 0, 0, 256, 256, 1,
 *                                 BlackPixel(display, DefaultScreen(display)),
 *                                 WhitePixel(display, DefaultScreen(display)))
 *   OH ALL RIGHT, YOU CAN GO ON.
 *
 *   WHAT IS YOUR DISPLAY?
 *         display = XOpenDisplay("unix:0");
 *   WHAT IS YOUR COLORMAP?
 *         cmap = DefaultColormap(display, DefaultScreen(display));
 *   AND WHAT IS YOUR FAVORITE COLOR?
 *         favorite_color = 0; / * Black. * /
 *         / * Whoops! No, I mean: * /
 *         favorite_color = BlackPixel(display, DefaultScreen(display));
 *         / * AAAYYYYEEEEE!! (client dumps core & falls into the chasm) * /
 *
 *   WHAT IS YOUR DISPLAY?
 *         display = XOpenDisplay("unix:0");
 *   WHAT IS YOUR VISUAL?
 *         struct XVisualInfo vinfo;
 *         if (XMatchVisualInfo(display, DefaultScreen(display),
 *                              8, PseudoColor, &vinfo) != 0)
 *            visual = vinfo.visual;
 *   AND WHAT IS THE NET SPEED VELOCITY OF AN XConfigureWindow REQUEST?
 *         / * Is that a SubStructureRedirectMask or a ResizeRedirectMask? * /
 *   WHAT?! HOW AM I SUPPOSED TO KNOW THAT?
 *   AAAAUUUGGGHHH!!!! (server dumps core & falls into the chasm)
 */

#ifndef __SCREENHACK_I_H__
#define __SCREENHACK_I_H__

#include "precomp.h"

#include "yarandom.h"
#include "fixed-funcs.h"

#undef countof
#define countof(x) (sizeof((x))/sizeof((*x)))

#define RANDSIGN() ((random() & 1) ? 1 : -1)

extern Bool mono_p;

extern const char *progname;

#endif /* __SCREENHACK_I_H__ */
