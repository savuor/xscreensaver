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

#include "analogtv.hpp"
#include "utils.hpp"

/* #define DEBUG 1 */

#define FASTRND_A 1103515245
#define FASTRND_C 12345
#define FASTRND (fastrnd = fastrnd*FASTRND_A+FASTRND_C)


float AnalogTV::puramp(float tc, float start, float over) const
{
  float pt = this->powerup - start;

  if (pt < 0.0f) return 0.0f;
  if (pt > 900.0f || pt / tc > 8.0f) return 1.0f;

  float ret = (1.0f-expf(-pt/tc))*over;

  if (ret > 1.0f) return 1.0f;
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

void AnalogTV::set_defaults()
{
  // values taken from analogtv-cli
  this->tint_control = 5; // 270 (almost) fixes the color issue!
  this->color_control = 70/100.0;
  this->brightness_control = 2 / 100.0;
  this->contrast_control = 150 / 100.0;
  this->height_control = 1.0;
  this->width_control = 1.0;
  this->squish_control = 0.0;
  this->powerup=1000.0;

  //it->hashnoise_rpm=0;
  this->hashnoise_on=0;
  this->hashnoise_enable=1;

  this->horiz_desync  = this->rng.uniform(-5.0, 5.0);
  this->squeezebottom = this->rng.uniform(-1.0, 4.0);

  //TODO: make logs
#ifdef DEBUG
  printf("analogtv: prefix=%s\n",prefix);
  printf("  use: color=1\n");
  printf("  controls: tint=%g color=%g brightness=%g contrast=%g\n",
         this->tint_control, this->color_control, this->brightness_control,
         this->contrast_control);
/*  printf("  freq_error %g: %g %d\n",
         this->freq_error, this->freq_error_inc, this->flutter_tint); */
  printf("  desync: %g %d\n",
         this->horiz_desync, this->flutter_horiz_desync);
  printf("  hashnoise rpm: %g\n",
         this->hashnoise_rpm);
  printf("  size: %d %d xrepl=%d\n",
         this->usewidth, this->useheight,
         this->xrepl);

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


void AnalogTV::configure()
{
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

  int hlim = this->outBuffer.rows;
  int wlim = this->outBuffer.cols;
  ratio = wlim / (float) hlim;

// NO_CONSTRAIN_RATIO is defined
//#if defined(HAVE_MOBILE) || defined(NO_CONSTRAIN_RATIO)
  /* Fill the whole iPhone screen, even though that distorts the image. */
  min_ratio = 0;
  max_ratio = 10;
//#endif

  std::string debugPrint1 = std::to_string(wlim) + "x" + std::to_string(hlim);
  std::string debugPrint2 = " in " + std::to_string(this->outBuffer.cols) + "x" + std::to_string(this->outBuffer.rows);
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
      Log::write(3, "size: close enough: " + debugPrint1 + debugPrint3);
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
        hlim = this->outBuffer.rows;
      else
        wlim = this->outBuffer.cols;
      // debug mode
      Log::write(3, "size: aspect: " + debugPrint1 + debugPrint2 + debugPrint3);
    }

  int height_diff = ((hlim + ANALOGTV_VISLINES/2) % ANALOGTV_VISLINES) - ANALOGTV_VISLINES/2;
  if (height_diff != 0 && abs(height_diff) < hlim * height_snap)
    {
      hlim -= height_diff;
    }

  /* Most times this doesn't change */
  if (wlim != this->usewidth || hlim != this->useheight)
  {
    this->usewidth=wlim;
    this->useheight=hlim;

    //TODO: fix this behavior
    this->xrepl = 1 + this->usewidth / 640;
    this->xrepl = std::min(this->xrepl, 2);
    this->subwidth = this->usewidth / this->xrepl;

    this->image = cv::Mat4b(this->useheight, this->usewidth);
  }
}

AnalogTV::AnalogTV() :
  agclevel(),
  tint_control(),
  color_control(),
  brightness_control(),
  contrast_control(),
  height_control(),
  width_control(),
  squish_control(),
  horiz_desync(),
  squeezebottom(),
  powerup(),

  usewidth(),
  useheight(),
  xrepl(),
  subwidth(),
  image(),
  outBuffer(),

  flutter_horiz_desync(),
  hashnoise_on(),
  hashnoise_enable(),

  tint_i(),
  tint_q(),

  cur_hsync(),
  line_hsync(),
  cur_vsync(),
  cb_phase(),
  line_cb_phase(),

  channel_change_cycles(),
  rx_signal_level(),
  rx_signal(),

  puheight(),
  rng()
{
  // float crtload[ANALOGTV_V];

  // int line_hsync[ANALOGTV_V];
  // double cb_phase[4];
  // double line_cb_phase[ANALOGTV_V][4];

  // struct {
  //   int index;
  //   double value;
  // } leveltable[ANALOGTV_MAX_LINEHEIGHT+1][ANALOGTV_MAX_LINEHEIGHT+1];

  this->rx_signal.resize(ANALOGTV_SIGNAL_LEN + 2*ANALOGTV_H);

  this->shrinkpulse = -1;

  for (int i = 0; i < ANALOGTV_CV_MAX; i++)
  {
    int intensity = pow(i / 256.0, 0.8) * 65535.0; /* gamma correction */
    intensity = std::min(intensity, 65535);
    this->intensity_values[i] = intensity >> 8;
  }
}


void AnalogTV::set_buffer(cv::Mat4b outBuffer)
{
  this->outBuffer = outBuffer;

  this->configure();
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

struct analogtv_yiq_s {
  float y,i,q;
} /*yiq[ANALOGTV_PIC_LEN+10] */;

void AnalogTV::ntsc_to_yiq(int lineno, unsigned int signal_offset, int start, int end, struct analogtv_yiq_s *it_yiq) const
{
  enum {MAXDELAY=32};

  const float *signal = this->rx_signal.data() + signal_offset;
  int phasecorr = signal_offset & 3;

  float multiq2[4];

  bool colormode;
  {
    double cb_i=(this->line_cb_phase[lineno][(2+phasecorr)&3]-
                 this->line_cb_phase[lineno][(0+phasecorr)&3])/16.0;
    double cb_q=(this->line_cb_phase[lineno][(3+phasecorr)&3]-
                 this->line_cb_phase[lineno][(1+phasecorr)&3])/16.0;

    colormode = (cb_i * cb_i + cb_q * cb_q) > 2.8;

    if (colormode)
    {
      multiq2[0] = (cb_i * this->tint_i - cb_q * this->tint_q) * this->color_control;
      multiq2[1] = (cb_q * this->tint_i + cb_i * this->tint_q) * this->color_control;
      multiq2[2] = -multiq2[0];
      multiq2[3] = -multiq2[1];
    }
  }

#if 0
  if (lineno==100) {
    printf("multiq = [%0.3f %0.3f %0.3f %0.3f] ",
           this->multiq[60], this->multiq[61], this->multiq[62], this->multiq[63]);
    printf("it->line_cb_phase = [%0.3f %0.3f %0.3f %0.3f]\n",
           this->line_cb_phase[lineno][0], this->line_cb_phase[lineno][1],
           this->line_cb_phase[lineno][2], this->line_cb_phase[lineno][3]);
    printf("multiq2 = [%0.3f %0.3f %0.3f %0.3f]\n",
           multiq2[0],multiq2[1],multiq2[2],multiq2[3]);
  }
#endif

  //TODO: no ptr arithmetics, use idx
  float delay[MAXDELAY + ANALOGTV_PIC_LEN], *dp;

  dp = delay + ANALOGTV_PIC_LEN - MAXDELAY;
  for (int i = 0; i < 5; i++) dp[i]=0.0f;

  assert(start >= 0);
  assert(end < ANALOGTV_PIC_LEN + 10);

  dp = delay + ANALOGTV_PIC_LEN - MAXDELAY;
  for (int i = 0; i < 24; i++) dp[i]=0.0;

  float agclevel  = this->agclevel;
  float brightadd = this->brightness_control * 100.0 - ANALOGTV_BLACK_LEVEL;

  for (int i = start; i < end; i++, dp--)
  {
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

    float sig = signal[i];
    dp[0] = sig * 0.0469904257251935f * agclevel;
    dp[8] = (+1.0f*(dp[6]+dp[0])
             +4.0f*(dp[5]+dp[1])
             +7.0f*(dp[4]+dp[2])
             +8.0f*(dp[3])
             -0.0176648f*dp[12]
             -0.4860288f*dp[10]);
    it_yiq[i].y = dp[8] + brightadd;
  }

  if (colormode)
  {
    dp = delay + ANALOGTV_PIC_LEN - MAXDELAY;
    for (int i = 0; i < 27; i++) dp[i]=0.0;

    for (int i = start; i < end; i++, dp--)
    {
      float sig = signal[i];

      /* Filter I and Q with a 3-pole low-pass Butterworth filter at
         1.5 MHz with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 -Z 2.5000000000e-01 -l
         Delay about 3.
      */

      dp[0] = sig*multiq2[i&3] * 0.0833333333333f;
      it_yiq[i].i = dp[8] = (dp[5] + dp[0]
                            +3.0f*(dp[4] + dp[1])
                            +4.0f*(dp[3] + dp[2])
                            -0.3333333333f * dp[10]);

      dp[16] = sig*multiq2[(i+3)&3] * 0.0833333333333f;

      it_yiq[i].q = dp[24] = (dp[16+5] + dp[16+0]
                             +3.0f*(dp[16+4] + dp[16+1])
                             +4.0f*(dp[16+3] + dp[16+2])
                             -0.3333333333f * dp[24+2]);
    }
  }
  else
  {
    for (int i = start; i < end; i++)
    {
      it_yiq[i].i = 0.f;
      it_yiq[i].q = 0.f;
    }
  }
}


void AnalogTV::setup_frame()
{
  /*  int i,x,y;*/

  if (this->flutter_horiz_desync)
  {
    /* Horizontal sync during vertical sync instability. */

    this->horiz_desync += -0.10*(this->horiz_desync-3.0) +
                          this->rng.uniform(-0x80, 0x80) *
                          this->rng.uniform(-0x80, 0x80) *
                          this->rng.uniform(-0x80, 0x80) * 0.000001;
  }

  /* it wasn't used
  for (i=0; i<ANALOGTV_V; i++) {
    this->hashnoise_times[i]=0;
  }
  */

  /* let's leave it to process shrinkpulse */
  if (this->hashnoise_enable && !this->hashnoise_on)
  {
    if (this->rng() % 10000 == 0)
    {
      this->hashnoise_on = 1;
      this->shrinkpulse = this->rng() % ANALOGTV_V;
    }
  }
  if (this->rng() % 1000 == 0)
  {
    this->hashnoise_on = 0;
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


  if (this->rx_signal_level != 0.0)
    this->agclevel = 1.0/this->rx_signal_level;

//TODO: make logs
#ifdef DEBUG2
  printf("filter: ");
  for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
    printf(" %0.3f",this->ghostfir[i]);
  }
  printf(" siglevel=%g agc=%g\n", siglevel, this->agclevel);
#endif
}


void AnalogInput::setup_sync(int do_cb, int do_ssavi)
{
  int synclevel = do_ssavi ? ANALOGTV_WHITE_LEVEL : ANALOGTV_SYNC_LEVEL;

  for (int lineno = 0; lineno < ANALOGTV_V; lineno++)
  {
    int vsync = lineno >= 3 && lineno < 7;

    signed char *sig = this->sigMat[lineno];

    int i = ANALOGTV_SYNC_START;
    if (vsync)
    {
      while (i<ANALOGTV_BP_START) sig[i++] = ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_H)        sig[i++] = synclevel;
    }
    else
    {
      while (i<ANALOGTV_BP_START)  sig[i++] = synclevel;
      while (i<ANALOGTV_PIC_START) sig[i++] = ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_FP_START)  sig[i++] = ANALOGTV_BLACK_LEVEL;
    }
    while (i<ANALOGTV_H) sig[i++]=ANALOGTV_BLANK_LEVEL;

    if (do_cb)
    {
      /* 9 cycles of colorburst */
      for (int i = ANALOGTV_CB_START; i < ANALOGTV_CB_START + 36*ANALOGTV_SCALE; i+=4*ANALOGTV_SCALE)
      {
        sig[i+1] += ANALOGTV_CB_LEVEL;
        sig[i+3] -= ANALOGTV_CB_LEVEL;
      }
    }
  }
}

