
This repo contains a tool that shows images or videos like they are on an old TV screen.

It's based on a tool called `analogtv-cli` from [XScreensaver](https://www.jwz.org/xscreensaver/) stripped to the minimum.
The original code is written by [Trevor Blackwell](https://tlb.org/), [Jamie Zawinski](https://jwz.org/) and the team.

It imitates old TV so well that I always wanted to have this as a filter.

Build:
* Get OpenCV and CMake
* Run CMake with `-DOpenCV_DIR=<path_to_OpenCV_installation>` flag
* Build it

TODO:
* strip platform-dependent threads
* rewrite non-calculating code to proper c++
* fix channel issue
* transform this code to a platform-independent shader-like filter

Copyright notice:

xscreensaver, Copyright (c) 1992-2014 Jamie Zawinski <jwz@jwz.org>

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.  No representations are made about the suitability of this
software for any purpose.  It is provided "as is" without express or 
implied warranty.