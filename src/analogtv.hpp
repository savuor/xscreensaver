/* analogtv, Copyright (c) 2003-2018 Trevor Blackwell <tlb@tlb.org>
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

#include "thread_util.hpp"
#include "fixed-funcs.hpp"
#include "utils.hpp"

/* To simulate an NTSC CRT monitor with way more scanlines, and thus
   apply an ahistorical tv-like effect to a larger image, increase
   this resolution multiplier.
 */
#ifndef ANALOGTV_SCALE
# define ANALOGTV_SCALE 1
#endif

/*
  You'll need these to generate standard NTSC TV signals
 */
enum {
  /* We don't handle interlace here */
  ANALOGTV_V=262*ANALOGTV_SCALE,
  ANALOGTV_TOP=30*ANALOGTV_SCALE,
  ANALOGTV_VISLINES=200*ANALOGTV_SCALE,
  ANALOGTV_BOT=ANALOGTV_TOP + ANALOGTV_VISLINES,

  /* This really defines our sampling rate, 4x the colorburst
     frequency. Handily equal to the Apple II's dot clock.
     You could also make a case for using 3x the colorburst freq,
     but 4x isn't hard to deal with. */
  ANALOGTV_H=912*ANALOGTV_SCALE,

  /* Each line is 63500 nS long. The sync pulse is 4700 nS long, etc.
     Define sync, back porch, colorburst, picture, and front porch
     positions */
  ANALOGTV_SYNC_START=0,
  ANALOGTV_BP_START=4700*ANALOGTV_H/63500,
  ANALOGTV_CB_START=5800*ANALOGTV_H/63500,
  /* signal[row][ANALOGTV_PIC_START] is the first displayed pixel */
  ANALOGTV_PIC_START=9400*ANALOGTV_H/63500,
  ANALOGTV_PIC_LEN=52600*ANALOGTV_H/63500,
  ANALOGTV_FP_START=62000*ANALOGTV_H/63500,
  ANALOGTV_PIC_END=ANALOGTV_FP_START,

  /* TVs scan past the edges of the picture tube, so normally you only
     want to use about the middle 3/4 of the nominal scan line.
  */
  ANALOGTV_VIS_START=ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN*1/8),
  ANALOGTV_VIS_END=ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN*7/8),
  ANALOGTV_VIS_LEN=ANALOGTV_VIS_END-ANALOGTV_VIS_START,

  ANALOGTV_HASHNOISE_LEN=6*ANALOGTV_SCALE,

  ANALOGTV_GHOSTFIR_LEN=4,

  /* analogtv.signal is in IRE units, as defined below: */
  ANALOGTV_WHITE_LEVEL=100,
  ANALOGTV_GRAY50_LEVEL=55,
  ANALOGTV_GRAY30_LEVEL=35,
  ANALOGTV_BLACK_LEVEL=10,
  ANALOGTV_BLANK_LEVEL=0,
  ANALOGTV_SYNC_LEVEL=-40,
  ANALOGTV_CB_LEVEL=20,

  ANALOGTV_SIGNAL_LEN=ANALOGTV_V*ANALOGTV_H,

  /* The number of intensity levels we deal with for gamma correction &c */
  ANALOGTV_CV_MAX=1024,

  /* MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which
     it interpolates extra black lines. */
  ANALOGTV_MAX_LINEHEIGHT=12

};

typedef struct analogtv_input_s {
  signed char signal[ANALOGTV_V+1][ANALOGTV_H];

  /* for client use */
  void (*updater)(struct analogtv_input_s *inp);
  void *client_data;
  double next_update_time;

} analogtv_input;

typedef struct analogtv_reception_s {

  analogtv_input *input;
  double ofs;
  double level;
  double multipath;
  double freqerr;

  double ghostfir[ANALOGTV_GHOSTFIR_LEN];
  double ghostfir2[ANALOGTV_GHOSTFIR_LEN];

  double hfloss;
  double hfloss2;

} analogtv_reception;

/*
  The rest of this should be considered mostly opaque to the analogtv module.
 */

struct analogtv_yiq_s {
  float y,i,q;
} /*yiq[ANALOGTV_PIC_LEN+10] */;

