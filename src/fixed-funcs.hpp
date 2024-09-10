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

#pragma once

#include "precomp.hpp"

#include "utils.hpp"

// from analogtv-cli.c
int custom_XPutImage (const cv::Mat4b& img,
                        int src_x, int src_y, int dest_x, int dest_y,
                        unsigned int w, unsigned int h);

