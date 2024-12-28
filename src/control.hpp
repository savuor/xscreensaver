#pragma once

#include "precomp.hpp"

#include "analogtv.hpp"
#include "source.hpp"

namespace atv
{

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

  static std::shared_ptr<Control> create(const std::string& desc);

  virtual void setRNG(uint64_t rngSeed) = 0;

  // why const ref to sources does not work?
  virtual void createChannels(const std::vector<std::shared_ptr<atv::Source>> sources) = 0;

  virtual void setTvControls(atv::AnalogTV& tv) = 0;

  virtual void rotateKnobsStart() = 0;

  virtual void rotateKnobsSwitch() = 0;

  virtual void run() = 0;

  virtual Operation getNext() = 0;

  std::vector<ChanSetting> chanSettings;
};


} // ::atv