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

//#include "wx/wx.h"

struct Params
{
  int         verbosity;
  int         seed;
  cv::Size    size;

  std::string controlDescription;
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









//TODO: this
struct Control
{
  struct Operation
  {
    enum class Type
    {
      QUIT, SWITCH, KNOBS, NONE
    };

    Type type;
    int channel;
  };

  //TODO: delegate construction
  Control() {}

  Control(bool _fixSettings, double _fps, double _duration, bool _powerUpDown)
  {
    this->fixSettings = _fixSettings;

    this->fps = _fps;
    this->duration = _duration;
    this->usePowerUpDown = _powerUpDown;
  }

  static std::shared_ptr<Control> create(const std::string& desc)
  {
    std::shared_ptr<Control> control;
    std::vector<std::string> tokens = atv::split(desc, ':');

    if (tokens[0].empty())
    {
      // string starts from ":"
      if (tokens.size() < 2)
      {
        throw std::runtime_error("Control type not given");
      }
      std::string stype = tokens[1];
      // should be like ":random" or ":random:p=1:q=2:b"
      if (stype == "random")
      {
        std::map<std::string, std::string> kv;
        for (size_t ti = 2; ti < tokens.size(); ti++)
        {
          const std::string& st = tokens[ti];
          if (!st.empty())
          {
            std::vector<std::string> ttk = atv::split(st, '=');
            if (ttk.size() > 2)
            {
              throw std::runtime_error("Parameters should be of a form param=value");
            }
            std::string arg = ttk.size() == 2 ? ttk[1] : std::string();
            kv[ttk[0]] = arg;
          }
        }

        double duration = 60;
        if (kv.count("duration"))
        {
          duration = atv::parseInt(kv.at("duration")).value_or(60);
        }
        bool powerUpDown = kv.count("powerup");
        bool fixSettings = kv.count("fixsettings");
        int fps = 30;
        if (kv.count("fps"))
        {
          fps = atv::parseInt(kv.at("fps")).value_or(30);
        }

        control = std::make_shared<Control>(fixSettings, fps, duration, powerUpDown);
      }
      else
      {
          throw std::runtime_error("Unknown source type: " + stype);
      }
    }
    else
    {
      //TODO: load json with settings
      throw std::runtime_error("JSON loading is not implemented yet");
    }

    return control;
  }

  void setRNG(uint64_t rngSeed)
  {
    this->rng = cv::RNG(rngSeed);
  }

  // why const ref to sources does not work?
  void createChannels(const std::vector<std::shared_ptr<atv::Source>> sources)
  {
    size_t nChannels = std::max(sources.size() * 2, 6UL);

    this->chanSettings = { };
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
            stationId = this->rng() % (sources.size());
            // don't do ghost reception with the same station...
            if (stationId != last_station) break;
            // ...at least too often
            if (this->rng() % 10 == 0) break;
          }
          last_station = stationId;
          std::shared_ptr<atv::Source> source = sources[stationId];

          atv::AnalogReception rec;
          if (this->fixSettings)
          {
            rec.level = 0.3;
            rec.ofs = 0;
            rec.multipath = 0.0;
            rec.freqerr = 0;
          }
          else
          {
            rec.level = pow(this->rng.uniform(0.0, 1.0), 3.0) * 2.0 + 0.05;
            rec.ofs   = this->rng() % atv::ANALOGTV_SIGNAL_LEN;
            if (this->rng() % 3)
            {
              rec.multipath = this->rng.uniform(0.0, 1.0);
            }
            else
            {
              rec.multipath = 0.0;
            }
            if (stati > 0)
            {
              /* We only set a frequency error for ghosting stations,
                because it doesn't matter otherwise */
              rec.freqerr = this->rng.uniform(-1.0, 1.0) * 3.0;
            }
          }

          channel.receptions.push_back(rec);
          channel.sources.push_back(source);

          if (rec.level > 0.3) break;
          if (this->rng() % 4) break;
      }

