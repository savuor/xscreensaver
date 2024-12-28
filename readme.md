# Analog TV emulator

This repo contains a tool that shows images or videos like they are on an old TV screen. NTSC standard is emulated, interlacing is not used.

It's based on a tool called `analogtv-cli` from [XScreensaver](https://www.jwz.org/xscreensaver/) stripped to the minimum.
The original code is written by [Trevor Blackwell](https://tlb.org/), [Jamie Zawinski](https://jwz.org/) and the team.

It imitates old TV so well that I always wanted to have this as a filter.

OpenCV is used just for video I/O, image loading and memory management.
wxWidgets will be used for GUI (or will not).

### How to build
* Get OpenCV, wxWidgets and CMake
  - wxWidgets is not used yet; will be added later (or not)
* Run CMake with the flags:
  - `-DOpenCV_DIR=<path_to_OpenCV_installation>`
  - when wxWidgets is added: `-DwxWidgets_DIR=<path_to_wxWidgets_installation>`
* Build it

### How to run
* Provide several signal sources, several outputs (they will get the same frames) and some other parameters
* Example:
  ```
  analogtv-cli --control :random:duration=60:powerup --size 1280 1024 --in image.png :bars:logo.png :cam:0 sintel.avi --out video.mp4 :highgui
  ```
* For more details, see command line help

### TODO
* keep desired FPS
* make some kind of GUI to switch channels and rotate knobs
* transform this code to a platform-independent shader-like filter

### Copyright notice

xscreensaver, Copyright (c) 1992-2014 Jamie Zawinski <jwz@jwz.org>

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.  No representations are made about the suitability of this
software for any purpose.  It is provided "as is" without express or 
implied warranty.