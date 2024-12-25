/* xanalogtv-cli, Copyright © 2018-2023 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#include "precomp.hpp"

#include "analogtv.hpp"
#include "utils.hpp"
#include "source.hpp"
#include "output.hpp"

#include <chrono>

#include <opencv2/core.hpp>

#include "wx/wx.h"

struct Params
{
  int         verbosity;
  int         duration;
  int         seed;
  bool        powerup;
  bool        fixSettings;
  cv::Size    size;

  std::vector<std::string> sources;
  std::vector<std::string> outputs;
};

const int MAX_MULTICHAN = 2;

const double POWERUP_DURATION = 6.0;  /* Hardcoded in analogtv.c */
const double POWERDOWN_DURATION = 1.0;  /* Only used here */

struct ChanSetting
{
  ChanSetting() :
    receptions(),
    sources(),
    noise_level(0)
  { }

  //TODO: join them into one vector
  std::vector<atv::AnalogReception> receptions;
  std::vector<std::shared_ptr<atv::Source>> sources;
  double noise_level;
};


cv::Size getBestSize(const std::vector<std::shared_ptr<atv::Source>>& sources, cv::Size size)
{
  // get best size
  cv::Size outSize;
  int maxw = 0, maxh = 0;
  for (const auto& s : sources)
  {
    cv::Size sz = s->getImageSize();
    maxw = std::max(maxw, sz.width);
    maxh = std::max(maxh, sz.height);
  }
  outSize = (size.empty()) ? cv::Size(maxw, maxh) : size;
  /* can't be odd */
  outSize.width  &= ~1;
  outSize.height &= ~1;

  return outSize;
}


void rotateKnobsStart(bool fixSettings, atv::AnalogTV& tv, cv::RNG& rng)
{
  // from analogtv_set_defaults()

  // values taken from analogtv-cli

  // tint: 0 to 360, default 5
  tv.tint_control  = 5;
  // color: 0 to 400, default 70
  // or 0 to +/- 500, need to check it
  tv.color_control = 70 / 100.0;

  // brightness: -75 to 100, default 1.5 or 3.0
  tv.brightness_control = 2 / 100.0;
  // contrast: 0 to 500, default 150
  tv.contrast_control   = 150 / 100.0;
  tv.height_control = 1.0;
  tv.width_control  = 1.0;
  tv.squish_control = 0.0;

  tv.powerup =1000.0;

  //tv.hashnoise_rpm = 0;
  tv.hashnoise_on = 0;
  tv.hashnoise_enable = 1;

  tv.horiz_desync  = rng.uniform(-5.0, 5.0);
  tv.squeezebottom = rng.uniform(-1.0, 4.0);

  if (!fixSettings)
  {
    if (rng() % 4 == 0)
    {
      tv.tint_control += pow(rng.uniform(-1.0, 1.0), 7) * 180.0;
    }
    if (1)
    {
      tv.color_control += rng.uniform(0.0, 0.3) * ((rng() & 1) ? 1 : -1);
    }
    if (0) //if (darkp)
    {
      if (rng() % 4 == 0)
      {
        tv.brightness_control += rng.uniform(0.0, 0.15);
      }
      if (rng() % 4 == 0)
      {
        tv.contrast_control += rng.uniform(0.0, 0.2) * ((rng() & 1) ? 1 : -1);
      }
    }
  }
}


void rotateKnobsSwitch(bool fixSettings, atv::AnalogTV& tv, cv::RNG& rng)
{
  if (!fixSettings && !(rng() % 5))
  {
    if (rng() % 4 == 0) 
    {
      tv.tint_control += pow(rng.uniform(-1.0, 1.0), 7) * 180.0 * ((rng() & 1) ? 1 : -1);
    }
    if (1)
    {
      tv.color_control += rng.uniform(0.0, 0.3) * ((rng() & 1) ? 1 : -1);
    }
    if (0) //(darkp)
    {
      if (rng() % 4 == 0)
      {
        tv.brightness_control += rng.uniform(0.0, 0.15);
      }
      if (rng() % 4 == 0)
      {
        tv.contrast_control += rng.uniform(0.0, 0.2) * ((rng() & 1) ? 1 : -1);
      }
    }
  }
}

