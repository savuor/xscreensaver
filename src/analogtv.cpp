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

/*

  This is the code for implementing something that looks like a conventional
  analog TV set. It simulates the following characteristics of standard
  televisions:

  - Realistic rendering of a composite video signal
  - Compression & brightening on the right, as the scan gets truncated
    because of saturation in the flyback transformer
  - Blooming of the picture dependent on brightness
  - Overscan, cutting off a few pixels on the left side.
  - Colored text in mixed graphics/text modes

  It's amazing how much it makes your high-end monitor look like at large
  late-70s TV. All you need is to put a big "Solid State" logo in curly script
  on it and you'd be set.

  In DirectColor or TrueColor modes, it generates pixel values
  directly from RGB values it calculates across each scan line. In
  PseudoColor mode, it consider each possible pattern of 5 preceding
  bit values in each possible position modulo 4 and allocates a color
  for each. A few things, like the brightening on the right side as
  the horizontal trace slows down, aren't done in PseudoColor.

  I originally wrote it for the Apple ][ emulator, and generalized it
  here for use with a rewrite of xteevee and possibly others.

  A maxim of technology is that failures reveal underlying mechanism.
  A good way to learn how something works is to push it to failure.
  The way it fails will usually tell you a lot about how it works. The
  corollary for this piece of software is that in order to emulate
  realistic failures of a TV set, it has to work just like a TV set.
  So there is lots of DSP-style emulation of analog circuitry for
  things like color decoding, H and V sync following, and more. In
  2003, computers are just fast enough to do this at television signal
  rates. We use a 14 MHz sample rate here, so we can do on the order
  of a couple hundred instructions per sample and keep a good frame
  rate.

  Trevor Blackwell <tlb@tlb.org>
*/

/*
  2014-04-20, Dave Odell <dmo2118@gmail.com>:
  API change: Folded analogtv_init_signal and *_add_signal into *_draw().
  Added SMP support.
  Replaced doubles with floats, including constants and transcendental functions.
  Fixed a bug or two.
*/

/* 2015-02-27, Tomasz Sulej <tomeksul@gmail.com>:
   - tint_control variable is used now
   - removed unusable hashnoise code
 */

/*
  2016-10-09, Dave Odell <dmo2118@gmail.com>:
  Updated for new xshm.c.
*/

#include "precomp.hpp"

#include "fixed-funcs.hpp"
#include "analogtv.hpp"
#include "yarandom.hpp"
#include "utils.hpp"


/* #define DEBUG 1 */

#if defined(DEBUG) && (defined(__linux) || defined(__FreeBSD__))
/* only works on linux + freebsd */
#include <machine/cpufunc.h>

#define DTIME_DECL u_int64_t dtimes[100]; int n_dtimes
#define DTIME_START do {n_dtimes=0; dtimes[n_dtimes++]=rdtsc(); } while (0)
#define DTIME dtimes[n_dtimes++]=rdtsc()
#define DTIME_SHOW(DIV) \
do { \
  double _dtime_div=(DIV); \
  printf("time/%.1f: ",_dtime_div); \
  for (i=1; i<n_dtimes; i++) \
    printf(" %0.9f",(dtimes[i]-dtimes[i-1])* 1e-9 / _dtime_div); \
  printf("\n"); \
} while (0)

#else

#define DTIME_DECL
#define DTIME_START  do { } while (0)
#define DTIME  do { } while (0)
#define DTIME_SHOW(DIV)  do { } while (0)

#endif


#define FASTRND_A 1103515245
#define FASTRND_C 12345
#define FASTRND (fastrnd = fastrnd*FASTRND_A+FASTRND_C)

static void analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                                 int start, int end, struct analogtv_yiq_s *it_yiq);

static float puramp(const analogtv *it, float tc, float start, float over)
{
  float pt = it->powerup - start;
  if (pt<0.0f) return 0.0f;
  if (pt>900.0f || pt/tc>8.0f) return 1.0f;

  float ret=(1.0f-expf(-pt/tc))*over;
  if (ret>1.0f) return 1.0f;
  return ret*ret;
}

/*
  There are actual standards for TV signals: NTSC and RS-170A describe the
  system used in the US and Japan. Europe has slightly different systems, but
  not different enough to make substantially different screensaver displays.
  Sadly, the standards bodies don't do anything so useful as publish the spec on
  the web. Best bets are:

    http://www.ee.washington.edu/conselec/CE/kuhn/ntsc/95x4.htm
    http://www.ntsc-tv.com/ntsc-index-02.htm

  In DirectColor or TrueColor modes, it generates pixel values directly from RGB
  values it calculates across each scan line. In PseudoColor mode, it consider
  each possible pattern of 5 preceding bit values in each possible position
  modulo 4 and allocates a color for each. A few things, like the brightening on
  the right side as the horizontal trace slows down, aren't done in PseudoColor.

  I'd like to add a bit of visible retrace, but it conflicts with being able to
  bitcopy the image when fast scrolling. After another couple of CPU
  generations, we could probably regenerate the whole image from scratch every
  time. On a P4 2 GHz it can manage this fine for blinking text, but scrolling
  looks too slow.
*/

static const double float_low8_ofs=8388608.0;

void
analogtv_set_defaults(analogtv *it)
{
  // values taken from analogtv-cli
  it->tint_control = 5; // 270 (almost) fixes the color issue!
  it->color_control = 70/100.0;
  it->brightness_control = 2 / 100.0;
  it->contrast_control = 150 / 100.0;
  it->height_control = 1.0;
  it->width_control = 1.0;
  it->squish_control = 0.0;
  it->powerup=1000.0;

  it->hashnoise_rpm=0;
  it->hashnoise_on=0;
  it->hashnoise_enable=1;

  it->horiz_desync=ya_frand(10.0)-5.0;
  it->squeezebottom=ya_frand(5.0)-1.0;

  //TODO: make logs
#ifdef DEBUG
  printf("analogtv: prefix=%s\n",prefix);
  printf("  use: color=1\n");
  printf("  controls: tint=%g color=%g brightness=%g contrast=%g\n",
         it->tint_control, it->color_control, it->brightness_control,
         it->contrast_control);
/*  printf("  freq_error %g: %g %d\n",
         it->freq_error, it->freq_error_inc, it->flutter_tint); */
  printf("  desync: %g %d\n",
         it->horiz_desync, it->flutter_horiz_desync);
  printf("  hashnoise rpm: %g\n",
         it->hashnoise_rpm);
  printf("  size: %d %d  %d %d  xrepl=%d\n",
         it->usewidth, it->useheight,
         it->screen_xo, it->screen_yo, it->xrepl);

  printf("    ANALOGTV_V=%d\n",ANALOGTV_V);
  printf("    ANALOGTV_TOP=%d\n",ANALOGTV_TOP);
  printf("    ANALOGTV_VISLINES=%d\n",ANALOGTV_VISLINES);
  printf("    ANALOGTV_BOT=%d\n",ANALOGTV_BOT);
  printf("    ANALOGTV_H=%d\n",ANALOGTV_H);
  printf("    ANALOGTV_SYNC_START=%d\n",ANALOGTV_SYNC_START);
  printf("    ANALOGTV_BP_START=%d\n",ANALOGTV_BP_START);
  printf("    ANALOGTV_CB_START=%d\n",ANALOGTV_CB_START);
  printf("    ANALOGTV_PIC_START=%d\n",ANALOGTV_PIC_START);
  printf("    ANALOGTV_PIC_LEN=%d\n",ANALOGTV_PIC_LEN);
  printf("    ANALOGTV_FP_START=%d\n",ANALOGTV_FP_START);
  printf("    ANALOGTV_PIC_END=%d\n",ANALOGTV_PIC_END);
  printf("    ANALOGTV_HASHNOISE_LEN=%d\n",ANALOGTV_HASHNOISE_LEN);

#endif

}


