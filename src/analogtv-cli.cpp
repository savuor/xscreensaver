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
#include <set>
#include <map>
#include <optional>
#include <variant>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

struct Output
{
  Output() { }

  static std::shared_ptr<Output> create(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) = 0;

  virtual ~Output() { }
};

struct HighguiOutput : Output
{
  HighguiOutput()
  {
    cv::namedWindow("tv");
  }

  void send(const cv::Mat& m) override
  {
    cv::imshow("tv", m);
    cv::waitKey(1);
  }

  ~HighguiOutput()
  {
    cv::destroyAllWindows();
  }
};

struct VideoOutput : Output
{
  // used with ffmpeg:
  //const enum AVCodecID video_codec = AV_CODEC_ID_H264;
  //const enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;

  VideoOutput(const std::string& s, cv::Size imgSize)
  {
    // cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
    if (!writer.open(s, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), /* fps */ 30, imgSize))
    {
      throw std::runtime_error("Failed to open VideoWriter");
    }
    //TODO: logger
    //if (verbose_p > 1)
    {
      fprintf(stderr, "%s: opened %s %dx%d\n", progname, s.c_str(), imgSize.width, imgSize.height);
    }
  }

  virtual void send(const cv::Mat& m) override
  {
    cv::Mat out = m;
    if (m.channels() == 4)
    {
      cvtColor(m, out, cv::COLOR_BGRA2BGR);
    }
    writer.write(out);
  }

  ~VideoOutput() { }

  cv::VideoWriter writer;
};

std::shared_ptr<Output> Output::create(const std::string& s, cv::Size imgSize)
{
  if (s.at(0) == ':')
  {
    std::string name = s.substr(1, s.length() - 1);
    if (name == "highgui")
    {
      return std::make_shared<HighguiOutput>();
    }
    else
    {
      throw std::runtime_error("Unknown video output: " + name);
    }
  }
  else
  {
    return std::make_shared<VideoOutput>(s, imgSize);
  }
}

struct Params
{
  int         verbosity;
  int         duration;
  int         slideshow;
  int         seed;
  bool        powerup;
  cv::Size    size;
  std::string logoFname;

  std::vector<std::string> sources;
  std::vector<std::string> outputs;
};

const char *progname;
const char *progclass;
static int verbose_p = 0;

#define MAX_MULTICHAN 2

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
  stations(),
  curinputi(),
  chanSettings(),
  cs(),
  outputs()
  { }

  cv::Mat outBuffer, logoImg, logoMask;
  analogtv *tv;

  std::vector<analogtv_input*> stations;

  int curinputi;
  std::vector<chansetting> chanSettings;
  chansetting *cs;

  std::vector<std::shared_ptr<Output>> outputs;
};

//TODO: refactor it
static cv::Mat globalOutBuffer;

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
custom_XPutImage (XImage *image, 
           int src_x, int src_y, int dest_x, int dest_y,
           unsigned int w, unsigned int h)
{
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
  w = std::min((int)w, std::min(globalOutBuffer.cols - dest_x, image->width - src_x));

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
  h = std::min((int)h, std::min(globalOutBuffer.rows - dest_y, image->height - src_y));

  img(cv::Rect(src_x, src_y, w, h)).copyTo(globalOutBuffer(cv::Rect(dest_x, dest_y, w, h)));

  return 0;
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
      int w2 = st->tv->outbuffer_width  * scale;
      int h2 = st->tv->outbuffer_height * scale * aspect;
      analogtv_load_ximage (st->tv, input, fromCvMat(st->logoImg), fromCvMat(st->logoMask),
                            (st->tv->outbuffer_width - w2) / 2,
                            st->tv->outbuffer_height * 0.20,
                            w2, h2);
    }

  input->next_update_time += 1.0;
}


cv::Mat loadImage(const std::string& fname)
{
  assert(!fname.empty());

  cv::Mat img = cv::imread(fname, cv::IMREAD_UNCHANGED);

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
    //TODO: BGR to RGB?
    cv::cvtColor(img, cvt4, cv::COLOR_BGR2BGRA);
    //cv::cvtColor(img, cvt4, cv::COLOR_BGR2RGBA);
  }
  else if (img.channels() == 4)
  {
    //TODO: BGR to RGB?
    //cv::cvtColor(img, cvt4, cv::COLOR_BGRA2RGBA);
    cvt4 = img;
  }
  else
  {
    std::cout << "Unknown format for file " << fname << std::endl;
    abort();
  }

  return cvt4;
}


