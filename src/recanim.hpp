/* recanim, Copyright (c) 2014-2021 Jamie Zawinski <jwz@jwz.org>
 * Record animation frames of the running screenhack.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#ifndef __XSCREENSAVER_RECORD_ANIM_H__
# define __XSCREENSAVER_RECORD_ANIM_H__

#include "precomp.hpp"

#undef gettimeofday  /* wrapped by recanim.h */
#undef time
#define time screenhack_record_anim_time
#define gettimeofday screenhack_record_anim_gettimeofday

#endif /* __XSCREENSAVER_RECORD_ANIM_H__ */