void AnalogTV::sync()
{
  int cur_hsync = this->cur_hsync;
  int cur_vsync = this->cur_vsync;

  float osc, filt;

/*  sp = this->rx_signal + lineno*ANALOGTV_H + cur_hsync;*/
  int vi;
  for (int i = -32*ANALOGTV_SCALE; i < 32*ANALOGTV_SCALE; i++)
  {
    vi = i;
    int lineno = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;

    filt=0.0f;
    for (int j = 0; j < ANALOGTV_H; j += ANALOGTV_H/(16*ANALOGTV_SCALE))
    {
      filt += this->rx_signal[lineno * ANALOGTV_H + j];
    }
    filt *= this->agclevel;

    osc = (float)(ANALOGTV_V + i)/(float)ANALOGTV_V;

    if (osc >= 1.05f+0.0002f * filt)
      break;
  }
  cur_vsync = (cur_vsync + vi + ANALOGTV_V) % ANALOGTV_V;

  for (int lineno = 0; lineno < ANALOGTV_V; lineno++)
  {
    if (lineno > 5*ANALOGTV_SCALE && lineno < ANALOGTV_V - 3*ANALOGTV_SCALE)
    {
      /* ignore vsync interval */
      unsigned lineno2 = (lineno + cur_vsync + ANALOGTV_V) % ANALOGTV_V;
      if (!lineno2)
        lineno2 = ANALOGTV_V;

      int sidx = lineno2*ANALOGTV_H + cur_hsync;
      int hi;
      for (int i = -8*ANALOGTV_SCALE;  i < 8*ANALOGTV_SCALE; i++)
      {
        hi = i;
        osc = (float)(ANALOGTV_H + i) / (float)ANALOGTV_H;
        filt = ( this->rx_signal[sidx + i - 3] +
                 this->rx_signal[sidx + i - 2] +
                 this->rx_signal[sidx + i - 1] +
                 this->rx_signal[sidx + i - 0] ) * this->agclevel;

        if (osc >= 1.005f + 0.0001f*filt)
          break;
      }
      cur_hsync = (cur_hsync + hi + ANALOGTV_H) % ANALOGTV_H;
    }

    this->line_hsync[lineno]=(cur_hsync + ANALOGTV_PIC_START +
                            ANALOGTV_H) % ANALOGTV_H;

    /* Now look for the colorburst, which is a few cycles after the H
       sync pulse, and store its phase.
       The colorburst is 9 cycles long, and we look at the middle 5
       cycles.
    */

    if (lineno > 15*ANALOGTV_SCALE)
    {
      for (int i = ANALOGTV_CB_START + 8*ANALOGTV_SCALE; i < ANALOGTV_CB_START + (36-8)*ANALOGTV_SCALE; i++)
      {
        this->cb_phase[i&3] = this->cb_phase[i&3] * (1.0f - 1.0f/128.0f) +
                              this->rx_signal[lineno*ANALOGTV_H + (cur_hsync&~3) + i] * this->agclevel * (1.0f/128.0f);
      }
    }

    {
      float tot=0.1f;
      float cbgain;

      for (int i = 0; i < 4; i++)
      {
        tot += this->cb_phase[i] * this->cb_phase[i];
      }
      cbgain = 32.0f/sqrtf(tot);

      for (int i = 0; i < 4; i++)
      {
        this->line_cb_phase[lineno][i] = this->cb_phase[i]*cbgain;
      }
    }

#ifdef DEBUG
    if (0) printf("hs=%d cb=[%0.3f %0.3f %0.3f %0.3f]\n",
                  cur_hsync,
                  this->cb_phase[0], this->cb_phase[1],
                  this->cb_phase[2], this->cb_phase[3]);
#endif

    /* if (ya_random()%2000==0) cur_hsync=ya_random()%ANALOGTV_H; */
  }

  this->cur_hsync = cur_hsync;
  this->cur_vsync = cur_vsync;
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

void AnalogTV::setup_levels(double avgheight)
{
  static const double levelfac[3] = {-7.5, 5.5, 24.5};

  for (int height = 0; height < avgheight + 2.0 && height <= ANALOGTV_MAX_LINEHEIGHT; height++)
  {
    for (int i = 0; i < height; i++)
    {
      this->leveltable[height][i].index = 2;
    }
    
    if (avgheight >= 3)
    {
      this->leveltable[height][0].index=0;
    }

    if (avgheight >= 5)
    {
      if (height >= 1)
        this->leveltable[height][height-1].index=0;
    }
    if (avgheight >= 7)
    {
      this->leveltable[height][1].index=1;
      if (height >= 2)
        this->leveltable[height][height-2].index=1;
    }

    for (int i = 0; i<height; i++)
    {
      this->leveltable[height][i].value = (40.0 + levelfac[this->leveltable[height][i].index] * this->puramp(3.0, 6.0, 1.0)) / 256.0;
    }

  }
}


static unsigned int rnd_seek(unsigned a, unsigned c, unsigned rnd, unsigned dist)
{
  unsigned int a1 = a, c1 = c;
  a = 1, c = 0;

  while(dist)
  {
    if(dist & 1)
    {
        a = (a * a1) & 0xffffffffu;
        c = (c1 + a1 * c) & 0xffffffffu;
    }

    a1 = (a1 * a1) & 0xffffffffu;
    c1 = (c1 + a1 * c1) & 0xffffffffu;

    dist >>= 1;
  }

  return a * rnd + c;
}


// generating uniform value from -range to range
float getUniformSymmetrical(unsigned int& fastrnd, float range)
{
  unsigned int fastrnd_offset = fastrnd - 0x7fffffff;
  float v = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * (range/(float)0x7fffffff);
  fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;

  return v;
}

void AnalogTV::init_signal(double noiselevel, unsigned start, unsigned end, unsigned randVal)
{
  unsigned int fastrnd = rnd_seek(FASTRND_A, FASTRND_C, randVal, start);

  float noiseSize = sqrt(noiselevel*150);

  float nm1 = getUniformSymmetrical(fastrnd, noiseSize);
  float nm2;
  for (uint32_t i = start; i < end; i++)
  {
    nm2 = nm1;
    nm1 = getUniformSymmetrical(fastrnd, noiseSize);
    this->rx_signal[i] = nm1 * nm2;
  }
}


void AnalogTV::transit_channels(const AnalogReception& rec, unsigned start, int skip, unsigned randVal)
{
  signed char* signal = rec.input->sigMat[0];

  /* Do a big noisy transition. We can make the transition noise of
     high constant strength regardless of signal strength.

     There are two separate state machines. here, One is the noise
     process and the other is the

     We don't bother with the FIR filter here
  */

  unsigned int fastrnd = rnd_seek(FASTRND_A, FASTRND_C, randVal, start);

  const float noise_decay = 0.99995f;
  float noise_ampl = 1.3f * powf(noise_decay, start);

  float level = rec.level;
  for (int i = start; i < skip + (int)start; i++)
  {
    float noise = getUniformSymmetrical(fastrnd, 50.f);

    int idx = (start + (unsigned)rec.ofs + i) % ANALOGTV_SIGNAL_LEN;
    this->rx_signal[i] += (float)(signal[idx]) * level * (1.0f - noise_ampl) + noise * noise_ampl;

    noise_ampl *= noise_decay;
  }
}


void AnalogTV::add_signal(const AnalogReception& rec, unsigned start, unsigned end, int skip)
{
  assert(((int)end - (int)start - skip) % 4 == 0);

  signed char* signal = rec.input->sigMat[0];
  float level = rec.level;

  float dp[5];
  dp[0]=0.0;

  int sii = (start + (unsigned)rec.ofs + skip) % ANALOGTV_SIGNAL_LEN;
  for (int i = 1; i < 5; i++)
  {
    sii -= 4;
    if (sii < 0)
      sii += ANALOGTV_SIGNAL_LEN;
    dp[i] = (float)((int)(signal[sii + 0]) +
                    (int)(signal[sii + 1]) +
                    (int)(signal[sii + 2]) +
                    (int)(signal[sii + 3]));
  }

  float hfloss=rec.hfloss;
  for (int i = (int)start + skip; i < (int)end; i += 4)
  {
    float sig0,sig1,sig2,sig3,sigr;

    int sigIdx = (i + (int)(rec.ofs)) % ANALOGTV_SIGNAL_LEN;
    sig0 = (float) (signal[sigIdx + 0]);
    sig1 = (float) (signal[sigIdx + 1]);
    sig2 = (float) (signal[sigIdx + 2]);
    sig3 = (float) (signal[sigIdx + 3]);

    dp[0] = sig0 + sig1 + sig2 + sig3;

    /* Get the video out signal, and add some ghosting, typical of RF
       monitor cables. This corresponds to a pretty long cable, but
       looks right to me.
    */

    sigr = (dp[1]*rec.ghostfir[0] + dp[2]*rec.ghostfir[1] +
            dp[3]*rec.ghostfir[2] + dp[4]*rec.ghostfir[3]);
    dp[4]=dp[3]; dp[3]=dp[2]; dp[2]=dp[1]; dp[1]=dp[0];

    this->rx_signal[i + 0] += (sig0 + sigr + sig2 * hfloss) * level;
    this->rx_signal[i + 1] += (sig1 + sigr + sig3 * hfloss) * level;
    this->rx_signal[i + 2] += (sig2 + sigr + sig0 * hfloss) * level;
    this->rx_signal[i + 3] += (sig3 + sigr + sig1 * hfloss) * level;
  }
}


int AnalogTV::get_line(int lineno, int *slineno, int *ytop, int *ybot, unsigned *signal_offset) const
{
  *slineno = lineno - ANALOGTV_TOP;
  *ytop = (int)(((lineno - ANALOGTV_TOP  ) * this->useheight / ANALOGTV_VISLINES - this->useheight/2) * this->puheight) + this->useheight/2;
  *ybot = (int)(((lineno - ANALOGTV_TOP+1) * this->useheight / ANALOGTV_VISLINES - this->useheight/2) * this->puheight) + this->useheight/2;
#if 0
  int linesig=analogtv_line_signature(input,lineno)
    + it->hashnoise_times[lineno];
#endif
  *signal_offset = ((lineno + this->cur_vsync+ANALOGTV_V) % ANALOGTV_V) * ANALOGTV_H +
                    this->line_hsync[lineno];

  if (*ytop == *ybot) return 0;
  if (*ybot < 0 || *ytop > this->useheight) return 0;

  *ytop = std::max(0, *ytop);

  *ybot = std::min(*ybot, std::min(this->useheight, *ytop + ANALOGTV_MAX_LINEHEIGHT));
  return 1;
}


void AnalogTV::blast_imagerow(const std::vector<float>& rgbf, int ytop, int ybot)
{
  std::vector<cv::Vec4b*> level_copyfrom(3, nullptr);
  // 1 or 2
  int xrepl = this->xrepl;

  unsigned lineheight = ybot - ytop;
  lineheight = std::min(lineheight, (unsigned)ANALOGTV_MAX_LINEHEIGHT);

  for (int y = ytop; y < ybot; y++)
  {
    cv::Vec4b *rowdata = this->image[y];
    unsigned line = y-ytop;

    int   level     = this->leveltable[lineheight][line].index;
    float levelmult = this->leveltable[lineheight][line].value;

    if (level_copyfrom[level])
    {
      memcpy((void*)rowdata, (void*)level_copyfrom[level], this->image.step);
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
          rgb[j] = this->intensity_values[std::min(int(rgb[j] * levelmult), ANALOGTV_CV_MAX-1)];
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


void AnalogTV::parallel_for_draw_lines(const cv::Range& r)
{
  //TODO: Vec3f
  std::vector<float> raw_rgb(this->subwidth * 3);

  // from ANALOGTV_TOP to ANALOGTV_BOT
  for (int lineno = r.start; lineno < r.end; lineno++)
  {
    int slineno, ytop, ybot;
    unsigned signal_offset;
    if (! this->get_line(lineno, &slineno, &ytop, &ybot, &signal_offset))
      continue;

    float bloomthisrow = std::clamp(-10.0f * this->crtload[lineno], -10.f, 2.0f);

    float shiftthisrow = (slineno < 16) ? this->horiz_desync * (expf(-0.17f * slineno) * (0.7f + cosf(slineno*0.6f))) : 0.f;

    float viswidth = ANALOGTV_PIC_LEN * 0.79f - 5.0f * bloomthisrow;
    float middle = ANALOGTV_PIC_LEN/2 - shiftthisrow;

    float scanwidth = this->width_control * this->puramp(0.5f, 0.3f, 1.0f);

    int scw = this->subwidth * scanwidth;
    if (scw > this->subwidth)
        scw = this->usewidth;

    int scl = this->subwidth/2 - scw/2;
    int scr = this->subwidth/2 + scw/2;

    int pixrate = (int)((viswidth*65536.0f*1.0f)/this->subwidth)/scanwidth;
    int scanstart_i = (int)((middle-viswidth*0.5f)*65536.0f);
    int scanend_i = (ANALOGTV_PIC_LEN-1)*65536;
    int squishright_i = (int)((middle+viswidth*(0.25f + 0.25f * this->puramp(2.0f, 0.0f, 1.1f) - this->squish_control)) *65536.0f);
    int squishdiv = this->subwidth/15;

    assert(scanstart_i>=0);

#ifdef DEBUG
      if (0) printf("scan %d: %0.3f %0.3f %0.3f scl=%d scr=%d scw=%d\n",
                    lineno,
                    scanstart_i/65536.0f,
                    squishright_i/65536.0f,
                    scanend_i/65536.0f,
                    scl,scr,scw);
#endif

    struct analogtv_yiq_s yiq[ANALOGTV_PIC_LEN+10];
    this->ntsc_to_yiq(lineno, signal_offset, (scanstart_i>>16)-10, (scanend_i>>16)+10, yiq);

    float pixbright = this->contrast_control * this->puramp(1.0f, 0.0f, 1.0f) / (0.5f+0.5f*this->puheight) * 1024.0f/100.0f;
    int pixmultinc = pixrate;
    int i = scanstart_i;
    int rrpIdx = scl*3;
    while (i < 0 && rrpIdx != scr*3)
    {
      raw_rgb[rrpIdx + 0] = 0;
      raw_rgb[rrpIdx + 1] = 0;
      raw_rgb[rrpIdx + 2] = 0;
      i+=pixmultinc;
      rrpIdx += 3;
    }
    while (i < scanend_i && rrpIdx != scr*3)
    {
      float pixfrac=(i&0xffff)/65536.0f;
      float invpixfrac=1.0f-pixfrac;
      int pati=i>>16;
      float r,g,b;

      float interpy = yiq[pati].y*invpixfrac + yiq[pati+1].y*pixfrac;
      float interpi = yiq[pati].i*invpixfrac + yiq[pati+1].i*pixfrac;
      float interpq = yiq[pati].q*invpixfrac + yiq[pati+1].q*pixfrac;

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

      r = (interpy + 0.948f*interpi + 0.624f*interpq) * pixbright;
      g = (interpy - 0.276f*interpi - 0.639f*interpq) * pixbright;
      b = (interpy - 1.105f*interpi + 1.729f*interpq) * pixbright;
      r = std::max(r, 0.0f);
      g = std::max(g, 0.0f);
      b = std::max(b, 0.0f);
      raw_rgb[rrpIdx + 0] = r;
      raw_rgb[rrpIdx + 1] = g;
      raw_rgb[rrpIdx + 2] = b;

      if (i>=squishright_i)
      {
        pixmultinc += pixmultinc/squishdiv;
        pixbright += pixbright/squishdiv/2;
      }
      i+=pixmultinc;
      rrpIdx += 3;
    }
    while (rrpIdx != scr*3)
    {
      raw_rgb[rrpIdx + 0] = 0;
      raw_rgb[rrpIdx + 1] = 0;
      raw_rgb[rrpIdx + 2] = 0;
      rrpIdx += 3;
    }

    this->blast_imagerow(raw_rgb, ytop, ybot);
  }
}


void AnalogTV::draw(double noiselevel, const std::vector<AnalogReception>& receptions)
{
  /*  int bigloadchange,drawcount;*/

  /* AnalogTV isn't very interesting if there isn't enough RAM. */
  if (this->image.empty())
    return;

  this->rx_signal_level = noiselevel;
  for (int i = 0; i < (int)receptions.size(); ++i)
  {
    const AnalogReception& rec = receptions[i];
    double level = rec.level;

    this->rx_signal_level =
      sqrt(this->rx_signal_level * this->rx_signal_level +
           (level * level * (1.0 + 4.0*(rec.ghostfir[0] + rec.ghostfir[1] +
                                        rec.ghostfir[2] + rec.ghostfir[3]))));

    /* duplicate the first line into the Nth line to ease wraparound computation */
    rec.input->sigMat.row(0).copyTo(rec.input->sigMat.row(ANALOGTV_V));
  }

  this->setup_frame();

  unsigned randVal0 = this->rng();
  unsigned randVal1 = this->rng();

  assert (ANALOGTV_SIGNAL_LEN % 4 == 0);
  cv::parallel_for_(cv::Range(0, ANALOGTV_SIGNAL_LEN), [this, &receptions, noiselevel, randVal0, randVal1](const cv::Range& r)
  {
    unsigned start  = r.start;
    unsigned finish = r.end;

    // align it by 4 for ghost FIR processing
    start  &= ~3;
    finish &= ~3;

    while(start != finish)
    {
      /* Work on 8 KB blocks; these should fit in L1. */
      /* (Though it doesn't seem to help much on my system.) */
      unsigned end = std::min(start + 2048, finish);

      this->init_signal(noiselevel, start, end, randVal0);

      for (uint32_t i = 0; i < receptions.size(); ++i)
      {
        /* Sometimes start > ec. */
        int ec = !i ? this->channel_change_cycles : 0;
        int skip = ((int)start >= ec) ? 0 : std::min(ec, (int)end) - start;

        if (skip > 0)
        {
          this->transit_channels(receptions[i], start, skip, randVal1);
        }
        
        this->add_signal(receptions[i], start, end, skip);
      }

      start = end;
    }
  });

  this->channel_change_cycles = 0;

  /* rx_signal has an extra 2 lines at the end, where we copy the
     first 2 lines so we can index into it while only worrying about
     wraparound on a per-line level */
  memcpy(&this->rx_signal[ANALOGTV_SIGNAL_LEN],
         &this->rx_signal[0],
         2*ANALOGTV_H*sizeof(this->rx_signal[0]));

  this->sync(); /* Requires the add_signals be complete. */

  double baseload = 0.5;
  /* if (this->hashnoise_on) baseload=0.5; */

  /*bigloadchange=1;
    drawcount=0;*/
  this->crtload[ANALOGTV_TOP-1] = baseload;
  this->puheight = this->puramp(2.0, 1.0, 1.3) * this->height_control * (1.125 - 0.125 * this->puramp(2.0, 2.0, 1.1));

  this->setup_levels(this->puheight * (double)this->useheight/(double)ANALOGTV_VISLINES);

  /* calculate tint once per frame */
  /* Christopher Mosher argues that this should use 33 degress instead of
     103 degrees, and then TVTint should default to 0 in analogtv.h and
     all relevant XML files. But that makes all the colors go really green
     and saturated, so apparently that's not right.  -- jwz, Nov 2020.
   */
  this->tint_i = -cos((103 + this->tint_control)*M_PI/180);
  this->tint_q =  sin((103 + this->tint_control)*M_PI/180);

  for (int lineno = ANALOGTV_TOP; lineno < ANALOGTV_BOT; lineno++)
  {
    int slineno, ytop, ybot;
    unsigned signal_offset;
    if (!this->get_line(lineno, &slineno, &ytop, &ybot, &signal_offset))
      continue;

    if (lineno == this->shrinkpulse)
    {
      baseload += 0.4;
      /*bigloadchange=1;*/
      this->shrinkpulse=-1;
    }

#if 0
    if (it->hashnoise_rpm>0.0 &&
        !(bigloadchange ||
         // it->redraw_all ||
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
        totsignal += this->rx_signal[i];
      }

      totsignal *= this->agclevel;
      float ncl = 0.95f * this->crtload[lineno-1] +
                  0.05f*(baseload +
                        (totsignal-30000)/100000.0f +
                        (slineno>184 ? (slineno-184)*(lineno-184)*0.001f * this->squeezebottom
                          : 0.0f));
      /*diff = ncl - this->crtload[lineno];*/
      /*bigloadchange = (diff>0.01 || diff<-0.01);*/
      this->crtload[lineno]=ncl;
    }
  }

  cv::parallel_for_(cv::Range(ANALOGTV_TOP, ANALOGTV_BOT), [this](const cv::Range& r)
  {
    this->parallel_for_draw_lines(r);
  });

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

  int overall_top = (int)(this->useheight*(1-this->puheight)/2);
  int overall_bot = (int)(this->useheight*(1+this->puheight)/2);

  overall_top = std::max(overall_top, 0);
  overall_bot = std::min(overall_bot, this->useheight);

  if (overall_bot > overall_top)
  {
    int screen_xo = ( this->outBuffer.cols - this->usewidth  )/2;
    int screen_yo = ( this->outBuffer.rows - this->useheight )/2;

    int /* dest_x = screen_xo, */ dest_y = screen_yo + overall_top;
    unsigned w = this->usewidth, h = overall_bot - overall_top;

    if (screen_xo < 0)
    {
      w += screen_xo;
      screen_xo = 0;
    }
    w = std::min((int)w, std::min(this->outBuffer.cols - screen_xo, this->image.cols));

    if (dest_y < 0)
    {
      h += dest_y;
      overall_top -= dest_y;
      dest_y = 0;
    }
    h = std::min((int)h, std::min(this->outBuffer.rows - dest_y, this->image.rows - overall_top));

    this->image(cv::Rect(0, overall_top, w, h)).copyTo(this->outBuffer(cv::Rect(screen_xo, dest_y, w, h)));
  }
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


void AnalogInput::load_ximage(const cv::Mat4b& pic_im, const cv::Mat4b& mask_im,
                              int xoff, int yoff, int target_w, int target_h, int out_w, int out_h)
{
  int x_length=ANALOGTV_PIC_LEN;
  int y_overscan=5*ANALOGTV_SCALE; /* overscan this much top and bottom */
  int y_scanlength=ANALOGTV_VISLINES+2*y_overscan;

  if (target_w > 0) x_length     = x_length     * target_w / out_w;
  if (target_h > 0) y_scanlength = y_scanlength * target_h / out_h;

  int img_w = pic_im.cols;
  int img_h = pic_im.rows;

  xoff = ANALOGTV_PIC_LEN  * xoff / out_w;
  yoff = ANALOGTV_VISLINES * yoff / out_w;

  int multiq[ANALOGTV_PIC_LEN+4];
  for (int i=0; i<x_length+4; i++)
  {
    double phase = 90.0 - 90.0 * i;
    double ampl = 1.0;
    multiq[i] = (int)(-cos(M_PI/180.0*(phase-303)) * 4096.0 * ampl);
  }

  for (int y = 0; y < y_scanlength; y++)
  {
    int picy1=(y*img_h                 )/y_scanlength;
    int picy2=(y*img_h + y_scanlength/2)/y_scanlength;

    Color col1[ANALOGTV_PIC_LEN];
    Color col2[ANALOGTV_PIC_LEN];
    char mask[ANALOGTV_PIC_LEN];

    uint32_t* rowIm1 = (uint32_t*)(pic_im.data + picy1 * pic_im.step);
    uint32_t* rowIm2 = (uint32_t*)(pic_im.data + picy2 * pic_im.step);
    uint32_t* rowMask1 = mask_im.data ? (uint32_t*)(mask_im.data + picy1 * mask_im.step) : nullptr;
    for (int x = 0; x < x_length; x++)
    {
      int picx = (x*img_w) / x_length;
      col1[x] = pixToColor(rowIm1[picx]);
      col2[x] = pixToColor(rowIm2[picx]);
      if (rowMask1)
        mask[x] = (rowMask1[picx] != 0);
      else
        mask[x] = 1;
    }

    int fyx[7], fyy[7];
    int fix[4], fiy[4];
    int fqx[4], fqy[4];
    for (int i=0; i<7; i++) fyx[i]=fyy[i]=0;
    for (int i=0; i<4; i++) fix[i]=fiy[i]=fqx[i]=fqy[i]=0.0;

    signed char* sigRow = this->sigMat[y - y_overscan + ANALOGTV_TOP + yoff];
    for (int x = 0; x < x_length; x++)
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
      composite = std::clamp(composite, 0, 125);

      sigRow[x+ANALOGTV_PIC_START+xoff] = composite;
    }
  }
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


void AnalogReception::update(cv::RNG& rng)
{
  if (this->multipath > 0.0)
  {
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      this->ghostfir2[i] += -(this->ghostfir2[i]/16.0) + this->multipath * rng.uniform(-0.01, 0.01);
    }
    if (rng() % 20 == 0)
    {
      this->ghostfir2[rng() % ANALOGTV_GHOSTFIR_LEN] = this->multipath * rng.uniform(-0.04, 0.04);
    }
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      this->ghostfir[i] = 0.8*this->ghostfir[i] + 0.2*this->ghostfir2[i];
    }

    if (0)
    {
      this->hfloss2 += -(this->hfloss2/16.0) + this->multipath * rng.uniform(-0.04, 0.04);
      this->hfloss = 0.5*this->hfloss + 0.5*this->hfloss2;
    }
  }
  else
  {
    for (int i=0; i<ANALOGTV_GHOSTFIR_LEN; i++)
    {
      this->ghostfir[i] = (i>=ANALOGTV_GHOSTFIR_LEN/2) ? ((i&1) ? +0.04 : -0.08) /ANALOGTV_GHOSTFIR_LEN : 0.0;
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
    val = std::clamp(val, 0.0, 127.0);
    ntsc[i]=(int)val;
  }
}

void AnalogInput::draw_solid(int left, int right, int top, int bot, int ntsc[4])
{
  left  = left  / 4;
  right = right / 4;

  right = std::max(right, left+1);
  bot   = std::max(bot,   top+1);

  typedef cv::Vec<int8_t, 4> Vec4c;
  Vec4c v(ntsc[0], ntsc[1], ntsc[2], ntsc[3]);
  for (int y = top; y < bot; y++)
  {
    Vec4c* sigRow = this->sigMat.ptr<Vec4c>(y);
    for (int x = left; x < right; x++)
    {
      sigRow[x] = v;
    }
  }
}


void AnalogInput::draw_solid_rel_lcp(double left, double right, double top, double bot,
                                     double luma, double chroma, double phase)
{
  int ntsc[4];

  int topi   = (int)(ANALOGTV_TOP + ANALOGTV_VISLINES*top);
  int boti   = (int)(ANALOGTV_TOP + ANALOGTV_VISLINES*bot);
  int lefti  = (int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*left);
  int righti = (int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*right);

  analogtv_lcp_to_ntsc(luma, chroma, phase, ntsc);
  this->draw_solid(lefti, righti, topi, boti, ntsc);
}