static void run(Params params)
{
  std::vector<const char*> inVec;
  for (const auto& f : params.sources)
  {
    inVec.push_back(f.c_str());
  }
  inVec.push_back(nullptr);

  verbose_p = params.verbosity;

  int nFiles = params.sources.size();
  if (nFiles == 1)
  {
    params.slideshow = params.duration;
  }

  /* stations should be a multiple of files, but >= 6.
     channels should be double that. */
  int MAX_STATIONS = 6;
  if (!params.slideshow)
  {
    MAX_STATIONS = 0;
    while (MAX_STATIONS < 6)
    {
      MAX_STATIONS += nFiles;
    }
    MAX_STATIONS *= 2;
  }
  int N_CHANNELS = MAX_STATIONS * 2;

  ya_rand_init (params.seed);
  int output_w = params.size.width, output_h = params.size.height;
  int duration = params.duration, slideshow = params.slideshow;
  bool powerp = params.powerup;

  unsigned long start_time = time((time_t *)0);
  int i;

  unsigned long curticks = 0, curticks_sub = 0;
  time_t lastlog = time((time_t *)0);
  int frames_left = 0;
  int channel_changes = 0;
  int fps = 30;
  std::vector<cv::Mat> images;
  cv::Mat baseImage;
  int *stats;

  /* Load all of the input images.
   */
  stats = (int *) calloc(N_CHANNELS, sizeof(*stats));

  int maxw = 0, maxh = 0;
  for (i = 0; i < nFiles; i++)
  {
    //TODO: sources
    std::string fname = params.sources[i];
    cv::Mat img = loadImage(fname);
    images.push_back(img);
    if (verbose_p > 1)
    {
      fprintf(stderr, "%s: loaded %s %dx%d\n", progname, fname.c_str(), img.cols, img.rows);
    }
    maxw = std::max(maxw, img.cols);
    maxh = std::max(maxh, img.rows);
  }

  if (!output_w || !output_h)
  {
    output_w = maxw;
    output_h = maxh;
  }

  output_w &= ~1;  /* can't be odd */
  output_h &= ~1;

  /* Scale all of the input images to the size of the largest one, or frame.
   */
  for (i = 0; i < nFiles; i++)
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

  state runState;
  state* st = &runState;

  st->outBuffer = cv::Mat(output_h, output_w, CV_8UC4, cv::Scalar(0));
  globalOutBuffer = st->outBuffer;

  if (!params.logoFname.empty())
  {
    st->logoImg = loadImage(params.logoFname);
    if (verbose_p)
    {
      fprintf (stderr, "%s: loaded %s %dx%d\n", 
               progname, params.logoFname.c_str(), st->logoImg.cols, st->logoImg.rows);
    }

    /* Pull the alpha out of the logo and make a separate mask ximage. */
    st->logoMask = cv::Mat(st->logoImg.size(), CV_8UC4, cv::Scalar(0));
    std::vector<cv::Mat> logoCh;
    cv::split(st->logoImg, logoCh);
    cv::Mat z = cv::Mat(st->logoImg.size(), CV_8UC1, cv::Scalar(0));
    cv::merge(std::vector<cv::Mat> {logoCh[0], logoCh[1], logoCh[2], z}, st->logoImg);
    cv::merge(std::vector<cv::Mat> {z, z, z, logoCh[3]}, st->logoMask);
  }

  st->tv = analogtv_allocate(output_w, output_h);

  for (int i = 0; i < MAX_STATIONS; i++)
  {
    analogtv_input *input=analogtv_input_allocate();
    input->client_data = st;
    st->stations.push_back(input);
  }

  analogtv_set_defaults(st->tv);

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

  st->chanSettings.resize(N_CHANNELS);
  for (i = 0; i < N_CHANNELS; i++)
  {
    st->chanSettings[i].noise_level = 0.06;

    int last_station=42;
    for (int stati=0; stati<MAX_MULTICHAN; stati++)
    {
        analogtv_reception *rec = &st->chanSettings[i].recs[stati];
        int station;
        while (1)
        {
          station=ya_random()%(st->stations.size());
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
  st->cs = &st->chanSettings[st->curinputi];

  for (const auto& s : params.outputs)
  {
    st->outputs.emplace_back(Output::create(s, st->outBuffer.size()));
  }

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

    //TODO: won't work when sources are implemented
    int n = ya_random() % nFiles;
    cv::Mat img = images[n];
    baseImage = img;
    if (verbose_p > 1)
    {
      fprintf (stderr, "%s: initializing for %s %dx%d in %d channels\n", 
               progname, params.sources[n].c_str(), img.cols, img.rows,
               MAX_STATIONS);
    }

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
               progname, nFiles, MAX_STATIONS);

    for (i = 0; i < MAX_STATIONS; i++)
    {
      cv::Mat img = images[i % nFiles];
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
  while (1)
  {
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
      st->cs = &st->chanSettings[st->curinputi];
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

    for (i=0; i<MAX_MULTICHAN; i++)
    {
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

    for (const auto& o : st->outputs)
    {
      o->send(st->outBuffer);
    }

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
}


struct CmdArgument
{
  enum class Type
  {
    BOOL, INT, LIST_INT, STRING, LIST_STRING
  };

  Type type;
  bool optional;
  std::string exampleArgs;
  std::vector<std::string> help;

  CmdArgument(const std::string& _exampleArgs, const Type& _type,
              bool _optional, const std::string& _help) :
    type(_type),
    optional(_optional),
    exampleArgs(_exampleArgs),
    help()
  {
    size_t p0 = 0;
    for (size_t i = 0; i < _help.length(); i++)
    {
      if (_help.at(i) == '\n')
      {
        help.push_back(_help.substr(p0, i - p0));
        p0 = i+1;
      }
    }
    if (p0 < _help.length())
    {
      help.push_back(_help.substr(p0, _help.length() - p0 - 1));
    }
  }
};


static const std::map<std::string, CmdArgument> knownArgs =
{
    // name, exampleArgs, type, optional, help
    {"verbose",
      { "n",     CmdArgument::Type::INT, true, "level of verbosity from 0 to 5" }},
    {"duration",
      { "secs",  CmdArgument::Type::INT, false, "length of video in secs, e.g. 60" }},
    {"slideshow",
      { "secs",  CmdArgument::Type::INT, true, "how many secs to wait in slideshow mode, e.g. 5" }},
    {"powerup",
      { "",      CmdArgument::Type::BOOL, true, "to run power up sequence or not" }},
    {"size",
      { "width height", CmdArgument::Type::LIST_INT, true, "use different size than maximum of given images" }},
    {"logo",
      { "file",  CmdArgument::Type::STRING, true, "logo image to display over color bars" }},
    {"seed",
      { "value", CmdArgument::Type::INT, true, "random seed to start random generator or 0 to randomize by current date and time" }},
    {"in",
      { "src1 [src2 ... srcN]", CmdArgument::Type::LIST_STRING, false,
          "signal sources: still images, video files (not implemented yet) or special sources:\n"
          "  * :cam0 to :cam9 are camera sources (not implemented yet)\n"
          "  * :bars are SMPTE color bars (if it's the only image and no size is given then the output size will be 320x240)\n"
          "    (not implemented yet)" }},
    {"out",
      { "out1 [out2 ... outN]", CmdArgument::Type::LIST_STRING, false,
          "where to output video: video files or window, output to all sources happens simultaneously\n"
          "  * :highgui means output to window using OpenCV HighGUI module, stable FPS is not guaranteed" }}
};

void showUsage()
{
  std::cout << "Shows images or videos like they are on an old TV screen" << std::endl;
  std::cout << "Based on analogtv hack written by Trevor Blackwell (https://tlb.org/)" << std::endl;
  std::cout << "from XScreensaver (https://www.jwz.org/xscreensaver/) by Jamie Zawinski (https://jwz.org/) and the team" << std::endl;

  std::cout << "Usage: analogtv-cli";
  for (const auto& [k, v] : knownArgs)
  {
    if (!v.optional)
    {
      std::cout << " --" << k << " " << v.exampleArgs;
    }
  }
  std::cout << " [other keys are optional]" << std::endl;

  std::cout << "Keys:" << std::endl;
  for (const auto& [k, v] : knownArgs)
  {
    std::cout << "    --" << k << std::string(12 - k.length(), ' ');
    std::cout << v.exampleArgs << std::endl;
    for (const auto& hs : v.help)
    {
      std::cout << "      " << hs << std::endl;
    }
  }
}

std::optional<int> parseInt(const std::string& s)
{
  int v;
  size_t at;
  try
  {
    v = std::stoi(s, &at);
  }
  catch (std::invalid_argument const &ex)
  {
    std::cout << "Failed to parse " << s << ": " << ex.what() << std::endl;
    return { };
  }
  catch (std::out_of_range const &ex)
  {
    std::cout << "Failed to parse " << s << ": " << ex.what() << std::endl;
    return { };
  }

  if (at != s.length())
  {
    std::cout << "Failed to parse " << s << ": only " << at << " symbols parsed" << std::endl;
    return { };
  }

  return v;
}

bool isArgName(const std::string& arg)
{
  return arg.length() > 2 && arg[0] == '-' && arg[1] == '-';
}


typedef std::variant<bool, int, std::string, std::vector<int>, std::vector<std::string>> ArgType;
std::map<std::string, ArgType> parseCmdArgs(int nArgs, char** argv)
{
  std::map<std::string, ArgType> usedArgs;

  for (int i = 1; i < nArgs; i++)
  {
    std::string arg(argv[i]);
    if (!isArgName(arg))
    {
      std::cout << "Argument starting from \"--\" expected, instead we got " << arg << std::endl;
      return { };
    }

    std::string name = arg.substr(2, arg.length() - 2);
    if (knownArgs.count(name) != 1)
    {
      std::cout << "Argument " << name << " is not known" << std::endl;
      return { };
    }

    if (usedArgs.count(name) > 0)
    {
      std::cout << "Argument " << name << " was already used" << std::endl;
      return { };
    }

    ArgType value;
    switch (knownArgs.at(name).type)
    {
      case CmdArgument::Type::BOOL: value = true; break;
      case CmdArgument::Type::INT:
        {
          i++;
          if (i >= nArgs)
          {
            std::cout << "Argument " << name << "requires int argument" << std::endl;
            return { };
          }
          auto v = parseInt(argv[i]);
          if (v)
          {
            value = v.value();
          }
          else
          {
            return { };
          }
          break;
        }
      case CmdArgument::Type::STRING:
        {
          i++;
          if (i >= nArgs)
          {
            std::cout << "Argument " << name << "requires string argument" << std::endl;
            return { };
          }
          value = std::string(argv[i]);
          break;
        }
      case CmdArgument::Type::LIST_INT:
        {
          std::vector<int> listInt;
          while (++i < nArgs)
          {
            if (isArgName(argv[i]))
            {
              i--; break;
            }
            auto v = parseInt(argv[i]);
            if (v)
            {
              listInt.push_back(v.value());
            }
            else
            {
              return { };
            }
          }

          if (listInt.empty())
          {
            std::cout << "Argument " << name << " requires a list of integers" << std::endl;
            return { };
          }
          value = listInt;
          break;
        }
      case CmdArgument::Type::LIST_STRING:
        {
          std::vector<std::string> listStr;
          while (++i < nArgs)
          {
            if (isArgName(argv[i]))
            {
              i--; break;
            }
            listStr.push_back(argv[i]);
          }

          if (listStr.empty())
          {
            std::cout << "Argument " << name << " requires a list of strings" << std::endl;
            return { };
          }
          value = listStr;
          break;
        }
      default: break;
    }
    usedArgs[name] = value;
  }

  return usedArgs;
}


std::optional<Params> parseParams(int args, char** argv)
{
  std::set<std::string> mandatoryArgsToFill;
  for (const auto& [k, v] : knownArgs)
  {
    if (!v.optional)
    {
      mandatoryArgsToFill.insert(k);
    }
  }

  std::map<std::string, ArgType> usedArgs = parseCmdArgs(args, argv);
  if (usedArgs.empty())
  {
    return { };
  }

  for (const auto& [name, val] : usedArgs)
  {
    if (mandatoryArgsToFill.count(name))
    {
      mandatoryArgsToFill.erase(name);
    }
  }

  if (!mandatoryArgsToFill.empty())
  {
    std::cout << "Following args are required:";
    for (const auto& k : mandatoryArgsToFill)
    {
      std::cout << " " << k;
    }
    std::cout << std::endl;
    return { };
  }

  Params p;
  p.sources  = std::get<std::vector<std::string>>(usedArgs.at("in"));
  p.outputs  = std::get<std::vector<std::string>>(usedArgs.at("out"));
  p.duration = std::get<int>(usedArgs.at("duration"));

  p.verbosity = 0;
  if (usedArgs.count("verbose"))
  {
    p.verbosity = std::get<int>(usedArgs.at("verbose"));
  }

  p.slideshow = 0;
  if (usedArgs.count("slideshow"))
  {
    p.slideshow = std::get<int>(usedArgs.at("slideshow"));
  }

  p.powerup = false;
  if (usedArgs.count("powerup"))
  {
    p.powerup = std::get<bool>(usedArgs.at("powerup"));
  }

  p.size = { };
  if (usedArgs.count("size"))
  {
    std::vector<int> l = std::get<std::vector<int>>(usedArgs.at("size"));
    if (l.size() != 2)
    {
      std::cout << "--size requires 2 integers" << std::endl;
      return { };
    }
    p.size = { l[0], l[1]};
    if (p.size.width < 64 || p.size.height < 64)
    {
      std::cout << "Image size should be bigger than 64x64" << std::endl;
      return { };
    }
  }

  p.logoFname = "";
  if (usedArgs.count("logo"))
  {
    p.logoFname = std::get<std::string>(usedArgs.at("logo"));
  }

  p.seed = 0;
  if (usedArgs.count("seed"))
  {
    p.seed = std::get<int>(usedArgs.at("seed"));
  }

  return p;
}


int main (int argc, char **argv)
{
  char *s = strrchr (argv[0], '/');
  progname = s ? s+1 : argv[0];
  progclass = progname;

  std::optional<Params> oparams = parseParams(argc, argv);
  if (!oparams)
  {
    showUsage();
    return -1;
  }

  run(oparams.value());
  exit (0);
}
