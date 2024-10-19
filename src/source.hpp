#pragma once

#include "precomp.hpp"

#include "analogtv.hpp"


struct Source
{
  Source() { }

  /**
   * @brief Currently supported are: ":bars" and image files
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
};


struct BarsSource : Source
{
  static const cv::Size defaultSize; // 320x240

  BarsSource() :
    outSize(defaultSize) // default size
  { }

  BarsSource(cv::Size _outSize) :
    BarsSource()
  {
    outSize = _outSize;
  }

  BarsSource(const cv::Mat& _logoImg) :
    BarsSource(_logoImg, defaultSize)
  { }

  BarsSource(const cv::Mat& _logoImg, cv::Size _outSize);

  void update(AnalogInput& input) override;

  cv::Size getImageSize() override
  {
    return defaultSize;
  }

  void setOutSize(cv::Size _outSize) override
  {
    outSize = _outSize;
  }

  // used for images only
  void setSsavi(bool _do_ssavi) override
  { }

  cv::Mat logoImg, logoMask;
  cv::Size outSize;
};


struct ImageSource : Source
{
  ImageSource() :
    outSize(),
    img(),
    resizedImg(),
    do_ssavi()
  { }

  ImageSource(const cv::Mat& _img) :
    ImageSource(_img, _img.size(), false)
  { }

  ImageSource(const cv::Mat& _img, cv::Size _outSize, bool _do_ssavi) :
    outSize(_outSize),
    img(_img),
    resizedImg(_img),
    do_ssavi(_do_ssavi)
  { }

  cv::Size getImageSize() override
  {
    return img.size();
  }

  void setOutSize(cv::Size _outSize) override;

  void setSsavi(bool _do_ssavi) override
  {
    do_ssavi = _do_ssavi;
  }

  void update(AnalogInput& input) override;

  cv::Size outSize;
  cv::Mat img;
  cv::Mat resizedImg;
  bool do_ssavi;
};