// why const ref to sources does not work?
std::vector<ChanSetting> createChannels(bool fixSettings, const std::vector<std::shared_ptr<atv::Source>> sources,
                                        cv::RNG& rng)
{
  size_t nChannels = std::max(sources.size() * 2, 6UL);

  std::vector<ChanSetting> chanSettings;
  for (size_t i = 0; i < nChannels; i++)
  {
    ChanSetting channel;
    // noise: 0 to 0.2 or 0 to 5.0, default 0.04
    channel.noise_level = 0.06;

    int last_station = 42;
    for (int stati = 0; stati < MAX_MULTICHAN; stati++)
    {
        int stationId;
        while (1)
        {
          stationId = rng() % (sources.size());
          // don't do ghost reception with the same station...
          if (stationId != last_station) break;
          // ...at least too often
          if (rng() % 10 == 0) break;
        }
        last_station = stationId;
        std::shared_ptr<atv::Source> source = sources[stationId];

        atv::AnalogReception rec;
        if (fixSettings)
        {
          rec.level = 0.3;
          rec.ofs = 0;
          rec.multipath = 0.0;
          rec.freqerr = 0;
        }
        else
        {
          rec.level = pow(rng.uniform(0.0, 1.0), 3.0) * 2.0 + 0.05;
          rec.ofs   = rng() % atv::ANALOGTV_SIGNAL_LEN;
          if (rng() % 3)
          {
            rec.multipath = rng.uniform(0.0, 1.0);
          }
          else
          {
            rec.multipath = 0.0;
          }
          if (stati > 0)
          {
            /* We only set a frequency error for ghosting stations,
              because it doesn't matter otherwise */
            rec.freqerr = rng.uniform(-1.0, 1.0) * 3.0;
          }
        }

        channel.receptions.push_back(rec);
        channel.sources.push_back(source);

        if (rec.level > 0.3) break;
        if (rng() % 4) break;
    }

    chanSettings.push_back(channel);
  }

  return chanSettings;
}


static void run(Params params)
{
  int duration = params.duration;

  int seed = params.seed;
  if (params.seed == 0)
  {
    auto tp = std::chrono::high_resolution_clock::now().time_since_epoch();
    seed = tp.count();
  }
  cv::RNG rng(seed);

  int fps = 30;

  std::vector<std::shared_ptr<atv::Source>> sources;
  for (const auto& s : params.sources)
  {
    sources.push_back(atv::Source::create(s));
  }

  atv::Log::write(2, "initialized " + std::to_string(sources.size()) + " sources");

  cv::Size outSize = getBestSize(sources, params.size);

  for (const auto& s : sources)
  {
    s->setOutSize(outSize);
    // randomly set ssavi (what's this? BW?) for image sources
    s->setSsavi(rng() % 20 == 0);
  }

  std::vector<std::shared_ptr<atv::Output>> outputs;
  for (const auto& s : params.outputs)
  {
    outputs.emplace_back(atv::Output::create(s, outSize));
  }

  atv::Log::write(2, "initialized " + std::to_string(outputs.size()) + " outputs");

  cv::Mat4b outBuffer = cv::Mat4b(outSize);
  atv::AnalogTV tv(seed);
  tv.set_buffer(outBuffer);

  rotateKnobsStart(params.fixSettings, tv, rng);

  std::vector<ChanSetting> chanSettings = createChannels(params.fixSettings, sources, rng);

  size_t nChannels = chanSettings.size();

  unsigned long start_time = time((time_t *)0);

  unsigned long curticks = 0;
  time_t lastlog = time((time_t *)0);
  int frames_left = 0;
  int channel_changes = 0;
  int curinputi = 0;

  tv.powerup = 0.0;

  std::vector<int> stats(nChannels);

  /* This is xanalogtv_draw()
   */
  while (1)
  {
    double curtime = curticks * 0.001;

    frames_left--;
    if (frames_left <= 0 &&
        (!params.powerup || curticks > POWERUP_DURATION*1000))
    {
      channel_changes++;

      /* 1 - 7 sec */
      frames_left = fps * (1 + rng.uniform(0.0, 6.0));

      /* Otherwise random */
      curinputi = 1 + (rng() % (nChannels - 1));

      stats[curinputi]++;
      /* Set channel change noise flag */
      tv.channel_change_cycles = 200000;

      atv::Log::write(2, std::to_string(curticks/1000.0) + " sec: channel " + std::to_string(curinputi));

      /* Turn the knobs every now and then */
      rotateKnobsSwitch(params.fixSettings, tv, rng);
    }

    tv.powerup = params.powerup ? curtime : 9999;

    ChanSetting& curChannel = chanSettings[curinputi];
    for (size_t i = 0; i < curChannel.receptions.size(); i++)
    {
      atv::AnalogReception& rec = curChannel.receptions[i];
      curChannel.sources[i]->update(rec.input);
      /* Noisy image */
      rec.update(rng);
    }

    tv.draw(curChannel.noise_level, curChannel.receptions);

    // Send rendered frame to outputs
    for (const auto& o : outputs)
    {
      o->send(outBuffer);
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
        ob = tv.brightness_control;  /* Terrible */
      tv.brightness_control = min + (ob - min) * r;
    }

    if (curtime >= duration)
      break;

    curticks     += 1000/fps;

    //TODO: refactor it
    unsigned long now = time((time_t *)0);
    if (now > (unsigned int)(atv::Log::getVerbosity() == 1 ? lastlog : lastlog + 10))
    {
      unsigned long elapsed = now - start_time;
        double ratio = curtime / (double) duration;
        int remaining = (ratio ? (elapsed / ratio) - elapsed : 0);
        int percent = 100 * ratio;
        int cols = 47;
        std::string dots(cols * ratio, '.');
        fprintf (stderr, "%sprocessing%s %2d%%, %d:%02d:%02d ETA%s",
                 (atv::Log::getVerbosity() == 1 ? "\r" : ""),
                 dots.c_str(), percent, 
                 (remaining/60/60),
                 (remaining/60)%60,
                 remaining%60,
                 (atv::Log::getVerbosity() == 1 ? "" : "\n"));
        lastlog = now;
    }
  }

  if (atv::Log::getVerbosity() == 1) fprintf(stderr, "\n");

  if (channel_changes == 0) channel_changes++;

  atv::Log::write(2, "channels shown: " + std::to_string(channel_changes));
  for (size_t i = 0; i < nChannels; i++)
  {
    atv::Log::write(2, "  " + std::to_string(i+1) + ":  " + std::to_string(stats[i] * 100 / channel_changes));
  }
}


