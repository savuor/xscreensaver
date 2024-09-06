#pragma once

#include "precomp.hpp"

#include <opencv2/core.hpp>

struct Output
{
  Output() { }

 /**
 * @brief Currently supported are: ":highgui" and video files
 * 
 * @param s Filename or source name
 * @param imgSize Image size to write
 * @return Output object
 */
  static std::shared_ptr<Output> create(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) = 0;

  virtual ~Output() { }
};

//TODO
// struct Logger
// {
//     static write(const std::string& s);
//     static setVerbosity(int n);
//     static setProgName(const std::string& s);
// };
cv::Mat loadImage(const std::string& fname);

