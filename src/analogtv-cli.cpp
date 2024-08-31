/* xanalogtv-cli, Copyright © 2018-2023 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 *
 * Performs the "Analog TV" transform on an image file, converting it to
 * an MP4.  The MP4 file will have the same dimensions as the largest of
 * the input images.
 *
 *    --duration     Length in seconds of output MP4.
 *    --size WxH     Dimensions of output MP4.  Defaults to dimensions of
 *                   the largest input image.
 *    --slideshow    With multiple files, how many seconds to display
 *                   variants of each file before switching to the next.
 *                   If unspecified, they all go in the mix at once with
 *                   short duration.
 *    --powerup      Do the power-on animation at the beginning, and
 *                   fade to black at the end.
 *    --logo FILE    Small image overlayed onto the colorbars image.
 *
 *  Created: 10-Dec-2018 by jwz.
 */

#include "precomp.hpp"

#include "fixed-funcs.hpp"
#include "yarandom.hpp"
#include "thread_util.hpp"
#include "analogtv.hpp"

#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>


const char *progname;
const char *progclass;
static int verbose_p = 0;

#define MAX_MULTICHAN 2
static int N_CHANNELS=12;
static int MAX_STATIONS=6;

#define POWERUP_DURATION   6  /* Hardcoded in analogtv.c */
#define POWERDOWN_DURATION 1  /* Only used here */

typedef struct chansetting_s {
  analogtv_reception recs[MAX_MULTICHAN];
  double noise_level;
} chansetting;

struct state
{
  state() : 
  outBuffer(),
  logoImg(),
  logoMask(),
  tv(),
  n_stations(),
  stations(),
  image_loading_p(),
  curinputi(),
  chansettings(),
  cs()
  { }

  cv::Mat outBuffer, logoImg, logoMask;
  analogtv *tv;

  int n_stations;
  analogtv_input **stations;
  bool image_loading_p;

  int curinputi;
  chansetting *chansettings;
  chansetting *cs;
};

static struct state global_state;


XImage fromCvMat(cv::Mat& m)
{
  assert(!m.empty());
  assert(m.type() == CV_8UC4);

  XImage image;
  image.width  = m.cols;
  image.height = m.rows;
  image.bytes_per_line = m.step;
  image.data = (char*)m.data;

  return image;
}

cv::Mat fromXImage(XImage* image)
{
  cv::Mat m(image->height, image->width, CV_8UC4, (uchar*)image->data);

  return m;
}


/* Since this program does not connect to an X server, or in fact link
   with Xlib, we need stubs for the few X11 routines that analogtv.c calls.
   Most are unused. It seems like I am forever implementing subsets of X11.
 */

int
custom_XGetWindowAttributes (XWindowAttributes *xgwa)
{
  struct state *st = &global_state;
  memset (xgwa, 0, sizeof(*xgwa));
  xgwa->width = st->outBuffer.cols;
  xgwa->height = st->outBuffer.rows;
  return true;
}

int
custom_XPutImage (XImage *image, 
           int src_x, int src_y, int dest_x, int dest_y,
           unsigned int w, unsigned int h)
{
  struct state *st = &global_state;
  cv::Mat img = fromXImage(image);

  if (src_x < 0)
  {
    w += src_x;
    dest_x -= src_x;
    src_x = 0;
  }
  if (dest_x < 0)
  {
    w += dest_x;
    src_x -= dest_x;
    dest_x = 0;
  }
  w = std::min((int)w, std::min(st->outBuffer.cols - dest_x, image->width - src_x));

  if (src_y < 0)
  {
    h += src_y;
    dest_y -= src_y;
    src_y = 0;
  }
  if (dest_y < 0)
  {
    h += dest_y;
    src_y -= dest_y;
    dest_y = 0;
  }
  h = std::min((int)h, std::min(st->outBuffer.rows - dest_y, image->height - src_y));

  img(cv::Rect(src_x, src_y, w, h)).copyTo(st->outBuffer(cv::Rect(dest_x, dest_y, w, h)));

  return 0;
}

