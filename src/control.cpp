#include "precomp.hpp"

#include "control.hpp"

namespace atv
{

const int MAX_MULTICHAN = 2;

const double POWERUP_DURATION = 6.0;  /* Hardcoded in analogtv.c */
const double POWERDOWN_DURATION = 1.0;  /* Only used here */

struct RandomControl : public Control
{
  //TODO: delegate construction
  RandomControl() {}

  RandomControl(bool _fixSettings, double _fps, double _duration, bool _powerUpDown)
  {
    this->fixSettings = _fixSettings;

    this->fps = _fps;
    this->duration = _duration;
    this->usePowerUpDown = _powerUpDown;
  }

  void setRNG(uint64_t rngSeed) override
  {
    this->rng = cv::RNG(rngSeed);
  }

  // why const ref to sources does not work?
  void createChannels(const std::vector<std::shared_ptr<atv::Source>> sources) override
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

  void setTvControls(atv::AnalogTV& tv) override
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

  void rotateKnobsStart() override
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

  void rotateKnobsSwitch() override
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

  void run() override
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

  Operation getNext() override
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

std::shared_ptr<Control> Control::create(const std::string &desc)
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
        const std::string &st = tokens[ti];
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

      control = std::make_shared<RandomControl>(fixSettings, fps, duration, powerUpDown);
    }
    else
    {
      throw std::runtime_error("Unknown source type: " + stype);
    }
  }
  else
  {
    // TODO: load json with settings
    throw std::runtime_error("JSON loading is not implemented yet");
  }

  return control;
}

} // ::atv