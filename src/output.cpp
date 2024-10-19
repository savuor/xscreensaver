#include "precomp.hpp"

#include "output.hpp"
#include "utils.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

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
    Log::write(2, "opened " + s + " " + std::to_string(imgSize.width) + "x" + std::to_string(imgSize.height));
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

std::shared_ptr<Output> Output::create(const std::string &s, cv::Size imgSize)
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