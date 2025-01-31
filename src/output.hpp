#pragma once

#include "precomp.hpp"

namespace atv
{

struct Output
{
  Output() { }

  //TODO: send desired FPS
 /**
 * @brief Currently supported are: ":highgui" and video files
 * 
 * @param s Filename or output name
 * @param imgSize Image size to write
 * @return Output object
 */
  static std::shared_ptr<Output> create(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) = 0;

  virtual ~Output() { }
};

} // ::atv
