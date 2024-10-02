/* xscreensaver, Copyright (c) 1997, 1998, 2003 by Jamie Zawinski <jwz@jwz.org>
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

unsigned int ya_random ();
void ya_rand_init (unsigned int);

inline double ya_frand(double f)
{
  const double scale = (double)((unsigned int)~0);
  return std::abs(double(ya_random()) * f / scale);
}