static void
analogtv_configure(analogtv *it)
{
  int oldwidth=it->usewidth;
  int oldheight=it->useheight;
  int wlim,hlim,height_diff;

  /* If the window is very small, don't let the image we draw get lower
     than the actual TV resolution (266x200.)

     If the aspect ratio of the window is close to a 4:3 or 16:9 ratio --
     or if it is a completely weird aspect ratio --
     then scale the image to exactly fill the window.

     Otherwise, center the image either horizontally or vertically,
     letterboxing or pillarboxing (but not both).

     If it's very close (2.5%) to a multiple of VISLINES, make it exact
     For example, it maps 1024 => 1000.
   */
  float percent = 0.15;
  float min_ratio =  4.0 / 3.0 * (1 - percent);
  float max_ratio = 16.0 / 9.0 * (1 + percent);
  float crazy_min_ratio = 10;
  float crazy_max_ratio = 1/crazy_min_ratio;
  float ratio;
  float height_snap=0.025;

  hlim = it->outbuffer_height;
  wlim = it->outbuffer_width;
  ratio = wlim / (float) hlim;

// NO_CONSTRAIN_RATIO is defined
//#if defined(HAVE_MOBILE) || defined(NO_CONSTRAIN_RATIO)
  /* Fill the whole iPhone screen, even though that distorts the image. */
  min_ratio = 0;
  max_ratio = 10;
//#endif

  std::string debugPrint1 = std::to_string(wlim) + "x" + std::to_string(hlim);
  std::string debugPrint2 = " in " + std::to_string(it->outbuffer_width) + "x" + std::to_string(it->outbuffer_height);
  std::string debugPrint3 = " (" + std::to_string(min_ratio) + " < " + std::to_string(ratio) + " < " + std::to_string(max_ratio) + ")";
  if (wlim < 266 || hlim < 200)
    {
      wlim = 266;
      hlim = 200;
      // debug mode
      Log::write(3, "size: minimal: " + debugPrint1 + debugPrint2 + debugPrint3);
    }
  else if (ratio > min_ratio && ratio < max_ratio)
    {
      // debug mode
      Log::write(3, "size: close enough:" + debugPrint1 + debugPrint3);
    }
  else if (ratio >= max_ratio)
    {
      wlim = hlim*max_ratio;
      // debug mode
      Log::write(3, "size: center H: " + debugPrint1 + debugPrint2 + debugPrint3);
    }
  else /* ratio <= min_ratio */
    {
      hlim = wlim/min_ratio;
      // debug mode
      Log::write(3, "size: center V: " + debugPrint1 + debugPrint2 + debugPrint3);
    }

  if (ratio < crazy_min_ratio || ratio > crazy_max_ratio)
    {
      if (ratio < crazy_min_ratio)
        hlim = it->outbuffer_height;
      else
        wlim = it->outbuffer_width;
      // debug mode
      Log::write(3, "size: aspect: " + debugPrint1 + debugPrint2 + debugPrint3);
    }

  height_diff = ((hlim + ANALOGTV_VISLINES/2) % ANALOGTV_VISLINES) - ANALOGTV_VISLINES/2;
  if (height_diff != 0 && abs(height_diff) < hlim * height_snap)
    {
      hlim -= height_diff;
    }

  /* Most times this doesn't change */
  if (wlim != oldwidth || hlim != oldheight)
  {
    it->usewidth=wlim;
    it->useheight=hlim;

    //TODO: fix this behavior
    it->xrepl=1+it->usewidth/640;
    if (it->xrepl>2) it->xrepl=2;
    it->subwidth=it->usewidth/it->xrepl;

    it->image = cv::Mat4b(it->useheight, it->usewidth);
  }

  it->screen_xo = ( it->outbuffer_width  - it->usewidth  )/2;
  it->screen_yo = ( it->outbuffer_height - it->useheight )/2;
}

typedef struct analogtv_thread_s
{
  analogtv *it;
  unsigned thread_id;
  size_t signal_start, signal_end;
} analogtv_thread;

static int analogtv_thread_create(void *thread_raw, struct threadpool *threads,
                                  unsigned thread_id)
{
  analogtv_thread *thread = (analogtv_thread *)thread_raw;

  thread->it = GET_PARENT_OBJ(analogtv, threads, threads);
  thread->thread_id = thread_id;

  uint32_t align = ~(32 - 1);

  thread->signal_start = (ANALOGTV_SIGNAL_LEN * (thread_id) / threads->count) & align;
  thread->signal_end = thread_id + 1 == threads->count ?
                       ANALOGTV_SIGNAL_LEN :
                       ((ANALOGTV_SIGNAL_LEN * (thread_id + 1) / threads->count) & align);

  return 0;
}

static void analogtv_thread_destroy(void *thread_raw)
{
}

analogtv * analogtv_allocate(int outbuffer_width, int outbuffer_height)
{
  static const struct threadpool_class cls = {
    sizeof(analogtv_thread),
    analogtv_thread_create,
    analogtv_thread_destroy
  };

  analogtv *it=NULL;
  int i;
  const size_t rx_signal_len = ANALOGTV_SIGNAL_LEN + 2*ANALOGTV_H;

  it=(analogtv *)calloc(1,sizeof(analogtv));
  if (!it) return 0;
  it->threads.count=0;


  //TODO: vector<float>
  it->rx_signal=NULL;
  if (thread_malloc((void **)&it->rx_signal, 
                    sizeof(it->rx_signal[0]) * rx_signal_len))
    goto fail;

  if (threadpool_create(&it->threads, &cls, hardware_concurrency()))
    goto fail;

  assert(it->threads.count);

  it->shrinkpulse=-1;

  it->outbuffer_width  = outbuffer_width;
  it->outbuffer_height = outbuffer_height;

  for (i = 0; i < ANALOGTV_CV_MAX; i++)
  {
    int intensity = pow(i / 256.0, 0.8) * 65535.0; /* gamma correction */
    if (intensity > 65535)
      intensity = 65535;
    it->intensity_values[i] = intensity >> 8;
  }

  analogtv_configure(it);

  return it;

 fail:
  if (it) {
    if(it->threads.count)
      threadpool_destroy(&it->threads);
    thread_free(it->rx_signal);
    free(it);
  }
  return NULL;
}




/*
  First generate the I and Q reference signals, which we'll multiply
  the input signal by to accomplish the demodulation. Normally they
  are shifted 33 degrees from the colorburst. I think this was convenient
  for inductor-capacitor-vacuum tube implementation.

  The tint control, FWIW, just adds a phase shift to the chroma signal,
  and the color control controls the amplitude.

  In text modes (colormode==0) the system disabled the color burst, and no
  color was detected by the monitor.

  freq_error gives a mismatch between the built-in oscillator and the
  TV's colorbust. Some II Plus machines seemed to occasionally get
  instability problems -- the crystal oscillator was a single
  transistor if I remember correctly -- and the frequency would vary
  enough that the tint would change across the width of the screen.
  The left side would be in correct tint because it had just gotten
  resynchronized with the color burst.

  If we're using a colormap, set it up.
*/
// analogtv_set_demod(analogtv *it) was removed

#if 0
unsigned int
analogtv_line_signature(analogtv_input *input, int lineno)
{
  int i;
  char *origsignal=&input->signal[(lineno+input->vsync)
                                  %ANALOGTV_V][input->line_hsync[lineno]];
  unsigned int hash=0;

  /* probably lame */
  for (i=0; i<ANALOGTV_PIC_LEN; i++) {
    int c=origsignal[i];
    hash = hash + (hash<<17) + c;
  }

  hash += input->line_hsync[lineno];
  hash ^= hash >> 2;
  /*
  hash += input->hashnoise_times[lineno];
  hash ^= hash >> 2;
  */

  return hash;
}
#endif