      this->chanSettings.push_back(channel);
    }
  }

  void setTvControls(atv::AnalogTV& tv)
  {
    tv.tint_control  = this->tint;
    tv.color_control = this->color;

    tv.brightness_control = this->brightness;
    tv.contrast_control   = this->contrast;
    tv.height_control = this->height;
    tv.width_control  = this->width;
    tv.squish_control = this->squish;

    tv.powerup = this->powerup;

    tv.hashnoise_on     = this->useHashNoise;
    tv.hashnoise_enable = this->enableHashNoise;

    tv.horiz_desync  = this->horizontalDesync;
    tv.squeezebottom = this->squeezeBottom;

    tv.flutter_horiz_desync = this->useFlutterHorizontalDesync;
  }

  void rotateKnobsStart()
  {
    // from analogtv_set_defaults()

    // values taken from analogtv-cli

    // tint: 0 to 360, default 5
    this->tint = 5;
    // color: 0 to 400, default 70
    // or 0 to +/- 500, need to check it
    this->color = 70 / 100.0;

    // brightness: -75 to 100, default 1.5 or 3.0
    this->brightness = 2 / 100.0;
    // contrast: 0 to 500, default 150
    this->contrast   = 150 / 100.0;
    this->height = 1.0;
    this->width  = 1.0;
    this->squish = 0.0;

    this->powerup = 1000.0;

    //tv.hashnoise_rpm = 0;
    //TODO: do we need both?
    this->useHashNoise = 0;
    this->enableHashNoise = 1;

    this->horizontalDesync = this->rng.uniform(-5.0, 5.0);
    this->squeezeBottom = this->rng.uniform(-1.0, 4.0);

    this->useFlutterHorizontalDesync = false;

    if (!this->fixSettings)
    {
      if (this->rng() % 4 == 0)
      {
        this->tint += pow(this->rng.uniform(-1.0, 1.0), 7) * 180.0;
      }
      if (1)
      {
        this->color += this->rng.uniform(0.0, 0.3) * ((this->rng() & 1) ? 1 : -1);
      }
      if (0) //if (darkp)
      {
        if (this->rng() % 4 == 0)
        {
          this->brightness += this->rng.uniform(0.0, 0.15);
        }
        if (this->rng() % 4 == 0)
        {
          this->contrast += this->rng.uniform(0.0, 0.2) * ((this->rng() & 1) ? 1 : -1);
        }
      }
    }
  }

  void rotateKnobsSwitch()
  {
    if (!this->fixSettings && !(this->rng() % 5))
    {
      if (this->rng() % 4 == 0) 
      {
        this->tint += pow(this->rng.uniform(-1.0, 1.0), 7) * 180.0 * ((this->rng() & 1) ? 1 : -1);
      }
      if (1)
      {
        this->color += this->rng.uniform(0.0, 0.3) * ((this->rng() & 1) ? 1 : -1);
      }
      if (0) //(darkp)
      {
        if (this->rng() % 4 == 0)
        {
          this->brightness += this->rng.uniform(0.0, 0.15);
        }
        if (this->rng() % 4 == 0)
        {
          this->contrast += this->rng.uniform(0.0, 0.2) * ((this->rng() & 1) ? 1 : -1);
        }
      }
    }
  }

  void run()
  {
    this->channel = this->rng() % this->chanSettings.size();
    // for fading out
    this->lastBrightness = -std::numeric_limits<double>::max();

    this->frameCounter = 0;
    this->lastFrame = this->fps * this->duration;
    this->powerUpLastFrame = POWERUP_DURATION * this->fps;
    this->fadeOutFirstFrame = (this->duration - POWERDOWN_DURATION) * this->fps;

    this->channelLastFrame = 0;
  }

  Operation getNext()
  {
    Operation op;
    op.channel = this->channel;
    op.type = Operation::Type::NONE;

    double curTime = this->frameCounter / this->fps;
    // power up -> switch channels -> power down

    bool canSwitchChannels = true;
    if (this->usePowerUpDown)
    {
      // don't switch channels when powering up / fading out
      if (this->frameCounter < this->powerUpLastFrame)
      {
        this->powerup = curTime;
        canSwitchChannels = false;
      }
      else if (this->frameCounter >= this->fadeOutFirstFrame)
      {
        /* Usable range is something like -0.75 to 1.0 */
        static const double minBrightness = -1.5;

        // initialize fading out
        if (this->lastBrightness <= -10.0) // some big value
        {
          this->lastBrightness = brightness;
        }

        /* Fade out, as there is no power-down animation. */
        double rate = (this->duration - curTime) / POWERDOWN_DURATION;
        brightness = minBrightness * (1.0 - rate) + this->lastBrightness * rate;

        canSwitchChannels = false;
      }
    }

    if (canSwitchChannels)
    {
      // channel switch is allowed
      if (this->frameCounter >= this->channelLastFrame)
      {
        /* 1 - 7 sec */
        this->channelLastFrame = frameCounter + this->fps * (1 + this->rng.uniform(0.0, 6.0));

        this->channel = this->rng() % this->chanSettings.size();

        atv::Log::write(2, std::to_string(curTime) + " sec: channel " + std::to_string(this->channel));

        /* Turn the knobs every now and then */
        this->rotateKnobsSwitch();

        op.type = Operation::Type::SWITCH;
      }
    }

    if (this->frameCounter >= this->lastFrame)
    {
      op.type = Operation::Type::QUIT;
    }

    this->frameCounter++;

    op.channel = this->channel;
    return op;
  }

  std::vector<ChanSetting> chanSettings;

  cv::RNG rng;

  bool fixSettings;

  double duration;
  double fps;
  bool usePowerUpDown;

  // state
  int frameCounter;
  int channel;
  int lastFrame;
  int channelLastFrame;
  int fadeOutFirstFrame;
  int powerUpLastFrame;

  // for fading out
  double lastBrightness;
  // tv knobs
  double powerup;
  double brightness;
  double tint;
  double color;
  double contrast;
  double height;
  double width;
  double squish;

  bool useHashNoise;
  bool enableHashNoise;

  double horizontalDesync;
  double squeezeBottom;

  bool useFlutterHorizontalDesync;
};