static int darkp = 0;
double
get_float_resource (char *name)
{
  if (!strcmp(name, "TVTint")) return 5;		/* default 5   */
  if (!strcmp(name, "TVColor")) return 70;		/* default 70  */
  if (!strcmp(name, "TVBrightness"))
    return (darkp ? -15 : 2);				/* default 2   */
  if (!strcmp(name, "TVContrast")) return 150;		/* default 150 */
  abort();
}

static void
update_smpte_colorbars(analogtv_input *input)
{
  struct state *st = (struct state *) input->client_data;
  int col;
  int black_ntsc[4];

  /* 
     SMPTE is the society of motion picture and television engineers, and
     these are the standard color bars in the US. Following the partial spec
     at http://broadcastengineering.com/ar/broadcasting_inside_color_bars/
     These are luma, chroma, and phase numbers for each of the 7 bars.
  */
  double top_cb_table[7][3]={
    {75, 0, 0.0},    /* gray */
    {69, 31, 167.0}, /* yellow */
    {56, 44, 283.5}, /* cyan */
    {48, 41, 240.5}, /* green */
    {36, 41, 60.5},  /* magenta */
    {28, 44, 103.5}, /* red */
    {15, 31, 347.0}  /* blue */
  };
  double mid_cb_table[7][3]={
    {15, 31, 347.0}, /* blue */
    {7, 0, 0},       /* black */
    {36, 41, 60.5},  /* magenta */
    {7, 0, 0},       /* black */
    {56, 44, 283.5}, /* cyan */
    {7, 0, 0},       /* black */
    {75, 0, 0.0}     /* gray */
  };

  analogtv_lcp_to_ntsc(0.0, 0.0, 0.0, black_ntsc);

  analogtv_setup_sync(input, 1, 0);

  for (col=0; col<7; col++) {
    analogtv_draw_solid_rel_lcp (input,
                                 col*(1.0/7.0),
                                 (col+1)*(1.0/7.0),
                                 0.00, 0.68, 
                                 top_cb_table[col][0], 
                                 top_cb_table[col][1],
                                 top_cb_table[col][2]);
    
    analogtv_draw_solid_rel_lcp(input,
                                col*(1.0/7.0),
                                (col+1)*(1.0/7.0),
                                0.68, 0.75, 
                                mid_cb_table[col][0], 
                                mid_cb_table[col][1],
                                mid_cb_table[col][2]);
  }

  analogtv_draw_solid_rel_lcp(input, 0.0, 1.0/6.0,
                              0.75, 1.00, 7, 40, 303);   /* -I */
  analogtv_draw_solid_rel_lcp(input, 1.0/6.0, 2.0/6.0,
                              0.75, 1.00, 100, 0, 0);    /* white */
  analogtv_draw_solid_rel_lcp(input, 2.0/6.0, 3.0/6.0,
                              0.75, 1.00, 7, 40, 33);    /* +Q */
  analogtv_draw_solid_rel_lcp(input, 3.0/6.0, 4.0/6.0,
                              0.75, 1.00, 7, 0, 0);      /* black */
  analogtv_draw_solid_rel_lcp(input, 12.0/18.0, 13.0/18.0,
                              0.75, 1.00, 3, 0, 0);      /* black -4 */
  analogtv_draw_solid_rel_lcp(input, 13.0/18.0, 14.0/18.0,
                              0.75, 1.00, 7, 0, 0);      /* black */
  analogtv_draw_solid_rel_lcp(input, 14.0/18.0, 15.0/18.0,
                              0.75, 1.00, 11, 0, 0);     /* black +4 */
  analogtv_draw_solid_rel_lcp(input, 5.0/6.0, 6.0/6.0,
                              0.75, 1.00, 7, 0, 0);      /* black */
  if (!st->logoImg.empty())
    {
      double aspect = (double)st->outBuffer.cols / st->outBuffer.rows;
      double scale = (aspect > 1 ? 0.35 : 0.6);
      int w2 = st->tv->xgwa.width  * scale;
      int h2 = st->tv->xgwa.height * scale * aspect;
      analogtv_load_ximage (st->tv, input, fromCvMat(st->logoImg), fromCvMat(st->logoMask),
                            (st->tv->xgwa.width - w2) / 2,
                            st->tv->xgwa.height * 0.20,
                            w2, h2);
    }

  input->next_update_time += 1.0;
}