/* Here we model the analog circuitry of an NTSC television.
   Basically, it splits the signal into 3 signals: Y, I and Q. Y
   corresponds to luminance, and you get it by low-pass filtering the
   input signal to below 3.57 MHz.

   I and Q are the in-phase and quadrature components of the 3.57 MHz
   subcarrier. We get them by multiplying by cos(3.57 MHz*t) and
   sin(3.57 MHz*t), and low-pass filtering. Because the eye has less
   resolution in some colors than others, the I component gets
   low-pass filtered at 1.5 MHz and the Q at 0.5 MHz. The I component
   is approximately orange-blue, and Q is roughly purple-green. See
   http://www.ntsc-tv.com for details.

   We actually do an awful lot to the signal here. I suspect it would
   make sense to wrap them all up together by calculating impulse
   response and doing FFT convolutions.

*/

static void
analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                     int start, int end, struct analogtv_yiq_s *it_yiq)
{
  enum {MAXDELAY=32};
  int i;
  const float *sp;
  int phasecorr=(signal-it->rx_signal)&3;
  struct analogtv_yiq_s *yiq;
  int colormode;
  float agclevel=it->agclevel;
  float brightadd=it->brightness_control*100.0 - ANALOGTV_BLACK_LEVEL;
  float delay[MAXDELAY+ANALOGTV_PIC_LEN], *dp;
  float multiq2[4];

  {

    double cb_i=(it->line_cb_phase[lineno][(2+phasecorr)&3]-
                 it->line_cb_phase[lineno][(0+phasecorr)&3])/16.0;
    double cb_q=(it->line_cb_phase[lineno][(3+phasecorr)&3]-
                 it->line_cb_phase[lineno][(1+phasecorr)&3])/16.0;

    colormode = (cb_i * cb_i + cb_q * cb_q) > 2.8;

    if (colormode) {
      multiq2[0] = (cb_i*it->tint_i - cb_q*it->tint_q) * it->color_control;
      multiq2[1] = (cb_q*it->tint_i + cb_i*it->tint_q) * it->color_control;
      multiq2[2]=-multiq2[0];
      multiq2[3]=-multiq2[1];
    }
  }

#if 0
  if (lineno==100) {
    printf("multiq = [%0.3f %0.3f %0.3f %0.3f] ",
           it->multiq[60],it->multiq[61],it->multiq[62],it->multiq[63]);
    printf("it->line_cb_phase = [%0.3f %0.3f %0.3f %0.3f]\n",
           it->line_cb_phase[lineno][0],it->line_cb_phase[lineno][1],
           it->line_cb_phase[lineno][2],it->line_cb_phase[lineno][3]);
    printf("multiq2 = [%0.3f %0.3f %0.3f %0.3f]\n",
           multiq2[0],multiq2[1],multiq2[2],multiq2[3]);
  }
#endif

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<5; i++) dp[i]=0.0f;

  assert(start>=0);
  assert(end < ANALOGTV_PIC_LEN+10);

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<24; i++) dp[i]=0.0;
  for (i=start, yiq=it_yiq+start, sp=signal+start;
       i<end;
       i++, dp--, yiq++, sp++) {

    /* Now filter them. These are infinite impulse response filters
       calculated by the script at
       http://www-users.cs.york.ac.uk/~fisher/mkfilter. This is
       fixed-point integer DSP, son. No place for wimps. We do it in
       integer because you can count on integer being faster on most
       CPUs. We care about speed because we need to recalculate every
       time we blink text, and when we spew random bytes into screen
       memory. This is roughly 16.16 fixed point arithmetic, but we
       scale some filter values up by a few bits to avoid some nasty
       precision errors. */

    /* Filter Y with a 4-pole low-pass Butterworth filter at 3.5 MHz
       with an extra zero at 3.5 MHz, from
       mkfilter -Bu -Lp -o 4 -a 2.1428571429e-01 0 -Z 2.5e-01 -l
       Delay about 2 */

    dp[0] = sp[0] * 0.0469904257251935f * agclevel;
    dp[8] = (+1.0f*(dp[6]+dp[0])
             +4.0f*(dp[5]+dp[1])
             +7.0f*(dp[4]+dp[2])
             +8.0f*(dp[3])
             -0.0176648f*dp[12]
             -0.4860288f*dp[10]);
    yiq->y = dp[8] + brightadd;
  }

  if (colormode) {
    dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
    for (i=0; i<27; i++) dp[i]=0.0;

    for (i=start, yiq=it_yiq+start, sp=signal+start;
         i<end;
         i++, dp--, yiq++, sp++) {
      float sig=*sp;

      /* Filter I and Q with a 3-pole low-pass Butterworth filter at
         1.5 MHz with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 -Z 2.5000000000e-01 -l
         Delay about 3.
      */

      dp[0] = sig*multiq2[i&3] * 0.0833333333333f;
      yiq->i=dp[8] = (dp[5] + dp[0]
                      +3.0f*(dp[4] + dp[1])
                      +4.0f*(dp[3] + dp[2])
                      -0.3333333333f * dp[10]);

      dp[16] = sig*multiq2[(i+3)&3] * 0.0833333333333f;
      yiq->q=dp[24] = (dp[16+5] + dp[16+0]
                       +3.0f*(dp[16+4] + dp[16+1])
                       +4.0f*(dp[16+3] + dp[16+2])
                       -0.3333333333f * dp[24+2]);
    }
  } else {
    for (i=start, yiq=it_yiq+start; i<end; i++, yiq++) {
      yiq->i = yiq->q = 0.0f;
    }
  }
}


void
analogtv_setup_frame(analogtv *it)
{
  /*  int i,x,y;*/

  it->redraw_all=0;

  if (it->flutter_horiz_desync) {
    /* Horizontal sync during vertical sync instability. */
    it->horiz_desync += -0.10*(it->horiz_desync-3.0) +
      ((int)(ya_random()&0xff)-0x80) *
      ((int)(ya_random()&0xff)-0x80) *
      ((int)(ya_random()&0xff)-0x80) * 0.000001;
  }

  /* it wasn't used
  for (i=0; i<ANALOGTV_V; i++) {
    it->hashnoise_times[i]=0;
  }
  */

  /* let's leave it to process shrinkpulse */
  if (it->hashnoise_enable && !it->hashnoise_on) {
    if (ya_random()%10000==0) {
      it->hashnoise_on=1;
      it->shrinkpulse=ya_random()%ANALOGTV_V;
    }
  }
  if (ya_random()%1000==0) {
    it->hashnoise_on=0;
  }

#if 0  /* never used */
  if (it->hashnoise_on) {
    it->hashnoise_rpm += (15000.0 - it->hashnoise_rpm)*0.05 +
      ((int)(ya_random()%2000)-1000)*0.1;
  } else {
    it->hashnoise_rpm -= 100 + 0.01*it->hashnoise_rpm;
    if (it->hashnoise_rpm<0.0) it->hashnoise_rpm=0.0;
  }
  if (it->hashnoise_rpm > 0.0) {
    int hni;
    double hni_double;
    int hnc=it->hashnoise_counter; /* in 24.8 format */

    /* Convert rpm of a 16-pole motor into dots in 24.8 format */
    hni_double = ANALOGTV_V * ANALOGTV_H * 256.0 /
                (it->hashnoise_rpm * 16.0 / 60.0 / 60.0);
    hni = (hni_double <= INT_MAX) ? (int)hni_double : INT_MAX;

    while (hnc < (ANALOGTV_V * ANALOGTV_H)<<8) {
      y=(hnc>>8)/ANALOGTV_H;
      x=(hnc>>8)%ANALOGTV_H;

      if (x>0 && x<ANALOGTV_H - ANALOGTV_HASHNOISE_LEN) {
        it->hashnoise_times[y]=x;
      }
      /* hnc += hni + (int)(ya_random()%65536)-32768; */
      {
        hnc += (int)(ya_random()%65536)-32768;
        if ((hnc >= 0) && (INT_MAX - hnc < hni)) break;
        hnc += hni;
      }
    }
  }
#endif /* 0 */

/*    hnc -= (ANALOGTV_V * ANALOGTV_H)<<8;*/


  if (it->rx_signal_level != 0.0)
    it->agclevel = 1.0/it->rx_signal_level;

//TODO: make logs
#ifdef DEBUG2
  printf("filter: ");
  for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
    printf(" %0.3f",it->ghostfir[i]);
  }
  printf(" siglevel=%g agc=%g\n", siglevel, it->agclevel);
