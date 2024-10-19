#pragma once

#include "precomp.hpp"

#include "analogtv.hpp"

namespace atv
{

struct Source
{
  Source() :
    outSize()
  { }

  /**
   * @brief Currently supported are: ":bars:<logoFile>", ":cam:<cameraNum>" and image files
   * 
   * @param s Filename or source name
   * @return Source object
   */
  static std::shared_ptr<Source> create(const std::string& s);

  virtual void update(AnalogInput& input) = 0;

  virtual cv::Size getImageSize() = 0;

  virtual void setOutSize(cv::Size size) = 0;

  // used for images only
  virtual void setSsavi(bool _do_ssavi) = 0;

  virtual ~Source() {}

  cv::Size outSize;
};

} // ::atv