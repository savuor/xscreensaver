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
#include "utils.hpp"

#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>


struct Params
{
  int         verbosity;
  int         duration;
  int         seed;
  bool        powerup;
  bool        fixSettings;
  cv::Size    size;
  std::string logoFname;

  std::vector<std::string> sources;
  std::vector<std::string> outputs;
};

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

  cv::Mat4b outBuffer, logoImg, logoMask;
  analogtv *tv;

  std::vector<analogtv_input*> stations;

  int curinputi;
  std::vector<chansetting> chanSettings;
  chansetting *cs;

  std::vector<std::shared_ptr<Output>> outputs;
};

//TODO: refactor it
static cv::Mat4b globalOutBuffer;


/* Since this program does not connect to an X server, or in fact link
   with Xlib, we need stubs for the few X11 routines that analogtv.c calls.
   Most are unused. It seems like I am forever implementing subsets of X11.
 */

int
custom_XPutImage (const cv::Mat4b& img,
           int src_x, int src_y, int dest_x, int dest_y,
           unsigned int w, unsigned int h)
{
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
  w = std::min((int)w, std::min(globalOutBuffer.cols - dest_x, img.cols - src_x));

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
  h = std::min((int)h, std::min(globalOutBuffer.rows - dest_y, img.rows - src_y));

  img(cv::Rect(src_x, src_y, w, h)).copyTo(globalOutBuffer(cv::Rect(dest_x, dest_y, w, h)));

  return 0;
}