#endif
}

void
analogtv_setup_sync(analogtv_input *input, int do_cb, int do_ssavi)
{
  int i,lineno,vsync;
  signed char *sig;

  int synclevel = do_ssavi ? ANALOGTV_WHITE_LEVEL : ANALOGTV_SYNC_LEVEL;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {
    vsync=lineno>=3 && lineno<7;

    sig=input->signal[lineno];

    i=ANALOGTV_SYNC_START;
    if (vsync) {
      while (i<ANALOGTV_BP_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_H) sig[i++]=synclevel;
    } else {
      while (i<ANALOGTV_BP_START) sig[i++]=synclevel;
      while (i<ANALOGTV_PIC_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_FP_START) sig[i++]=ANALOGTV_BLACK_LEVEL;
    }
    while (i<ANALOGTV_H) sig[i++]=ANALOGTV_BLANK_LEVEL;

    if (do_cb) {
      /* 9 cycles of colorburst */
      for (i=ANALOGTV_CB_START; i<ANALOGTV_CB_START+36*ANALOGTV_SCALE; i+=4*ANALOGTV_SCALE) {
        sig[i+1] += ANALOGTV_CB_LEVEL;
        sig[i+3] -= ANALOGTV_CB_LEVEL;
      }
    }
  }
}

static void
analogtv_sync(analogtv *it)
{
  int cur_hsync=it->cur_hsync;
  int cur_vsync=it->cur_vsync;
  int lineno = 0;
  int i,j;
  float osc,filt;
  float *sp;
  float cbfc=1.0f/128.0f;

/*  sp = it->rx_signal + lineno*ANALOGTV_H + cur_hsync;*/
  for (i=-32*ANALOGTV_SCALE; i<32*ANALOGTV_SCALE; i++) {
    lineno = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;
    sp = it->rx_signal + lineno*ANALOGTV_H;
    filt=0.0f;
    for (j=0; j<ANALOGTV_H; j+=ANALOGTV_H/(16*ANALOGTV_SCALE)) {
      filt += sp[j];
    }
    filt *= it->agclevel;

    osc = (float)(ANALOGTV_V+i)/(float)ANALOGTV_V;

    if (osc >= 1.05f+0.0002f * filt) break;
  }
  cur_vsync = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {

    if (lineno>5*ANALOGTV_SCALE && lineno<ANALOGTV_V-3*ANALOGTV_SCALE) { /* ignore vsync interval */
      unsigned lineno2 = (lineno + cur_vsync + ANALOGTV_V)%ANALOGTV_V;
      if (!lineno2) lineno2 = ANALOGTV_V;
      sp = it->rx_signal + lineno2*ANALOGTV_H + cur_hsync;
      for (i=-8*ANALOGTV_SCALE; i<8*ANALOGTV_SCALE; i++) {
        osc = (float)(ANALOGTV_H+i)/(float)ANALOGTV_H;
        filt=(sp[i-3]+sp[i-2]+sp[i-1]+sp[i]) * it->agclevel;

        if (osc >= 1.005f + 0.0001f*filt) break;
      }
      cur_hsync = (cur_hsync + i + ANALOGTV_H) % ANALOGTV_H;
    }

    it->line_hsync[lineno]=(cur_hsync + ANALOGTV_PIC_START +
                            ANALOGTV_H) % ANALOGTV_H;

    /* Now look for the colorburst, which is a few cycles after the H
       sync pulse, and store its phase.
       The colorburst is 9 cycles long, and we look at the middle 5
       cycles.
    */

    if (lineno>15*ANALOGTV_SCALE) {
      sp = it->rx_signal + lineno*ANALOGTV_H + (cur_hsync&~3);
      for (i=ANALOGTV_CB_START+8*ANALOGTV_SCALE; i<ANALOGTV_CB_START+(36-8)*ANALOGTV_SCALE; i++) {
        it->cb_phase[i&3] = it->cb_phase[i&3]*(1.0f-cbfc) +
          sp[i]*it->agclevel*cbfc;
      }
    }

    {
      float tot=0.1f;
      float cbgain;

      for (i=0; i<4; i++) {
        tot += it->cb_phase[i]*it->cb_phase[i];
      }
      cbgain = 32.0f/sqrtf(tot);

      for (i=0; i<4; i++) {
        it->line_cb_phase[lineno][i]=it->cb_phase[i]*cbgain;
      }
    }

#ifdef DEBUG
    if (0) printf("hs=%d cb=[%0.3f %0.3f %0.3f %0.3f]\n",
                  cur_hsync,
                  it->cb_phase[0], it->cb_phase[1],
                  it->cb_phase[2], it->cb_phase[3]);
#endif

    /* if (ya_random()%2000==0) cur_hsync=ya_random()%ANALOGTV_H; */
  }

  it->cur_hsync = cur_hsync;
  it->cur_vsync = cur_vsync;
}

// static double
// analogtv_levelmult(const analogtv *it, int level)
// {
//   static const double levelfac[3]={-7.5, 5.5, 24.5};
//   return (40.0 + levelfac[level]*puramp(it, 3.0, 6.0, 1.0))/256.0;
// }

// static int
// analogtv_level(const analogtv *it, int y, int ytop, int ybot)
// {
//   int level;
//   if (ybot-ytop>=7) {
//     if (y==ytop || y==ybot-1) level=0;
//     else if (y==ytop+1 || y==ybot-2) level=1;
//     else level=2;
//   }
//   else if (ybot-ytop>=5) {
//     if (y==ytop || y==ybot-1) level=0;
//     else level=2;
//   }
//   else if (ybot-ytop>=3) {
//     if (y==ytop) level=0;
//     else level=2;
//   }
//   else {
//     level=2;
//   }
//   return level;
// }

/*
  The point of this stuff is to ensure that when useheight is not a
  multiple of VISLINES so that TV scan lines map to different numbers
  of vertical screen pixels, the total brightness of each scan line
  remains the same.
  ANALOGTV_MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which
  it interpolates extra black lines.
 */

static void
analogtv_setup_levels(analogtv *it, double avgheight)
{
  static const double levelfac[3] = {-7.5, 5.5, 24.5};

  for (int height = 0; height < avgheight + 2.0 && height <= ANALOGTV_MAX_LINEHEIGHT; height++)
  {
    for (int i = 0; i < height; i++)
    {
      it->leveltable[height][i].index = 2;
    }
    
    if (avgheight >= 3)
    {
      it->leveltable[height][0].index=0;
    }

    if (avgheight >= 5)
    {
      if (height >= 1)
        it->leveltable[height][height-1].index=0;
    }
    if (avgheight >= 7)
    {
      it->leveltable[height][1].index=1;
      if (height >= 2)
        it->leveltable[height][height-2].index=1;
    }

    for (int i = 0; i<height; i++)
    {
      it->leveltable[height][i].value = 
        (40.0 + levelfac[it->leveltable[height][i].index]*puramp(it, 3.0, 6.0, 1.0)) / 256.0;
    }

  }
}

static void rnd_combine(unsigned *a0, unsigned *c0, unsigned a1, unsigned c1)
{
  *a0 = (*a0 * a1) & 0xffffffffu;
  *c0 = (c1 + a1 * *c0) & 0xffffffffu;
}

static void rnd_seek_ac(unsigned *a, unsigned *c, unsigned dist)
{
  unsigned int a1 = *a, c1 = *c;
  *a = 1, *c = 0;

  while(dist)
  {
    if(dist & 1)
      rnd_combine(a, c, a1, c1);
    dist >>= 1;
    rnd_combine(&a1, &c1, a1, c1);
  }
}

static unsigned int rnd_seek(unsigned a, unsigned c, unsigned rnd, unsigned dist)
{
  rnd_seek_ac(&a, &c, dist);
  return a * rnd + c;
}