typedef struct analogtv_s {
  int outbuffer_width;
  int outbuffer_height;

  struct threadpool threads;

#if 0
  unsigned int onscreen_signature[ANALOGTV_V];
#endif

  float agclevel;

  /* If you change these, call analogtv_set_demod */
  float tint_control,color_control,brightness_control,contrast_control;
  float height_control, width_control, squish_control;
  float horiz_desync;
  float squeezebottom;
  float powerup;

  /* For fast display, set fakeit_top, fakeit_bot to
     the scanlines (0..ANALOGTV_V) that can be preserved on screen.
     fakeit_scroll is the number of scan lines to scroll it up,
     or 0 to not scroll at all. It will DTRT if asked to scroll from
     an offscreen region.
  */
  // int fakeit_top;
  // int fakeit_bot;
  // int fakeit_scroll;
  int redraw_all;

  int usewidth, useheight, xrepl, subwidth;
  cv::Mat4b image; /* usewidth * useheight */

  int screen_xo, screen_yo; /* centers image in window */

  int flutter_horiz_desync;
  //int flutter_tint;

  struct timeval last_display_time;

  /* Add hash (in the radio sense, not the programming sense.) These
     are the small white streaks that appear in quasi-regular patterns
     all over the screen when someone is running the vacuum cleaner or
     the blender. We also set shrinkpulse for one period which
     squishes the image horizontally to simulate the temporary line
     voltate drop when someone turns on a big motor */
  double hashnoise_rpm;
  int hashnoise_counter;
  int hashnoise_times[ANALOGTV_V];
  int hashnoise_signal[ANALOGTV_V];
  int hashnoise_on;
  int hashnoise_enable;
  int shrinkpulse;

  float crtload[ANALOGTV_V];

  unsigned int intensity_values[ANALOGTV_CV_MAX];

  unsigned long colors[256];
  int cmap_y_levels;
  int cmap_i_levels;
  int cmap_q_levels;

  float tint_i, tint_q;
  
  int cur_hsync;
  int line_hsync[ANALOGTV_V];
  int cur_vsync;
  double cb_phase[4];
  double line_cb_phase[ANALOGTV_V][4];

  int channel_change_cycles;
  double rx_signal_level;
  float *rx_signal;

  struct {
    int index;
    double value;
  } leveltable[ANALOGTV_MAX_LINEHEIGHT+1][ANALOGTV_MAX_LINEHEIGHT+1];

  /* Only valid during draw. */
  unsigned random0, random1;
  double noiselevel;
  const analogtv_reception *const *recs;
  unsigned rec_count;

  float *signal_subtotals;

  float puheight;
} analogtv;


analogtv *analogtv_allocate(int outbuffer_width, int outbuffer_height);
analogtv_input *analogtv_input_allocate(void);


void analogtv_set_defaults(analogtv *it);
void analogtv_setup_frame(analogtv *it);
void analogtv_setup_sync(analogtv_input *input, int do_cb, int do_ssavi);
void analogtv_draw(analogtv *it, double noiselevel,
                   const analogtv_reception *const *recs, unsigned rec_count);

int analogtv_load_ximage(analogtv *it, analogtv_input *input,
                         const cv::Mat4b& pic_im, const cv::Mat4b& mask_im,
                         int xoff, int yoff, int width, int height);

void analogtv_reception_update(analogtv_reception *inp);


/* Functions for rendering content into an analogtv_input */

void analogtv_lcp_to_ntsc(double luma, double chroma, double phase,
                          int ntsc[4]);


void analogtv_draw_solid(analogtv_input *input,
                         int left, int right, int top, int bot,
                         int ntsc[4]);

void analogtv_draw_solid_rel_lcp(analogtv_input *input,
                                 double left, double right,
                                 double top, double bot,
                                 double luma, double chroma, double phase);

/* Brightness: useful range is around -75 to 100.
   Contrast:   useful range is around 0 - 500.
   Color:      useful range is around +/- 500.
   Tint:       range is mod 360.

   The values in the 'analogtv' struct are the resource divided by 100.0,
   except for tint, which is exact.
 */

#define ANALOGTV_DEFAULTS \
  "*TVColor:         70", \
  "*TVTint:           5",  \
  "*TVBrightness:     2",  \
  "*TVContrast:     150", \
  "*Background:      Black", \
  "*geometry:	     800x600", \
  "*fpsSolid:	     True", \
  "*lowrez:	     True", \