static void
update_smpte_colorbars(analogtv_input *input)
{
  struct state *st = (struct state *) input->client_data;
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

  for (int col = 0; col < 7; col++)
  {
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

  analogtv_draw_solid_rel_lcp(input,       0.0,   1.0/6.0, 0.75, 1.00,   7, 40, 303);   /* -I       */
  analogtv_draw_solid_rel_lcp(input,   1.0/6.0,   2.0/6.0, 0.75, 1.00, 100,  0,   0);   /* white    */
  analogtv_draw_solid_rel_lcp(input,   2.0/6.0,   3.0/6.0, 0.75, 1.00,   7, 40,  33);   /* +Q       */
  analogtv_draw_solid_rel_lcp(input,   3.0/6.0,   4.0/6.0, 0.75, 1.00,   7,  0,   0);   /* black    */
  analogtv_draw_solid_rel_lcp(input, 12.0/18.0, 13.0/18.0, 0.75, 1.00,   3,  0,   0);   /* black -4 */
  analogtv_draw_solid_rel_lcp(input, 13.0/18.0, 14.0/18.0, 0.75, 1.00,   7,  0,   0);   /* black    */
  analogtv_draw_solid_rel_lcp(input, 14.0/18.0, 15.0/18.0, 0.75, 1.00,  11,  0,   0);   /* black +4 */
  analogtv_draw_solid_rel_lcp(input,   5.0/6.0,   6.0/6.0, 0.75, 1.00,   7,  0,   0);   /* black    */

  if (!st->logoImg.empty())
  {
    double aspect = (double)st->outBuffer.cols / st->outBuffer.rows;
    double scale = (aspect > 1 ? 0.35 : 0.6);
    int w2 = st->tv->outbuffer_width  * scale;
    int h2 = st->tv->outbuffer_height * scale * aspect;
    analogtv_load_ximage (st->tv, input, st->logoImg, st->logoMask,
                          (st->tv->outbuffer_width - w2) / 2,
                          st->tv->outbuffer_height * 0.20,
                          w2, h2);
  }

  input->next_update_time += 1.0;
}


static void run(Params params)
{
  int nFiles = params.sources.size();

  /* stations should be a multiple of files, but >= 6.
     channels should be double that. */
  int MAX_STATIONS = 0;
  while (MAX_STATIONS < 6)
  {
    MAX_STATIONS += nFiles;
  }
  MAX_STATIONS *= 2;

  int N_CHANNELS = MAX_STATIONS * 2;

  ya_rand_init (params.seed);
  cv::Size outSize = params.size;
  int duration = params.duration;

  unsigned long start_time = time((time_t *)0);

  unsigned long curticks = 0, curticks_sub = 0;
  time_t lastlog = time((time_t *)0);
  int frames_left = 0;
  int channel_changes = 0;
  int fps = 30;
  std::vector<cv::Mat> images;
  cv::Mat baseImage;
  std::vector<int> stats(N_CHANNELS);

  /* Load all of the input images.
   */

  int maxw = 0, maxh = 0;
  for (int i = 0; i < nFiles; i++)
  {
    //TODO: sources
    std::string fname = params.sources[i];
    cv::Mat img = loadImage(fname);
    images.push_back(img);
    maxw = std::max(maxw, img.cols);
    maxh = std::max(maxh, img.rows);
  }

  if (outSize.empty())
  {
    outSize = cv::Size(maxw, maxh);
  }

  /* can't be odd */
  outSize.width  &= ~1;
  outSize.height &= ~1;

  /* Scale all of the input images to the size of the largest one, or frame.
   */
  for (int i = 0; i < nFiles; i++)
  {
    cv::Mat img = images[i];
    if (img.size() != outSize)
    {
      double r1 = (double) outSize.width / outSize.height;
      double r2 = (double) img.cols / img.rows;
      int w2, h2;
      if (r1 > r2)
        {
          w2 = outSize.height * r2;
          h2 = outSize.height;
        }
      else
        {
          w2 = outSize.width;
          h2 = outSize.width / r2;
        }
      cv::Mat out;
      cv::resize(img, out, cv::Size(w2, h2));
      images[i] = out;
    }
  }

  state runState;
  state* st = &runState;

  st->outBuffer = cv::Mat4b(outSize);
  globalOutBuffer = st->outBuffer;

  if (!params.logoFname.empty())
  {
    st->logoImg = loadImage(params.logoFname);
    /* Pull the alpha out of the logo and make a separate mask ximage. */
    st->logoMask = cv::Mat(st->logoImg.size(), CV_8UC4, cv::Scalar(0));
    std::vector<cv::Mat> logoCh;
    cv::split(st->logoImg, logoCh);
    cv::Mat z = cv::Mat(st->logoImg.size(), CV_8UC1, cv::Scalar(0));
    cv::merge(std::vector<cv::Mat> {logoCh[0], logoCh[1], logoCh[2], z}, st->logoImg);
    cv::merge(std::vector<cv::Mat> {z, z, z, logoCh[3]}, st->logoMask);
  }

  st->tv = analogtv_allocate(outSize.width, outSize.height);

  for (int i = 0; i < MAX_STATIONS; i++)
  {
    analogtv_input *input = analogtv_input_allocate();
    input->client_data = st;
    st->stations.push_back(input);
  }

  analogtv_set_defaults(st->tv);

  bool fixSettings = params.fixSettings;
  if (!fixSettings)
  {
    if (ya_random()%4==0)
    {
      st->tv->tint_control += pow(ya_frand(2.0)-1.0, 7) * 180.0;
    }
    if (1)
    {
      st->tv->color_control += ya_frand(0.3) * ((ya_random() & 1) ? 1 : -1);
    }
    if (0) //if (darkp)
    {
      if (ya_random()%4==0) {
        st->tv->brightness_control += ya_frand(0.15);
      }
      if (ya_random()%4==0) {
        st->tv->contrast_control += ya_frand(0.2) * ((ya_random() & 1) ? 1 : -1);
      }
    }
  }

  st->chanSettings.resize(N_CHANNELS);
  for (int i = 0; i < N_CHANNELS; i++)
  {
    st->chanSettings[i].noise_level = 0.06;

    int last_station = 42;
    for (int stati = 0; stati < MAX_MULTICHAN; stati++)
    {
        analogtv_reception *rec = &st->chanSettings[i].recs[stati];
        int station;
        while (1)
        {
          station = ya_random() % (st->stations.size());
          if (station != last_station) break;
          if ((ya_random()%10)==0) break;
        }
        last_station=station;
        rec->input = st->stations[station];
        if (fixSettings)
        {
          rec->level = 0.3;
          rec->ofs=0;
          rec->multipath=0.0;
          rec->freqerr = 0;
        }
        else
        {
          rec->level = pow(ya_frand(1.0), 3.0) * 2.0 + 0.05;
          rec->ofs = ya_random()%ANALOGTV_SIGNAL_LEN;
          if (ya_random()%3)
          {
            rec->multipath = ya_frand(1.0);
          }
          else
          {
            rec->multipath=0.0;
          }
          if (stati)
          {
            /* We only set a frequency error for ghosting stations,
              because it doesn't matter otherwise */
            rec->freqerr = (ya_frand(2.0)-1.0) * 3.0;
          }
        }

        if (rec->level > 0.3) break;
        if (ya_random()%4) break;
    }
  }

  st->curinputi=0;
  st->cs = &st->chanSettings[st->curinputi];

  for (const auto& s : params.outputs)
  {
    st->outputs.emplace_back(Output::create(s, outSize));
  }

  curticks_sub = 0;
  channel_changes = 0;
  st->curinputi = 0;
  st->tv->powerup = 0.0;

  /* Fill all channels with images */
  Log::write(2, "initializing " + std::to_string(nFiles) + " files in " +
                std::to_string(MAX_STATIONS) + " channels");

  for (int i = 0; i < MAX_STATIONS; i++)
  {
    const cv::Mat& img = images[i % nFiles];
    analogtv_input *input = st->stations[i];
    int w = img.cols * 0.815; /* underscan */
    int h = img.rows * 0.970;
    int x = (outSize.width  - w) / 2;
    int y = (outSize.height - h) / 2;

    if (!(ya_random() % 8)) /* Some stations are colorbars */
    {
      input->updater = update_smpte_colorbars;
    }

    analogtv_setup_sync(input, 1, (ya_random() % 20) == 0);
    analogtv_load_ximage(st->tv, input, img, cv::Mat4b(), x, y, w, h);
  }

  /* This is xanalogtv_draw()
   */
  while (1)
  {
    const analogtv_reception *recs[MAX_MULTICHAN];
    unsigned rec_count = 0;
    double curtime = curticks * 0.001;

    frames_left--;
    if (frames_left <= 0 &&
        (!params.powerup || curticks > POWERUP_DURATION*1000))
    {
      channel_changes++;

      /* 1 - 7 sec */
      frames_left = fps * (1 + ya_frand(6));

      /* Otherwise random */
      st->curinputi = 1 + (ya_random() % (N_CHANNELS - 1));

      stats[st->curinputi]++;
      st->cs = &st->chanSettings[st->curinputi];
      /* Set channel change noise flag */
      st->tv->channel_change_cycles=200000;

      Log::write(2, std::to_string(curticks/1000.0) + " sec: channel " + std::to_string(st->curinputi));

      /* Turn the knobs every now and then */
      if (!fixSettings && !(ya_random() % 5))
      {
        if (ya_random()%4==0) {
          st->tv->tint_control += pow(ya_frand(2.0)-1.0, 7) * 180.0 * ((ya_random() & 1) ? 1 : -1);
        }
        if (1)
        {
          st->tv->color_control += ya_frand(0.3) * ((ya_random() & 1) ? 1 : -1);
        }
        if (0) //(darkp)
        {
          if (ya_random()%4==0) {
            st->tv->brightness_control += ya_frand(0.15);
          }
          if (ya_random()%4==0) {
            st->tv->contrast_control += ya_frand(0.2) * ((ya_random() & 1) ? 1 : -1);
          }
        }
      }
    }

    for (int i = 0; i < MAX_MULTICHAN; i++)
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

    st->tv->powerup = (params.powerup ? curtime : 9999);

    for (int i = 0; i < MAX_MULTICHAN; i++)
    {
      /* Noisy image */
      analogtv_reception *rec = &st->cs->recs[i];
      if (rec->input)
      {
        analogtv_reception_update(rec);
        recs[rec_count] = rec;
        ++rec_count;
      }

      analogtv_draw (st->tv, st->cs->noise_level, recs, rec_count);
    }

    // Send rendered frame to outputs
    for (const auto& o : st->outputs)
    {
      o->send(st->outBuffer);
    }

    if (params.powerup &&
        curticks > (unsigned int)((duration*1000) - (POWERDOWN_DURATION*1000)))
    {
      /* Fade out, as there is no power-down animation. */
      double r = ((duration*1000 - curticks) /
                  (double) (POWERDOWN_DURATION*1000));
      static double ob = 9999;
      double min = -1.5;  /* Usable range is something like -0.75 to 1.0 */
      if (ob == 9999)
        ob = st->tv->brightness_control;  /* Terrible */
      st->tv->brightness_control = min + (ob - min) * r;
    }

    if (curtime >= duration)
      break;

    curticks     += 1000/fps;
    curticks_sub += 1000/fps;

    //TODO: refactor it
    unsigned long now = time((time_t *)0);
    if (now > (unsigned int)(Log::getVerbosity() == 1 ? lastlog : lastlog + 10))
    {
      unsigned long elapsed = now - start_time;
        double ratio = curtime / (double) duration;
        int remaining = (ratio ? (elapsed / ratio) - elapsed : 0);
        int percent = 100 * ratio;
        int cols = 47;
        std::string dots(cols * ratio, '.');
        fprintf (stderr, "%sprocessing%s %2d%%, %d:%02d:%02d ETA%s",
                 (Log::getVerbosity() == 1 ? "\r" : ""),
                 dots.c_str(), percent, 
                 (remaining/60/60),
                 (remaining/60)%60,
                 remaining%60,
                 (Log::getVerbosity() == 1 ? "" : "\n"));
        lastlog = now;
    }
  }

  if (Log::getVerbosity() == 1) fprintf(stderr, "\n");


  if (channel_changes == 0) channel_changes++;

  Log::write(2, "channels shown: " + std::to_string(channel_changes));
  for (int i = 0; i < N_CHANNELS; i++)
  {
    Log::write(2, "  " + std::to_string(i+1) + ":  " + std::to_string(stats[i] * 100 / channel_changes));
  }
}


static const std::map<std::string, CmdArgument> knownArgs =
{
    // name, exampleArgs, type, optional, help
    {"verbose",
      { "n",     CmdArgument::Type::INT,  true,
        "level of verbosity from 0 to 5" }},
    {"duration",
      { "secs",  CmdArgument::Type::INT,  false,
        "length of video in secs, e.g. 60" }},
    {"powerup",
      { "",      CmdArgument::Type::BOOL, true,
        "to run power up sequence or not" }},
    {"fixsettings",
      { "",      CmdArgument::Type::BOOL, true,
        "apply less randomness to settings" }},
    {"size",
      { "width height", CmdArgument::Type::LIST_INT, true,
        "use different size than maximum of given images" }},
    {"logo",
      { "file",  CmdArgument::Type::STRING, true,
        "logo image to display over color bars" }},
    {"seed",
      { "value", CmdArgument::Type::INT, true,
        "random seed to start random generator or 0 to randomize by current date and time" }},
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

static const std::string message =
"Shows images or videos like they are on an old TV screen\n"
"Based on analogtv hack written by Trevor Blackwell (https://tlb.org/)\n"
"from XScreensaver (https://www.jwz.org/xscreensaver/) by Jamie Zawinski (https://jwz.org/) and the team";


std::optional<Params> parseParams(int args, char** argv)
{
  std::map<std::string, ArgType> usedArgs = parseCmdArgs(knownArgs, args, argv);
  if (usedArgs.empty())
  {
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

  p.powerup = false;
  if (usedArgs.count("powerup"))
  {
    p.powerup = std::get<bool>(usedArgs.at("powerup"));
  }

  p.fixSettings = false;
  if (usedArgs.count("fixsettings"))
  {
    p.fixSettings = std::get<bool>(usedArgs.at("fixsettings"));
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
  std::string progName(s ? s+1 : argv[0]);

  std::optional<Params> oparams = parseParams(argc, argv);
  if (!oparams)
  {
    showUsage(message, progName, knownArgs);
    return -1;
  }

  Log::setProgName(progName);
  Log::setVerbosity(oparams.value().verbosity);

  run(oparams.value());

  return 0;
}