static void analogtv_init_signal(const analogtv *it, double noiselevel, unsigned start, unsigned end)
{
  unsigned int fastrnd = rnd_seek(FASTRND_A, FASTRND_C, it->random0, start);
  unsigned int fastrnd_offset;
  float nm1, nm2;
  float noisemul = sqrt(noiselevel*150)/(float)0x7fffffff;

  fastrnd_offset = fastrnd - 0x7fffffff;
  nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;

  for (uint32_t i = start; i < end; i++)
  {
    nm2 = nm1;
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;
    fastrnd_offset = fastrnd - 0x7fffffff;
    nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;
    it->rx_signal[i] = nm1 * nm2;
  }
}

static void analogtv_add_signal(const analogtv *it, const analogtv_reception *rec, unsigned start, unsigned end, int ec)
{
  analogtv_input *inp=rec->input;

  signed char *s=&inp->signal[0][0] + ((start + (unsigned)rec->ofs) % ANALOGTV_SIGNAL_LEN);

  float level=rec->level;
  
  unsigned int fastrnd=rnd_seek(FASTRND_A, FASTRND_C, it->random1, start);


  const float noise_decay = 0.99995f;
  float noise_ampl = 1.3f * powf(noise_decay, start);

  /* assert((se-ss)%4==0 && (se-s)%4==0); */
  ec = std::min(ec, (int)end);
  /* Sometimes start > ec. */


  float *p = it->rx_signal + start;
  for (int i = start; i < ec; i++)
  {
    /* Do a big noisy transition. We can make the transition noise of
       high constant strength regardless of signal strength.

       There are two separate state machines. here, One is the noise
       process and the other is the

       We don't bother with the FIR filter here
    */

    float sig0=(float)s[0];
    unsigned int fastrnd_offset = fastrnd - 0x7fffffff;
    float noise = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * (50.0f/(float)0x7fffffff);
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;

    p[0] += sig0 * level * (1.0f - noise_ampl) + noise * noise_ampl;

    noise_ampl *= noise_decay;

    p++;
    s++;
    if (s >= &inp->signal[0][0] + ANALOGTV_SIGNAL_LEN)
    {
      s = &inp->signal[0][0];
    }
  }

  float dp[5];

  dp[0]=0.0;
  
  signed char *s2 = s;
  for (int i=1; i<5; i++)
  {
    s2 -= 4;
    if (s2 < &inp->signal[0][0])
      s2 += ANALOGTV_SIGNAL_LEN;
    dp[i] = (float)((int)s2[0]+(int)s2[1]+(int)s2[2]+(int)s2[3]);
  }

  assert(p <= it->rx_signal + end);
  assert(!((it->rx_signal + end - p) % 4));

  float hfloss=rec->hfloss;
  while (p != it->rx_signal + end)
  {
    float sig0,sig1,sig2,sig3,sigr;

    sig0=(float)s[0];
    sig1=(float)s[1];
    sig2=(float)s[2];
    sig3=(float)s[3];

    dp[0]=sig0+sig1+sig2+sig3;

    /* Get the video out signal, and add some ghosting, typical of RF
       monitor cables. This corresponds to a pretty long cable, but
       looks right to me.
    */

    sigr=(dp[1]*rec->ghostfir[0] + dp[2]*rec->ghostfir[1] +
          dp[3]*rec->ghostfir[2] + dp[4]*rec->ghostfir[3]);
    dp[4]=dp[3]; dp[3]=dp[2]; dp[2]=dp[1]; dp[1]=dp[0];

    p[0] += (sig0+sigr + sig2*hfloss) * level;
    p[1] += (sig1+sigr + sig3*hfloss) * level;
    p[2] += (sig2+sigr + sig0*hfloss) * level;
    p[3] += (sig3+sigr + sig1*hfloss) * level;

    p += 4;
    s += 4;
    if (s >= &inp->signal[0][0] + ANALOGTV_SIGNAL_LEN)
    {
      s = &inp->signal[0][0] + (s - &inp->signal[0][0] - ANALOGTV_SIGNAL_LEN);
    }
  }

  assert(p == it->rx_signal + end);
}

static void analogtv_thread_add_signals(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  const analogtv *it = thread->it;

  unsigned start = thread->signal_start;
  while(start != thread->signal_end)
  {
    /* Work on 8 KB blocks; these should fit in L1. */
    /* (Though it doesn't seem to help much on my system.) */
    unsigned end = start + 2048;
    if(end > thread->signal_end)
      end = thread->signal_end;

    analogtv_init_signal (it, it->noiselevel, start, end);

    for (uint32_t i = 0; i != it->rec_count; ++i)
    {
      analogtv_add_signal (it, it->recs[i], start, end,
                           !i ? it->channel_change_cycles : 0);
    }

    start = end;
  }
}