cv::Mat loadImage(std::string fname)
{
  assert(!fname.empty());

  cv::Mat img = cv::imread(fname, cv::IMREAD_UNCHANGED);
  //TODO: BGR to RGB?
  //cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

  if (img.empty())
  {
    std::cout << "Failed to load image " << fname << std::endl;
    abort();
  }

  if (img.depth() != CV_8U)
  {
    std::cout << "Image depth is not 8 bit: " << fname << std::endl;
    abort();
  }

  cv::Mat cvt4;
  if (img.channels() == 1)
  {
    cv::cvtColor(img, cvt4, cv::COLOR_GRAY2BGRA);
  }
  else if (img.channels() == 3)
  {
    cv::cvtColor(img, cvt4, cv::COLOR_BGR2BGRA);
  }
  else if (img.channels() == 4)
  {
    cvt4 = img;
  }
  else
  {
    std::cout << "Unknown format for file " << fname << std::endl;
    abort();
  }

  cv::flip(cvt4, cvt4, 0);

  return cvt4;
}


static void
analogtv_convert (const char **infiles, const char *outfile,
                  const char *logofile,
                  int output_w, int output_h,
                  int duration, int slideshow, bool powerp)
{
  unsigned long start_time = time((time_t *)0);
  int i;
  int nfiles;
  unsigned long curticks = 0, curticks_sub = 0;
  time_t lastlog = time((time_t *)0);
  int frames_left = 0;
  int channel_changes = 0;
  int fps = 30;
  std::vector<cv::Mat> images;
  cv::Mat baseImage;
  int *stats;
  cv::Ptr<cv::VideoWriter> writer;

  /* Load all of the input images.
   */
  stats = (int *) calloc(N_CHANNELS, sizeof(*stats));
  for (nfiles = 0; infiles[nfiles]; nfiles++)
    ;

  {
    int maxw = 0, maxh = 0;
    for (i = 0; i < nfiles; i++)
      {
        cv::Mat img = loadImage(infiles[i]);
        images.push_back(img);
        if (verbose_p > 1)
          fprintf (stderr, "%s: loaded %s %dx%d\n", progname, infiles[i],
                   img.cols, img.rows);
        flip(img, img, 0);
        maxw = std::max(maxw, img.cols);
        maxh = std::max(maxh, img.rows);
      }

    if (!output_w || !output_h) {
      output_w = maxw;
      output_h = maxh;
    }
  }

  output_w &= ~1;  /* can't be odd */
  output_h &= ~1;

  /* Scale all of the input images to the size of the largest one, or frame.
   */
  for (i = 0; i < nfiles; i++)
    {
      cv::Mat img = images[i];
      if (img.size() != cv::Size(output_w, output_h))
        {
          double r1 = (double) output_w / output_h;
          double r2 = (double) img.cols / img.rows;
          int w2, h2;
          if (r1 > r2)
            {
              w2 = output_h * r2;
              h2 = output_h;
            }
          else
            {
              w2 = output_w;
              h2 = output_w / r2;
            }
          cv::Mat out;
          cv::resize(img, out, cv::Size(w2, h2));
          images[i] = out;
        }
    }

  struct state *st = &global_state;

  st->outBuffer = cv::Mat(output_h, output_w, CV_8UC4, cv::Scalar(0));

  if (logofile)
  {
    st->logoImg = loadImage(logofile);
    if (verbose_p)
    {
      fprintf (stderr, "%s: loaded %s %dx%d\n", 
               progname, logofile, st->logoImg.cols, st->logoImg.rows);
    }
    cv::flip(st->logoImg, st->logoImg, 0);

    /* Pull the alpha out of the logo and make a separate mask ximage. */
    st->logoMask = cv::Mat(st->logoImg.size(), CV_8UC4, cv::Scalar(0));
    std::vector<cv::Mat> logoCh;
    cv::split(st->logoImg, logoCh);
    cv::Mat z = cv::Mat(st->logoImg.size(), CV_8UC1, cv::Scalar(0));
    cv::merge(std::vector<cv::Mat> {logoCh[0], logoCh[1], logoCh[2], z}, st->logoImg);
    cv::merge(std::vector<cv::Mat> {z, z, z, logoCh[3]}, st->logoMask);
  }

  st->tv=analogtv_allocate();

  st->stations = (analogtv_input **)
    calloc (MAX_STATIONS, sizeof(*st->stations));
  while (st->n_stations < MAX_STATIONS) {
    analogtv_input *input=analogtv_input_allocate();
    st->stations[st->n_stations++]=input;
    input->client_data = st;
  }

  analogtv_set_defaults(st->tv, "");

  if (ya_random()%4==0) {
    st->tv->tint_control += pow(ya_frand(2.0)-1.0, 7) * 180.0;
  }
  if (1) {
    st->tv->color_control += ya_frand(0.3) * ((ya_random() & 1) ? 1 : -1);
  }
  if (darkp) {
    if (ya_random()%4==0) {
      st->tv->brightness_control += ya_frand(0.15);
    }
    if (ya_random()%4==0) {
      st->tv->contrast_control += ya_frand(0.2) * ((ya_random() & 1) ? 1 : -1);
    }
  }

  st->chansettings = (chansetting *)calloc (N_CHANNELS, sizeof (*st->chansettings));
  for (i = 0; i < N_CHANNELS; i++) {
    st->chansettings[i].noise_level = 0.06;
    {
      int last_station=42;
      int stati;
      for (stati=0; stati<MAX_MULTICHAN; stati++) {
        analogtv_reception *rec=&st->chansettings[i].recs[stati];
        int station;
        while (1) {
          station=ya_random()%st->n_stations;
          if (station!=last_station) break;
          if ((ya_random()%10)==0) break;
        }
        last_station=station;
        rec->input = st->stations[station];
        rec->level = pow(ya_frand(1.0), 3.0) * 2.0 + 0.05;
        rec->ofs=ya_random()%ANALOGTV_SIGNAL_LEN;
        if (ya_random()%3) {
          rec->multipath = ya_frand(1.0);
        } else {
          rec->multipath=0.0;
        }
        if (stati) {
          /* We only set a frequency error for ghosting stations,
             because it doesn't matter otherwise */
          rec->freqerr = (ya_frand(2.0)-1.0) * 3.0;
        }

        if (rec->level > 0.3) break;
        if (ya_random()%4) break;
      }
    }
  }

  st->curinputi=0;
  st->cs = &st->chansettings[st->curinputi];

  // used with ffmpeg:
  //const enum AVCodecID video_codec = AV_CODEC_ID_H264;
  //const enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;

  //writer = cv::makePtr<cv::VideoWriter>(outfile + std::string("_ocv.avi"), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
  // writer = cv::makePtr<cv::VideoWriter>(outfile + std::string("_ocv.mp4"), cv::VideoWriter::fourcc('M', 'P', '4', 'V'),
  //                                       30, cv::Size(st->output_frame->width, st->output_frame->height));
  // if (!writer->isOpened())
  // {
  //   printf("VideoWriter is not opened!\n");
  //   abort();
  // }
  cv::namedWindow("tv");

 INIT_CHANNELS:

  curticks_sub = 0;
  channel_changes = 0;
  st->curinputi = 0;
  st->tv->powerup = 0.0;

  if (slideshow)
    /* First channel (initial unadulterated image) stays for this long */
    frames_left = fps * (2 + ya_frand(1.5));

  if (slideshow)
  {
    /* Pick one of the input images and fill all channels with variants
       of it.
     */
    int n = ya_random() % nfiles;
    cv::Mat img = images[n];
    baseImage = img;
    if (verbose_p > 1)
      fprintf (stderr, "%s: initializing for %s %dx%d in %d channels\n", 
               progname, infiles[n], img.cols, img.rows,
               MAX_STATIONS);

    for (i = 0; i < MAX_STATIONS; i++) {
      analogtv_input *input = st->stations[i];
      int w = img.cols * 0.815;  /* underscan */
      int h = img.rows * 0.970;
      int x = (output_w - w) / 2;
      int y = (output_h - h) / 2;

      if (i == 1) {   /* station 0 is the unadulterated image.
                         station 1 is colorbars. */
        input->updater = update_smpte_colorbars;
      }

      analogtv_setup_sync (input, 1, (ya_random()%20)==0);
      analogtv_load_ximage (st->tv, input, fromCvMat(img), XImage(), x, y, w, h);
    }
  }
  else
  {
    /* Fill all channels with images */
    if (verbose_p > 1)
      fprintf (stderr, "%s: initializing %d files in %d channels\n",
               progname, nfiles, MAX_STATIONS);

    for (i = 0; i < MAX_STATIONS; i++)
    {
      cv::Mat img = images[i % nfiles];
      analogtv_input *input = st->stations[i];
      int w = img.cols * 0.815;  /* underscan */
      int h = img.rows * 0.970;
      int x = (output_w - w) / 2;
      int y = (output_h - h) / 2;

      if (! (ya_random() % 8))  /* Some stations are colorbars */
        input->updater = update_smpte_colorbars;

      analogtv_setup_sync (input, 1, (ya_random()%20)==0);
      analogtv_load_ximage (st->tv, input, fromCvMat(img), XImage(), x, y, w, h);
    }
  }

  /* This is xanalogtv_draw()
   */
  while (1) {
    const analogtv_reception *recs[MAX_MULTICHAN];
    unsigned rec_count = 0;
    double curtime = curticks * 0.001;
    double curtime_sub = curticks_sub * 0.001;

    frames_left--;
    if (frames_left <= 0 &&
        (!powerp || curticks > POWERUP_DURATION*1000)) {

      channel_changes++;

      if (slideshow && channel_changes == 1) {
        /* Second channel has short duration, 0.25 to 0.75 sec. */
        frames_left = fps * (0.25 + ya_frand(0.5));
      } else if (slideshow) {
        /* 0.5 - 2.0 sec (was 0.5 - 3.0 sec) */
        frames_left = fps * (0.5 + ya_frand(1.5));
      } else {
        /* 1 - 7 sec */
        frames_left = fps * (1 + ya_frand(6));
      }

      if (slideshow && channel_changes == 2) {
        /* Always use the unadulterated image for the third channel:
           So the effect is, plain, brief blip, plain, then random. */
        st->curinputi = 0;
        frames_left += fps * (0.1 + ya_frand(0.5));

      } else if (slideshow && st->curinputi != 0 && ((ya_random() % 100) < 75)) {
        /* Use the unadulterated image 75% of the time (was 33%) */
        st->curinputi = 0;
      } else {
        /* Otherwise random */
        int prev = st->curinputi;
      AGAIN:
        st->curinputi = 1 + (ya_random() % (N_CHANNELS - 1));

        /* In slideshow mode, always alternate to the unadulterated image:
           no two noisy images in a row, always intersperse clean. */
        if (slideshow && prev != 0)
          st->curinputi = 0;

        /* In slideshow mode, do colorbars-only a bit less often. */
        if (slideshow && st->curinputi == 1 && !(ya_random() % 3))
          goto AGAIN;
      }

      stats[st->curinputi]++;
      st->cs = &st->chansettings[st->curinputi];
      /* Set channel change noise flag */
      st->tv->channel_change_cycles=200000;

      if (verbose_p > 1)
        fprintf (stderr, "%s: %5.1f sec: channel %d\n",
                 progname, curticks/1000.0, st->curinputi);

      /* Turn the knobs every now and then */
      if (! (ya_random() % 5)) {
        if (ya_random()%4==0) {
          st->tv->tint_control += pow(ya_frand(2.0)-1.0, 7) * 180.0 * ((ya_random() & 1) ? 1 : -1);
        }
        if (1) {
          st->tv->color_control += ya_frand(0.3) * ((ya_random() & 1) ? 1 : -1);
        }
        if (darkp) {
          if (ya_random()%4==0) {
            st->tv->brightness_control += ya_frand(0.15);
          }
          if (ya_random()%4==0) {
            st->tv->contrast_control += ya_frand(0.2) * ((ya_random() & 1) ? 1 : -1);
          }
        }
      }
    }

    for (i=0; i<MAX_MULTICHAN; i++) {
      analogtv_reception *rec = &st->cs->recs[i];
      analogtv_input *inp=rec->input;
      if (!inp) continue;

      if (inp->updater) {
        inp->next_update_time = curtime;
        (inp->updater)(inp);
      }
      rec->ofs += rec->freqerr;
    }

    st->tv->powerup=(powerp ? curtime : 9999);

    for (i=0; i<MAX_MULTICHAN; i++) {
      /* Noisy image */
      analogtv_reception *rec = &st->cs->recs[i];
      if (rec->input) {
        analogtv_reception_update(rec);
        recs[rec_count] = rec;
        ++rec_count;
      }

      analogtv_draw (st->tv, st->cs->noise_level, recs, rec_count);

      if (slideshow && st->curinputi == 0 &&
          (!powerp || curticks > POWERUP_DURATION*1000))
      {
        /* Unadulterated image centered on top of border of static */
        cv::Point tl((output_w - baseImage.cols) / 2, (output_h - baseImage.rows) / 2);
        baseImage.copyTo(st->outBuffer(cv::Rect(tl, baseImage.size())));
      }
    }

    cv::Mat m3;
    cvtColor(st->outBuffer, m3, cv::COLOR_BGRA2BGR);
    // writer->write(m3);
    cv::imshow("tv", m3);
    cv::waitKey(33);

    if (powerp &&
        curticks > (unsigned int)((duration*1000) - (POWERDOWN_DURATION*1000))) {
      /* Fade out, as there is no power-down animation. */
      double r = ((duration*1000 - curticks) /
                  (double) (POWERDOWN_DURATION*1000));
      static double ob = 9999;
      double min = -1.5;  /* Usable range is something like -0.75 to 1.0 */
      if (ob == 9999) ob = st->tv->brightness_control;  /* Terrible */
      st->tv->brightness_control = min + (ob - min) * r;
    }

    if (curtime >= duration) break;

    if (slideshow && curtime_sub >= slideshow)
      goto INIT_CHANNELS;

    curticks     += 1000/fps;
    curticks_sub += 1000/fps;

    if (verbose_p) {
      unsigned long now = time((time_t *)0);
      if (now > (unsigned int)(verbose_p == 1 ? lastlog : lastlog + 10)) {
        unsigned long elapsed = now - start_time;
        double ratio = curtime / (double) duration;
        int remaining = (ratio ? (elapsed / ratio) - elapsed : 0);
        int pct = 100 * ratio;
        int cols = 47;
        char dots[80];
        int ii;
        for (ii = 0; ii < cols * ratio; ii++)
          dots[ii] = '.';
        dots[ii] = 0;
        fprintf (stderr, "%s%s: %s %2d%%, %d:%02d:%02d ETA%s",
                 (verbose_p == 1 ? "\r" : ""),
                 progname,
                 dots, pct, 
                 (remaining/60/60),
                 (remaining/60)%60,
                 remaining%60,
                 (verbose_p == 1 ? "" : "\n"));
        lastlog = now;
      }
    }
  }

  if (verbose_p == 1) fprintf(stderr, "\n");

  if (verbose_p > 1) {
    if (channel_changes == 0) channel_changes++;
    fprintf(stderr, "%s: channels shown: %d\n", progname, channel_changes);
    for (i = 0; i < N_CHANNELS; i++)
      fprintf(stderr, "%s:   %2d: %3d%%\n", progname,
              i+1, stats[i] * 100 / channel_changes);
  }

  free (stats);
  //writer->release();
  cv::destroyAllWindows();
}