static void run(Params params)
{
  int seed = params.seed;
  if (params.seed == 0)
  {
    auto tp = std::chrono::high_resolution_clock::now().time_since_epoch();
    seed = tp.count();
  }
  cv::RNG rng(seed);

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
    //TODO: what's this?
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

  std::shared_ptr<Control> control = Control::create(params.controlDescription);
  control->setRNG(seed);

  control->createChannels(sources);

  control->rotateKnobsStart();
  control->setTvControls(tv);

  control->run();

  //TODO: remove it
  tv.powerup = 0.0;

  while (true)
  {
    auto action = control->getNext();

    if (action.type == Control::Operation::Type::QUIT)
    {
      break;
    }

    int curInput = action.channel;

    if (action.type == Control::Operation::Type::SWITCH)
    {
      tv.channel_change_cycles = 200000;
    }

    control->setTvControls(tv);

    ChanSetting& curChannel = control->chanSettings[curInput];
    for (size_t i = 0; i < curChannel.receptions.size(); i++)
    {
      atv::AnalogReception& rec = curChannel.receptions[i];
      //TODO: pass current time
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
  }

  atv::Log::write(2, "Finish");
}


static const std::map<std::string, atv::CmdArgument> knownArgs =
{
    // name, exampleArgs, type, optional, help
    {"verbose",
      { "n",     atv::CmdArgument::Type::INT,  true,
        "level of verbosity from 0 to 5" }},
    {"size",
      { "width height", atv::CmdArgument::Type::LIST_INT, true,
        "use different size than maximum of given images" }},
    {"seed",
      { "value", atv::CmdArgument::Type::INT, true,
        "random seed to start random generator or 0 to randomize by current date and time" }},
    {"control",
      { "<file.json or param string>", atv::CmdArgument::Type::STRING, false,
        "control scenario file in JSON format or a special control with its arguments separated by semicolon:\n"
        "  * JSON file containing prescripted instructions (not implemented yet)\n"
        "  * :random:par1=1:par2=0:boolPar3 is a random control with the following available parameters:\n"
        "    * duration: length of video in secs, 60 if not given\n"
        "    * powerup: if given, power-on animation is run at the beginning, and fade to black is done at the end\n"
        "    * fixsettings: if given, some TV settings are not random\n"
        "    * fps: frames per second, 30 if not given (not implemented properly yet)\n"
        "    Example control description: \":random:duration=60:fixsettings:powerup\"" }},
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
  p.controlDescription = std::get<std::string>(usedArgs.at("control"));

  p.verbosity = 0;
  if (usedArgs.count("verbose"))
  {
    p.verbosity = std::get<int>(usedArgs.at("verbose"));
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