static int analogtv_get_line(const analogtv *it, int lineno, int *slineno,
                             int *ytop, int *ybot, unsigned *signal_offset)
{
  *slineno=lineno-ANALOGTV_TOP;
  *ytop=(int)((*slineno*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
  *ybot=(int)(((*slineno+1)*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
#if 0
  int linesig=analogtv_line_signature(input,lineno)
    + it->hashnoise_times[lineno];
#endif
  *signal_offset = ((lineno+it->cur_vsync+ANALOGTV_V) % ANALOGTV_V) * ANALOGTV_H +
                    it->line_hsync[lineno];

  if (*ytop==*ybot) return 0;
  if (*ybot<0 || *ytop>it->useheight) return 0;
  if (*ytop<0) *ytop=0;
  if (*ybot>it->useheight) *ybot=it->useheight;

  if (*ybot > *ytop+ANALOGTV_MAX_LINEHEIGHT) *ybot=*ytop+ANALOGTV_MAX_LINEHEIGHT;
  return 1;
}

static void
analogtv_blast_imagerow(analogtv *it,
                        const std::vector<float>& rgbf,
                        int ytop, int ybot)
{
  std::vector<cv::Vec4b*> level_copyfrom(3, nullptr);
  // 1 or 2
  int xrepl=it->xrepl;

  unsigned lineheight = ybot - ytop;
  if (lineheight > ANALOGTV_MAX_LINEHEIGHT)
    lineheight = ANALOGTV_MAX_LINEHEIGHT;

  for (int y = ytop; y < ybot; y++)
  {
    cv::Vec4b *rowdata = it->image[y];
    unsigned line = y-ytop;

    int   level     = it->leveltable[lineheight][line].index;
    float levelmult = it->leveltable[lineheight][line].value;

    if (level_copyfrom[level])
    {
      memcpy((void*)rowdata, (void*)level_copyfrom[level], it->image.step);
    }
    else
    {
      level_copyfrom[level] = rowdata;

      for (size_t i = 0; i < rgbf.size() / 3; i++)
      {
        cv::Vec4i rgb;
        rgb[0] = rgbf[i*3 + 0];
        rgb[1] = rgbf[i*3 + 1];
        rgb[2] = rgbf[i*3 + 2];

        for (int j = 0; j < 3; j++)
        {
          rgb[j] = it->intensity_values[std::min(int(rgb[j] * levelmult), ANALOGTV_CV_MAX-1)];
        }

        cv::Vec4b v(rgb[2], rgb[1], rgb[0], 0);

        rowdata[i*xrepl + 0] = v;

        // 1 or 2
        if (xrepl >= 2)
        {
          rowdata[i*xrepl + 1] = v;
        }
      }
    }
  }
}

static void analogtv_thread_draw_lines(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  analogtv *it = thread->it;

  int lineno;

  std::vector<float> raw_rgb(it->subwidth * 3);

  for (lineno=ANALOGTV_TOP + thread->thread_id;
       lineno<ANALOGTV_BOT;
       lineno += it->threads.count) {
    int i;

    int slineno, ytop, ybot;
    unsigned signal_offset;

    const float *signal;

    int scanstart_i,scanend_i,squishright_i,squishdiv,pixrate;
    float *rgb_start, *rgb_end;
    float pixbright;
    int pixmultinc;

    float *rrp;

    struct analogtv_yiq_s yiq[ANALOGTV_PIC_LEN+10];

    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot,
        &signal_offset))
      continue;

    signal = it->rx_signal + signal_offset;

    {

      float bloomthisrow,shiftthisrow;
      float viswidth,middle;
      float scanwidth;
      int scw,scl,scr;

      bloomthisrow = -10.0f * it->crtload[lineno];
      if (bloomthisrow<-10.0f) bloomthisrow=-10.0f;
      if (bloomthisrow>2.0f) bloomthisrow=2.0f;
      if (slineno<16) {
        shiftthisrow=it->horiz_desync * (expf(-0.17f*slineno) *
                                         (0.7f+cosf(slineno*0.6f)));
      } else {
        shiftthisrow=0.0f;
      }

      viswidth=ANALOGTV_PIC_LEN * 0.79f - 5.0f*bloomthisrow;
      middle=ANALOGTV_PIC_LEN/2 - shiftthisrow;

      scanwidth=it->width_control * puramp(it, 0.5f, 0.3f, 1.0f);

      scw=it->subwidth*scanwidth;
      if (scw>it->subwidth) scw=it->usewidth;
      scl=it->subwidth/2 - scw/2;
      scr=it->subwidth/2 + scw/2;

      pixrate=(int)((viswidth*65536.0f*1.0f)/it->subwidth)/scanwidth;
      scanstart_i=(int)((middle-viswidth*0.5f)*65536.0f);
      scanend_i=(ANALOGTV_PIC_LEN-1)*65536;
      squishright_i=(int)((middle+viswidth*(0.25f + 0.25f*puramp(it, 2.0f, 0.0f, 1.1f)
                                            - it->squish_control)) *65536.0f);
      squishdiv=it->subwidth/15;

      rgb_start = raw_rgb.data() + scl*3;
      rgb_end = raw_rgb.data() + scr*3;

      assert(scanstart_i>=0);

#ifdef DEBUG
      if (0) printf("scan %d: %0.3f %0.3f %0.3f scl=%d scr=%d scw=%d\n",
                    lineno,
                    scanstart_i/65536.0f,
                    squishright_i/65536.0f,
                    scanend_i/65536.0f,
                    scl,scr,scw);
#endif
    }

    {
      analogtv_ntsc_to_yiq(it, lineno, signal,
                           (scanstart_i>>16)-10, (scanend_i>>16)+10, yiq);

      pixbright=it->contrast_control * puramp(it, 1.0f, 0.0f, 1.0f)
        / (0.5f+0.5f*it->puheight) * 1024.0f/100.0f;
      pixmultinc=pixrate;
      i=scanstart_i; rrp=rgb_start;
      while (i<0 && rrp!=rgb_end) {
        rrp[0]=rrp[1]=rrp[2]=0;
        i+=pixmultinc;
        rrp+=3;
      }
      while (i<scanend_i && rrp!=rgb_end) {
        float pixfrac=(i&0xffff)/65536.0f;
        float invpixfrac=1.0f-pixfrac;
        int pati=i>>16;
        float r,g,b;

        float interpy=(yiq[pati].y*invpixfrac + yiq[pati+1].y*pixfrac);
        float interpi=(yiq[pati].i*invpixfrac + yiq[pati+1].i*pixfrac);
        float interpq=(yiq[pati].q*invpixfrac + yiq[pati+1].q*pixfrac);

        /*
          According to the NTSC spec, Y,I,Q are generated as:

          y=0.30 r + 0.59 g + 0.11 b
          i=0.60 r - 0.28 g - 0.32 b
          q=0.21 r - 0.52 g + 0.31 b

          So if you invert the implied 3x3 matrix you get what standard
          televisions implement with a bunch of resistors (or directly in the
          CRT -- don't ask):

          r = y + 0.948 i + 0.624 q
          g = y - 0.276 i - 0.639 q
          b = y - 1.105 i + 1.729 q
        */

        r=(interpy + 0.948f*interpi + 0.624f*interpq) * pixbright;
        g=(interpy - 0.276f*interpi - 0.639f*interpq) * pixbright;
        b=(interpy - 1.105f*interpi + 1.729f*interpq) * pixbright;
        if (r<0.0f) r=0.0f;
        if (g<0.0f) g=0.0f;
        if (b<0.0f) b=0.0f;
        rrp[0]=r;
        rrp[1]=g;
        rrp[2]=b;

        if (i>=squishright_i) {
          pixmultinc += pixmultinc/squishdiv;
          pixbright += pixbright/squishdiv/2;
        }
        i+=pixmultinc;
        rrp+=3;
      }
      while (rrp != rgb_end)
      {
        rrp[0]=rrp[1]=rrp[2]=0.0f;
        rrp+=3;
      }

      analogtv_blast_imagerow(it, raw_rgb, ytop, ybot);
    }
  }
}

void
analogtv_draw(analogtv *it, double noiselevel,
              const analogtv_reception *const *recs, unsigned rec_count)
{
  int i,lineno;
  /*  int bigloadchange,drawcount;*/
  double baseload;
  int overall_top, overall_bot;

  /* AnalogTV isn't very interesting if there isn't enough RAM. */
  if (it->image.empty())
    return;

  it->rx_signal_level = noiselevel;
  for (i = 0; i != (int)rec_count; ++i) {
    const analogtv_reception *rec = recs[i];
    double level = rec->level;
    analogtv_input *inp=rec->input;

    it->rx_signal_level =
      sqrt(it->rx_signal_level * it->rx_signal_level +
           (level * level * (1.0 + 4.0*(rec->ghostfir[0] + rec->ghostfir[1] +
                                        rec->ghostfir[2] + rec->ghostfir[3]))));

    /* duplicate the first line into the Nth line to ease wraparound computation */
    memcpy(inp->signal[ANALOGTV_V], inp->signal[0],
           ANALOGTV_H * sizeof(inp->signal[0][0]));
  }

  analogtv_setup_frame(it);

  it->random0 = ya_random();
  it->random1 = ya_random();
  it->noiselevel = noiselevel;
  it->recs = recs;
  it->rec_count = rec_count;
  threadpool_run(&it->threads, analogtv_thread_add_signals);
  threadpool_wait(&it->threads);

  it->channel_change_cycles=0;

  /* rx_signal has an extra 2 lines at the end, where we copy the
     first 2 lines so we can index into it while only worrying about
     wraparound on a per-line level */
  memcpy(&it->rx_signal[ANALOGTV_SIGNAL_LEN],
         &it->rx_signal[0],
         2*ANALOGTV_H*sizeof(it->rx_signal[0]));

  analogtv_sync(it); /* Requires the add_signals be complete. */

  baseload=0.5;
  /* if (it->hashnoise_on) baseload=0.5; */

  /*bigloadchange=1;
    drawcount=0;*/
  it->crtload[ANALOGTV_TOP-1]=baseload;
  it->puheight = puramp(it, 2.0, 1.0, 1.3) * it->height_control *
    (1.125 - 0.125*puramp(it, 2.0, 2.0, 1.1));

  analogtv_setup_levels(it, it->puheight * (double)it->useheight/(double)ANALOGTV_VISLINES);

  /* calculate tint once per frame */
  /* Christopher Mosher argues that this should use 33 degress instead of
     103 degrees, and then TVTint should default to 0 in analogtv.h and
     all relevant XML files. But that makes all the colors go really green
     and saturated, so apparently that's not right.  -- jwz, Nov 2020.
   */
  it->tint_i = -cos((103 + it->tint_control)*M_PI/180);
  it->tint_q = sin((103 + it->tint_control)*M_PI/180);
  
  for (lineno=ANALOGTV_TOP; lineno<ANALOGTV_BOT; lineno++) {
    int slineno, ytop, ybot;
    unsigned signal_offset;
    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot, &signal_offset))
      continue;

    if (lineno==it->shrinkpulse) {
      baseload += 0.4;
      /*bigloadchange=1;*/
      it->shrinkpulse=-1;
    }

#if 0
    if (it->hashnoise_rpm>0.0 &&
        !(bigloadchange ||
          it->redraw_all ||
          (slineno<20 && it->flutter_horiz_desync) ||
          it->gaussiannoise_level>30 ||
          ((it->gaussiannoise_level>2.0 ||
            it->multipath) && ya_random()%4) ||
          linesig != it->onscreen_signature[lineno])) {
      continue;
    }
    it->onscreen_signature[lineno] = linesig;
#endif
    /*    drawcount++;*/

    /*
      Interpolate the 600-dotclock line into however many horizontal
      screen pixels we're using, and convert to RGB.

      We add some 'bloom', variations in the horizontal scan width with
      the amount of brightness, extremely common on period TV sets. They
      had a single oscillator which generated both the horizontal scan and
      (during the horizontal retrace interval) the high voltage for the
      electron beam. More brightness meant more load on the oscillator,
      which caused an decrease in horizontal deflection. Look for
      (bloomthisrow).

      Also, the A2 did a bad job of generating horizontal sync pulses
      during the vertical blanking interval. This, and the fact that the
      horizontal frequency was a bit off meant that TVs usually went a bit
      out of sync during the vertical retrace, and the top of the screen
      would be bent a bit to the left or right. Look for (shiftthisrow).

      We also add a teeny bit of left overscan, just enough to be
      annoying, but you can still read the left column of text.

      We also simulate compression & brightening on the right side of the
      screen. Most TVs do this, but you don't notice because they overscan
      so it's off the right edge of the CRT. But the A2 video system used
      so much of the horizontal scan line that you had to crank the
      horizontal width down in order to not lose the right few characters,
      and you'd see the compression on the right edge. Associated with
      compression is brightening; since the electron beam was scanning
      slower, the same drive signal hit the phosphor harder. Look for
      (squishright_i) and (squishdiv).
    */

    {
      /* This used to be an int, I suspect by mistake. - Dave */
      float totsignal = 0;
      for (uint32_t i = signal_offset; i < (signal_offset + ANALOGTV_PIC_LEN); i++)
      {
        totsignal += it->rx_signal[i];
      }

      totsignal *= it->agclevel;
      float ncl = 0.95f * it->crtload[lineno-1] +
        0.05f*(baseload +
               (totsignal-30000)/100000.0f +
               (slineno>184 ? (slineno-184)*(lineno-184)*0.001f * it->squeezebottom
                : 0.0f));
      /*diff=ncl - it->crtload[lineno];*/
      /*bigloadchange = (diff>0.01 || diff<-0.01);*/
      it->crtload[lineno]=ncl;
    }
  }

  threadpool_run(&it->threads, analogtv_thread_draw_lines);
  threadpool_wait(&it->threads);

#if 0
  /* poor attempt at visible retrace */
  for (i=0; i<15; i++) {
    int ytop=(int)((i*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int ybot=(int)(((i+1)*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int div=it->usewidth*3/2;

    for (x=0; x<it->usewidth; x++) {
      y = ytop + (ybot-ytop)*x / div;
      if (y<0 || y>=it->useheight) continue;
      *(uint32_t*)(it->image->data + y * it->image->bytes_per_line + x * sizeof(uint32_t)) = (uint32_t) 0xffffff;
    }
  }
#endif

  /*
    Subtle change: overall_bot was the bottom of the last scan line. Now it's
    the top of the next-after-the-last scan line. This is the same until
    the y-dimension is > 2400, note ANALOGTV_MAX_LINEHEIGHT.
  */

  overall_top=(int)(it->useheight*(1-it->puheight)/2);
  overall_bot=(int)(it->useheight*(1+it->puheight)/2);

  if (overall_top<0) overall_top=0;
  if (overall_bot>it->useheight) overall_bot=it->useheight;

  if (overall_bot > overall_top) {
      custom_XPutImage (it->image,
                   0, overall_top,
                   it->screen_xo, it->screen_yo+overall_top,
                   it->usewidth, overall_bot - overall_top);
  }
}

analogtv_input *
analogtv_input_allocate(void)
{
  analogtv_input *ret=(analogtv_input *)calloc(1,sizeof(analogtv_input));

  return ret;
}

/*
  This takes a screen image and encodes it as a video camera would,
  including all the bandlimiting and YIQ modulation.
  This isn't especially tuned for speed.

  xoff, yoff: top left corner of rendered image, in window pixels.
  w, h: scaled size of rendered image, in window pixels.
  mask: BlackPixel means don't render (it's not full alpha)
*/

struct Color
{
  uint16_t red;
  uint16_t green;
  uint16_t blue;
};

inline Color pixToColor(uint32_t p)
{
  // uint16_t r = (p & 0x00FF0000L) >> 16;
  // uint16_t g = (p & 0x0000FF00L) >> 8;
  // uint16_t b = (p & 0x000000FFL);
  uint16_t r = (p >> 16) & 0xFF;
  uint16_t g = (p >>  8) & 0xFF;
  uint16_t b = (p      ) & 0xFF;
  Color c;
  c.red   = r | (r<<8);
  c.green = g | (g<<8);
  c.blue  = b | (b<<8);
  // c.red   = (r<<8);
  // c.green = (g<<8);
  // c.blue  = (b<<8);
  return c;
}

int
analogtv_load_ximage(analogtv *it, analogtv_input *input,
                     const cv::Mat4b& pic_im, const cv::Mat4b& mask_im,
                     int xoff, int yoff, int target_w, int target_h)
{
  int img_w,img_h;
  int fyx[7],fyy[7];
  int fix[4],fiy[4];
  int fqx[4],fqy[4];
  Color col1[ANALOGTV_PIC_LEN];
  Color col2[ANALOGTV_PIC_LEN];
  char mask[ANALOGTV_PIC_LEN];
  int multiq[ANALOGTV_PIC_LEN+4];
  unsigned long black = 0; /* not BlackPixelOfScreen (it->xgwa.screen); */

  int x_length=ANALOGTV_PIC_LEN;
  int y_overscan=5*ANALOGTV_SCALE; /* overscan this much top and bottom */
  int y_scanlength=ANALOGTV_VISLINES+2*y_overscan;

  if (target_w > 0) x_length     = x_length     * target_w / it->outbuffer_width;
  if (target_h > 0) y_scanlength = y_scanlength * target_h / it->outbuffer_height;

  img_w = pic_im.cols;
  img_h = pic_im.rows;
  
  xoff = ANALOGTV_PIC_LEN  * xoff / it->outbuffer_width;
  yoff = ANALOGTV_VISLINES * yoff / it->outbuffer_height;

  for (int i=0; i<x_length+4; i++)
  {
    double phase=90.0-90.0*i;
    double ampl=1.0;
    multiq[i]=(int)(-cos(M_PI/180.0*(phase-303)) * 4096.0 * ampl);
  }

  for (int y=0; y<y_scanlength; y++)
  {
    int picy1=(y*img_h                 )/y_scanlength;
    int picy2=(y*img_h + y_scanlength/2)/y_scanlength;

    uint32_t* rowIm1 = (uint32_t*)(pic_im.data + picy1 * pic_im.step);
    uint32_t* rowIm2 = (uint32_t*)(pic_im.data + picy2 * pic_im.step);
    uint32_t* rowMask1 = mask_im.data ? (uint32_t*)(mask_im.data + picy1 * mask_im.step) : nullptr;
    for (int x=0; x<x_length; x++)
    {
      int picx=(x*img_w)/x_length;
      col1[x] = pixToColor(rowIm1[picx]);
      col2[x] = pixToColor(rowIm2[picx]);
      if (rowMask1)
        mask[x] = (rowMask1[picx] != black);
      else
        mask[x] = 1;
    }

    for (int i=0; i<7; i++) fyx[i]=fyy[i]=0;
    for (int i=0; i<4; i++) fix[i]=fiy[i]=fqx[i]=fqy[i]=0.0;

    for (int x=0; x<x_length; x++)
    {
      int rawy,rawi,rawq;
      int filty,filti,filtq;
      int composite;

      if (!mask[x]) continue;

      /* Compute YIQ as:
           y=0.30 r + 0.59 g + 0.11 b
           i=0.60 r - 0.28 g - 0.32 b
           q=0.21 r - 0.52 g + 0.31 b
          The coefficients below are in .4 format */

      rawy=( 5*col1[x].red + 11*col1[x].green + 2*col1[x].blue +
             5*col2[x].red + 11*col2[x].green + 2*col2[x].blue)>>7;
      rawi=(10*col1[x].red -  4*col1[x].green - 5*col1[x].blue +
            10*col2[x].red -  4*col2[x].green - 5*col2[x].blue)>>7;
      rawq=( 3*col1[x].red -  8*col1[x].green + 5*col1[x].blue +
             3*col2[x].red -  8*col2[x].green + 5*col2[x].blue)>>7;

      /* Filter y at with a 4-pole low-pass Butterworth filter at 3.5 MHz
         with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 4 -a 2.1428571429e-01 0 -Z 2.5e-01 -l */

      fyx[0] = fyx[1]; fyx[1] = fyx[2]; fyx[2] = fyx[3];
      fyx[3] = fyx[4]; fyx[4] = fyx[5]; fyx[5] = fyx[6];
      fyx[6] = (rawy * 1897) >> 16;
      fyy[0] = fyy[1]; fyy[1] = fyy[2]; fyy[2] = fyy[3];
      fyy[3] = fyy[4]; fyy[4] = fyy[5]; fyy[5] = fyy[6];
      fyy[6] = (fyx[0]+fyx[6]) + 4*(fyx[1]+fyx[5]) + 7*(fyx[2]+fyx[4]) + 8*fyx[3]
        + ((-151*fyy[2] + 8115*fyy[3] - 38312*fyy[4] + 36586*fyy[5]) >> 16);
      filty = fyy[6];

      /* Filter I at 1.5 MHz. 3 pole Butterworth from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 */

      fix[0] = fix[1]; fix[1] = fix[2]; fix[2] = fix[3];
      fix[3] = (rawi * 1413) >> 16;
      fiy[0] = fiy[1]; fiy[1] = fiy[2]; fiy[2] = fiy[3];
      fiy[3] = (fix[0]+fix[3]) + 3*(fix[1]+fix[2])
        + ((16559*fiy[0] - 72008*fiy[1] + 109682*fiy[2]) >> 16);
      filti = fiy[3];

      /* Filter Q at 0.5 MHz. 3 pole Butterworth from
         mkfilter -Bu -Lp -o 3 -a 3.5714285714e-02 0 -l */

      fqx[0] = fqx[1]; fqx[1] = fqx[2]; fqx[2] = fqx[3];
      fqx[3] = (rawq * 75) >> 16;
      fqy[0] = fqy[1]; fqy[1] = fqy[2]; fqy[2] = fqy[3];
      fqy[3] = (fqx[0]+fqx[3]) + 3 * (fqx[1]+fqx[2])
        + ((2612*fqy[0] - 9007*fqy[1] + 10453 * fqy[2]) >> 12);
      filtq = fqy[3];

      composite = filty + ((multiq[x] * filti + multiq[x+3] * filtq)>>12);
      composite = ((composite*100)>>14) + ANALOGTV_BLACK_LEVEL;
      if (composite>125) composite=125;
      if (composite<0) composite=0;

      input->signal[y-y_overscan+ANALOGTV_TOP+yoff][x+ANALOGTV_PIC_START+xoff] = composite;
    }
  }

  return 1;
}

#if 0
void analogtv_channel_noise(analogtv_input *it, analogtv_input *s2)
{
  int x,y,newsig;
  int change=ya_random()%ANALOGTV_V;
  unsigned int fastrnd=ya_random();
  double hso=(int)(ya_random()%1000)-500;
  int yofs=ya_random()%ANALOGTV_V;
  int noise;

  for (y=change; y<ANALOGTV_V; y++) {
    int s2y=(y+yofs)%ANALOGTV_V;
    int filt=0;
    int noiselevel=60000 / (y-change+100);

    it->line_hsync[y] = s2->line_hsync[y] + (int)hso;
    hso *= 0.9;
    for (x=0; x<ANALOGTV_H; x++) {
      FASTRND;
      filt+= (-filt/16) + (int)(fastrnd&0xfff)-0x800;
      noise=(filt*noiselevel)>>16;
      newsig=s2->signal[s2y][x] + noise;
      if (newsig>120) newsig=120;
      if (newsig<0) newsig=0;
      it->signal[y][x]=newsig;
    }
  }
  s2->vsync=yofs;
}
#endif


#ifdef FIXME
/* add hash */
  if (it->hashnoise_times[lineno]) {
    int hnt=it->hashnoise_times[lineno] - input->line_hsync[lineno];

    if (hnt>=0 && hnt<ANALOGTV_PIC_LEN) {
      double maxampl=1.0;
      double cur=ya_frand(150.0)-20.0;
      int len=ya_random()%15+3;
      if (len > ANALOGTV_PIC_LEN-hnt) len=ANALOGTV_PIC_LEN-hnt;
      for (i=0; i<len; i++) {
        double sig=signal[hnt];

        sig += cur*maxampl;
        cur += ya_frand(5.0)-5.0;
        maxampl = maxampl*0.9;

        signal[hnt]=sig;
        hnt++;
      }
    }
  }
#endif


void
analogtv_reception_update(analogtv_reception *rec)
{
  if (rec->multipath > 0.0)
  {
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      rec->ghostfir2[i] += -(rec->ghostfir2[i]/16.0) + rec->multipath * (ya_frand(0.02)-0.01);
    }
    if (ya_random()%20==0)
    {
      rec->ghostfir2[ya_random()%(ANALOGTV_GHOSTFIR_LEN)] = rec->multipath * (ya_frand(0.08)-0.04);
    }
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      rec->ghostfir[i] = 0.8*rec->ghostfir[i] + 0.2*rec->ghostfir2[i];
    }

    if (0)
    {
      rec->hfloss2 += -(rec->hfloss2/16.0) + rec->multipath * (ya_frand(0.08)-0.04);
      rec->hfloss = 0.5*rec->hfloss + 0.5*rec->hfloss2;
    }
  }
  else
  {
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      rec->ghostfir[i] = (i>=ANALOGTV_GHOSTFIR_LEN/2) ? ((i&1) ? +0.04 : -0.08) /ANALOGTV_GHOSTFIR_LEN
        : 0.0;
    }
  }
}


void
analogtv_lcp_to_ntsc(double luma, double chroma, double phase, int ntsc[4])
{
  for (int i=0; i<4; i++)
  {
    double w=90.0*i + phase;
    double val=luma + chroma * (cos(M_PI/180.0*w));
    if (val<0.0) val=0.0;
    if (val>127.0) val=127.0;
    ntsc[i]=(int)val;
  }
}

void
analogtv_draw_solid(analogtv_input *input,
                    int left, int right, int top, int bot,
                    int ntsc[4])
{
  if (right-left<4) right=left+4;
  if (bot-top<1) bot=top+1;

  for (int y=top; y<bot; y++) {
    for (int x=left; x<right; x++) {
      input->signal[y][x] = ntsc[x&3];
    }
  }
}


void
analogtv_draw_solid_rel_lcp(analogtv_input *input,
                            double left, double right, double top, double bot,
                            double luma, double chroma, double phase)
{
  int ntsc[4];

  int topi=(int)(ANALOGTV_TOP + ANALOGTV_VISLINES*top);
  int boti=(int)(ANALOGTV_TOP + ANALOGTV_VISLINES*bot);
  int lefti=(int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*left);
  int righti=(int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*right);

  analogtv_lcp_to_ntsc(luma, chroma, phase, ntsc);
  analogtv_draw_solid(input, lefti, righti, topi, boti, ntsc);
}