static const std::map<std::string, atv::CmdArgument> knownArgs =
{
    // name, exampleArgs, type, optional, help
    {"verbose",
      { "n",     atv::CmdArgument::Type::INT,  true,
        "level of verbosity from 0 to 5" }},
    {"duration",
      { "secs",  atv::CmdArgument::Type::INT,  false,
        "length of video in secs, e.g. 60" }},
    {"powerup",
      { "",      atv::CmdArgument::Type::BOOL, true,
        "to run or not the power-on animation at the beginning, and fade to black at the end" }},
    {"fixsettings",
      { "",      atv::CmdArgument::Type::BOOL, true,
        "apply less randomness to settings" }},
    {"size",
      { "width height", atv::CmdArgument::Type::LIST_INT, true,
        "use different size than maximum of given images" }},
    {"seed",
      { "value", atv::CmdArgument::Type::INT, true,
        "random seed to start random generator or 0 to randomize by current date and time" }},
    {"in",
      { "src1 [src2 ... srcN]", atv::CmdArgument::Type::LIST_STRING, false,
        "signal sources: still images, video files or special sources:\n"
        "  * :cam:0 to :cam:9 are camera sources\n"
        "  * :bars are SMPTE color bars (if it's the only image and no size is given then the output size will be 320x240)\n"
        "  * :bars:/path/to/image is the as above with an overlaid station logo\n"
        "Note: video files are detected by extension. Supported extensions are listed in source.cpp file\n"
        "as knownVideoExtensions variable." }},
    {"out",
      { "out1 [out2 ... outN]", atv::CmdArgument::Type::LIST_STRING, false,
        "where to output video: video files or window, output to all sources happens simultaneously\n"
        "  * :highgui means output to window using OpenCV HighGUI module, stable FPS is not guaranteed" }}
};

static const std::string message =
"Shows images or videos like they are on an old TV screen\n"
"Based on analogtv hack written by Trevor Blackwell (https://tlb.org/)\n"
"from XScreensaver (https://www.jwz.org/xscreensaver/) by Jamie Zawinski (https://jwz.org/) and the team";


std::optional<Params> parseParams(int args, char** argv)
{
  std::map<std::string, atv::ArgType> usedArgs = atv::parseCmdArgs(knownArgs, args, argv);
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

  atv::Log::setProgName(progName);
  atv::Log::setVerbosity(oparams.value().verbosity);

  // Check that wxWidgets builds and works
  //wxPuts(wxT("TODO: implement a real GUI instead"));


  run(oparams.value());

  return 0;
}
