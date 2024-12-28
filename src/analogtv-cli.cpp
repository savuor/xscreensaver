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
#include "control.hpp"

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

  std::shared_ptr<atv::Control> control = atv::Control::create(params.controlDescription);
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

    if (action.type == atv::Control::Operation::Type::QUIT)
    {
      break;
    }

    int curInput = action.channel;

    if (action.type == atv::Control::Operation::Type::SWITCH)
    {
      tv.channel_change_cycles = 200000;
    }

    control->setTvControls(tv);

    atv::ChanSetting& curChannel = control->chanSettings[curInput];
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
