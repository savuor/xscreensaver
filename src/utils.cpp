#include "precomp.hpp"
#include "utils.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

// I/O

cv::Mat loadImage(const std::string& fname)
{
  assert(!fname.empty());

  cv::Mat img = cv::imread(fname, cv::IMREAD_UNCHANGED);

  if (img.empty())
  {
    std::cout << "Failed to load image " << fname << std::endl;
    abort();
  }

  if (img.depth() != CV_8U)
  {
    std::cout << "Image depth is not 8 bit: " << fname << std::endl;
    abort();
  }

  cv::Mat cvt4;
  if (img.channels() == 1)
  {
    cv::cvtColor(img, cvt4, cv::COLOR_GRAY2BGRA);
  }
  else if (img.channels() == 3)
  {
    //TODO: BGR to RGB?
    cv::cvtColor(img, cvt4, cv::COLOR_BGR2BGRA);
  }
  else if (img.channels() == 4)
  {
    //TODO: BGR to RGB?
    cvt4 = img;
  }
  else
  {
    std::cout << "Unknown format for file " << fname << std::endl;
    abort();
  }

  return cvt4;
}

// Output classes

struct HighguiOutput : Output
{
  HighguiOutput();

  void send(const cv::Mat& m) override;

  ~HighguiOutput();
};

struct VideoOutput : Output
{
  VideoOutput(const std::string& s, cv::Size imgSize);

  virtual void send(const cv::Mat& m) override;

  ~VideoOutput() { }

  cv::VideoWriter writer;
};


HighguiOutput::HighguiOutput()
{
    cv::namedWindow("tv");
}

void HighguiOutput::send(const cv::Mat &m)
{
    cv::imshow("tv", m);
    cv::waitKey(1);
}

HighguiOutput::~HighguiOutput()
{
    cv::destroyAllWindows();
}

// used with ffmpeg:
// const enum AVCodecID video_codec = AV_CODEC_ID_H264;
// const enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;

VideoOutput::VideoOutput(const std::string &s, cv::Size imgSize)
{
    // cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
    if (!writer.open(s, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), /* fps */ 30, imgSize))
    {
        throw std::runtime_error("Failed to open VideoWriter");
    }
    // TODO: logger
    // if (verbose_p > 1)
    {
        //fprintf(stderr, "%s: opened %s %dx%d\n", progname, s.c_str(), imgSize.width, imgSize.height);
    }
}

void VideoOutput::send(const cv::Mat &m)
{
    cv::Mat out = m;
    if (m.channels() == 4)
    {
        cvtColor(m, out, cv::COLOR_BGRA2BGR);
    }
    writer.write(out);
}


std::shared_ptr<Output> Output::create(const std::string& s, cv::Size imgSize)
{
  if (s.at(0) == ':')
  {
    std::string name = s.substr(1, s.length() - 1);
    if (name == "highgui")
    {
      return std::make_shared<HighguiOutput>();
    }
    else
    {
      throw std::runtime_error("Unknown video output: " + name);
    }
  }
  else
  {
    return std::make_shared<VideoOutput>(s, imgSize);
  }
}