static void
usage(const char *err)
{
  if (err) fprintf (stderr, "%s: %s unknown\n", progname, err);
  fprintf (stderr, "usage: %s [--verbose] [--duration secs] [--slideshow secs]"
           " [--powerup] [--size WxH]"
           " infile.png ... outfile.mp4\n",
           progname);
  exit (1);
}

int
main (int argc, char **argv)
{
  int i;
  const char *infiles[1000];
  const char *outfile = 0;
  int duration = 30;
  bool powerp = false;
  char *logo = 0;
  int w = 0, h = 0;
  int nfiles = 0;
  int slideshow = 0;

  char *s = strrchr (argv[0], '/');
  progname = s ? s+1 : argv[0];
  progclass = progname;

  memset (infiles, 0, sizeof(infiles));

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-' && argv[i][1] == '-')
        argv[i]++;
       if (!strcmp(argv[i], "-v") ||
           !strcmp(argv[i], "-verbose"))
        verbose_p++;
       else if (!strcmp(argv[i], "-vv")) verbose_p += 2;
       else if (!strcmp(argv[i], "-vvv")) verbose_p += 3;
       else if (!strcmp(argv[i], "-vvvv")) verbose_p += 4;
       else if (!strcmp(argv[i], "-vvvvv")) verbose_p += 5;
       else if (!strcmp(argv[i], "-duration") && argv[i+1])
         {
           char dummy;
           i++;
           if (1 != sscanf (argv[i], " %d %c", &duration, &dummy))
             usage(argv[i]);
         }
       else if (!strcmp(argv[i], "-slideshow") && argv[i+1])
         {
           char dummy;
           i++;
           if (1 != sscanf (argv[i], " %d %c", &slideshow, &dummy))
             usage(argv[i]);
         }
       else if (!strcmp(argv[i], "-size") && argv[i+1])
         {
           char dummy;
           i++;
           if (2 != sscanf (argv[i], " %d x %d %c", &w, &h, &dummy))
             usage(argv[i]);
         }
       else if (!strcmp(argv[i], "-logo") && argv[i+1])
         logo = argv[++i];
       else if (!strcmp(argv[i], "-powerup") ||
                !strcmp(argv[i], "-power"))
         powerp = true;
       else if (!strcmp(argv[i], "-no-powerup") ||
                !strcmp(argv[i], "-no-power"))
         powerp = false;
      else if (argv[i][0] == '-')
        usage(argv[i]);
      else if (nfiles >= (int)(sizeof(infiles)/sizeof(*infiles))-1)
        usage("too many files");
      else
        infiles[nfiles++] = argv[i];
    }

  if (nfiles < 2)
    usage("");

  outfile = infiles[nfiles-1];
  infiles[--nfiles] = 0;

  if (nfiles == 1)
    slideshow = duration;

  /* stations should be a multiple of files, but >= 6.
     channels should be double that. */
  MAX_STATIONS = 6;
  if (! slideshow) {
    MAX_STATIONS = 0;
    while (MAX_STATIONS < 6)
      MAX_STATIONS += nfiles;
    MAX_STATIONS *= 2;
  }
  N_CHANNELS = MAX_STATIONS * 2;

  darkp = (nfiles == 1);

  ya_rand_init (0);
  analogtv_convert (infiles, outfile, logo,
                    w, h, duration, slideshow, powerp);
  exit (0);
}
