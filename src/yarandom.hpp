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

#ifndef __YARANDOM_H__
#define __YARANDOM_H__

#include "precomp.hpp"

extern unsigned int ya_random (void);
extern void ya_rand_init (unsigned int);

#if defined (__GNUC__) && (__GNUC__ >= 2)
 /* Implement frand using GCC's statement-expression extension. */

# define ya_frand(f)							\
  __extension__								\
  ({ double tmp = ((((double) random()) * ((double) (f))) /		\
		   ((double) ((unsigned int)~0)));			\
     tmp < 0 ? (-tmp) : tmp; })

#else /* not GCC2 - implement frand using a global variable.*/

static double _ya_frand_tmp_;
# define ya_frand(f)							\
  (_ya_frand_tmp_ = ((((double) random()) * ((double) (f))) /		\
		  ((double) ((unsigned int)~0))),			\
   _ya_frand_tmp_ < 0 ? (-_ya_frand_tmp_) : _ya_frand_tmp_)

#endif /* not GCC2 */

#endif /* __YARANDOM_H__ */
