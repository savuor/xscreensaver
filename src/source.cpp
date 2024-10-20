#include "precomp.hpp"

#include "source.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace atv
{

struct BarsSource : Source
{
  static const cv::Size defaultSize; // 320x240

  BarsSource()
  {
    Source::outSize = defaultSize;
  }

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
};

const cv::Size BarsSource::defaultSize = cv::Size {320, 240};

BarsSource::BarsSource(const cv::Mat& _logoImg, cv::Size _outSize)
{
  outSize = _outSize;
  logoImg = _logoImg;

  if (_logoImg.empty())
    return;

  /* Pull the alpha out of the logo and make a separate mask ximage. */
  logoMask = cv::Mat(logoImg.size(), CV_8UC4, cv::Scalar(0));
  std::vector<cv::Mat> logoCh;
  cv::split(logoImg, logoCh);
  cv::Mat z = cv::Mat(logoImg.size(), CV_8UC1, cv::Scalar(0));
  cv::merge(std::vector<cv::Mat> {logoCh[0], logoCh[1], logoCh[2], z}, logoImg);
  cv::merge(std::vector<cv::Mat> {z, z, z, logoCh[3]}, logoMask);
}


void BarsSource::update(AnalogInput& input)
{
  // original name: update_smpte_colorbars()

  /* 
     SMPTE is the society of motion picture and television engineers, and
     these are the standard color bars in the US. Following the partial spec
     at http://broadcastengineering.com/ar/broadcasting_inside_color_bars/
     These are luma, chroma, and phase numbers for each of the 7 bars.
  */
  double top_cb_table[7][3]={
    {75, 0, 0.0},    /* gray */
    {69, 31, 167.0}, /* yellow */
    {56, 44, 283.5}, /* cyan */
    {48, 41, 240.5}, /* green */
    {36, 41, 60.5},  /* magenta */
    {28, 44, 103.5}, /* red */
    {15, 31, 347.0}  /* blue */
  };
  double mid_cb_table[7][3]={
    {15, 31, 347.0}, /* blue */
    {7, 0, 0},       /* black */
    {36, 41, 60.5},  /* magenta */
    {7, 0, 0},       /* black */
    {56, 44, 283.5}, /* cyan */
    {7, 0, 0},       /* black */
    {75, 0, 0.0}     /* gray */
  };

  input.setup_sync(1, 0);

  for (int col = 0; col < 7; col++)
  {
    input.draw_solid_rel_lcp(col*(1.0/7.0),
                             (col+1)*(1.0/7.0),
                             0.00, 0.68,
                             top_cb_table[col][0],
                             top_cb_table[col][1],
                             top_cb_table[col][2]);

    input.draw_solid_rel_lcp(col*(1.0/7.0),
                             (col+1)*(1.0/7.0),
                             0.68, 0.75,
                             mid_cb_table[col][0],
                             mid_cb_table[col][1],
                             mid_cb_table[col][2]);
  }

  input.draw_solid_rel_lcp(      0.0,   1.0/6.0, 0.75, 1.00,   7, 40, 303);   /* -I       */
  input.draw_solid_rel_lcp(  1.0/6.0,   2.0/6.0, 0.75, 1.00, 100,  0,   0);   /* white    */
  input.draw_solid_rel_lcp(  2.0/6.0,   3.0/6.0, 0.75, 1.00,   7, 40,  33);   /* +Q       */
  input.draw_solid_rel_lcp(  3.0/6.0,   4.0/6.0, 0.75, 1.00,   7,  0,   0);   /* black    */
  input.draw_solid_rel_lcp(12.0/18.0, 13.0/18.0, 0.75, 1.00,   3,  0,   0);   /* black -4 */
  input.draw_solid_rel_lcp(13.0/18.0, 14.0/18.0, 0.75, 1.00,   7,  0,   0);   /* black    */
  input.draw_solid_rel_lcp(14.0/18.0, 15.0/18.0, 0.75, 1.00,  11,  0,   0);   /* black +4 */
  input.draw_solid_rel_lcp(  5.0/6.0,   6.0/6.0, 0.75, 1.00,   7,  0,   0);   /* black    */

  if (!this->logoImg.empty())
  {
    int outw = this->outSize.width;
    int outh = this->outSize.height;
    double aspect = (double)outw / outh;
    double scale = aspect > 1 ? 0.35 : 0.6;
    int w2 = outw * scale;
    int h2 = outh * scale * aspect;
    int xoff = (outw - w2) / 2;
    int yoff = outh * 0.20;
    input.load_ximage(this->logoImg, this->logoMask, xoff, yoff, w2, h2, outw, outh);
  }
}


struct ImageSource : Source
{
  ImageSource() :
    img(),
    resizedImg(),
    do_ssavi()
  { }

  ImageSource(const cv::Mat& _img) :
    ImageSource(_img, _img.size(), false)
  { }

  ImageSource(const cv::Mat& _img, cv::Size _outSize, bool _do_ssavi) :
    img(_img),
    resizedImg(_img),
    do_ssavi(_do_ssavi)
  {
    Source::outSize = _outSize;
  }

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

  cv::Mat img;
  cv::Mat resizedImg;
  bool do_ssavi;
};

void ImageSource::update(AnalogInput& input)
{
  //TODO: do not update since last time
  int w = this->resizedImg.cols * 0.815; /* underscan */
  int h = this->resizedImg.rows * 0.970;
  int x = (this->outSize.width  - w) / 2;
  int y = (this->outSize.height - h) / 2;

  input.setup_sync(1, this->do_ssavi);

  input.load_ximage(this->resizedImg, cv::Mat4b(), x, y, w, h, this->outSize.width, this->outSize.height);
}


cv::Size fitSize(cv::Size imgSize, cv::Size outSize)
{
  double r1 = (double) outSize.width / outSize.height;
  double r2 = (double) imgSize.width / imgSize.height;
  cv::Size sz;
  if (r1 > r2)
  {
    sz = { (int)(outSize.height * r2), outSize.height };
  }
  else
  {
    sz = { outSize.width, (int)(outSize.width / r2) };
  }
  return sz;
}


void ImageSource::setOutSize(cv::Size _outSize)
{
  outSize = _outSize;

  if (resizedImg.size() != outSize)
  {
    cv::Size fs = fitSize(resizedImg.size(), outSize);
    cv::resize(img, resizedImg, fs);
  }
}


// Video file or camera
struct VideoSource : Source
{
  VideoSource() :
    frameSize()
  { }

  VideoSource(int nCam);
  VideoSource(const std::string& fileName);

  void update(AnalogInput& input) override;

  cv::Size getImageSize() override
  {
    return frameSize;
  }

  void setOutSize(cv::Size size) override;

  // used for images only
  void setSsavi(bool _do_ssavi) override { }

  cv::Size frameSize, fittedSize;
  cv::VideoCapture cap;
};


VideoSource::VideoSource(int nCam)
{
  if (!cap.open(nCam))
  {
    throw std::runtime_error("Failed to open VideoCapture for camera #" + std::to_string(nCam));
  }

  frameSize = { (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT)};
  fittedSize = frameSize;

  Log::write(2, "opened cam #" + std::to_string(nCam) + " " + std::to_string(frameSize.width) + "x" + std::to_string(frameSize.height));
}

VideoSource::VideoSource(const std::string& fileName)
{
  if (!cap.open(fileName))
  {
    throw std::runtime_error("Failed to open VideoCapture for file " + fileName);
  }

  frameSize = { (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT)};
  fittedSize = frameSize;

  Log::write(2, "opened video file " + fileName + " " + std::to_string(frameSize.width) + "x" + std::to_string(frameSize.height));
}


void VideoSource::setOutSize(cv::Size _outSize)
{
  outSize = _outSize;

  if (fittedSize != outSize)
  {
    fittedSize = fitSize(frameSize, outSize);
  }
}


void VideoSource::update(AnalogInput& input)
{
  cv::Mat frame, prepared;

  //TODO: for a video file: keep time
  cap >> frame;

  if (frame.empty())
  {
    prepared = cv::Mat(fittedSize, CV_8UC4, cv::Scalar(128, 64, 0));

    cv::putText(prepared, "no frame :(", {120, fittedSize.height / 2}, cv::FONT_HERSHEY_SIMPLEX, 5.0, cv::Scalar::all(255), 6);
  }
  else
  {
    cv::Mat resized;
    cv::resize(frame, resized, fittedSize);
    std::vector<cv::Mat> ch;
    cv::split(resized, ch);
    cv::Mat z = cv::Mat(fittedSize, CV_8UC1, cv::Scalar(0));
    cv::merge(std::vector<cv::Mat> {ch[0], ch[1], ch[2], z}, prepared);
  }

  int w = fittedSize.width  * 0.815; /* underscan */
  int h = fittedSize.height * 0.970;
  int x = (this->outSize.width  - w) / 2;
  int y = (this->outSize.height - h) / 2;

  input.setup_sync(1, 0);

  input.load_ximage(prepared, cv::Mat4b(), x, y, w, h, this->outSize.width, this->outSize.height);
}


// the sources can be tuned later for different size or other params
std::shared_ptr<Source> Source::create(const std::string& name)
{
  std::shared_ptr<Source> src;
  if (name.at(0) == ':')
  {
    //TODO: split string by ":"
    size_t at = name.find_first_of(":", 1);
    std::string stype, arg;
    if (at != std::string::npos)
    {
      stype = name.substr(1, at - 1);
      arg = name.substr(at + 1, name.length() - at);
    }
    else
    {
      stype = name.substr(1, name.length() - 1);
    }
    // should be like ":bars" or ":bars:/path/to/image"
    if (stype == "bars")
    {
      cv::Mat logo;
      if (!arg.empty())
      {
        logo = loadImage(arg);
      }
      src = std::make_shared<BarsSource>(logo);
    }
    else if (stype == "cam")
    {
      int nCam = arg.empty() ? 0 : parseInt(arg).value_or(0);
      src = std::make_shared<VideoSource>(nCam);
    }
    else
    {
        throw std::runtime_error("Unknown source type: " + stype);
    }
  }
  else
  {
    const std::set<std::string> knownVideoExtensions = {
      "h264", "h265",
      "mpeg2", "mpeg4", "mp4", "mjpeg", "mpg",
      "vp8", "mov", "wmv", "flv",
      "avi", "mkv"
    };
    int extIdx = name.find_last_of(".");
    std::string ext = name.substr(extIdx + 1, name.length() - extIdx - 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (knownVideoExtensions.count(ext))
    {
      src = std::make_shared<VideoSource>(name);
    }
    else
    {
      cv::Mat img = loadImage(name);
      src = std::make_shared<ImageSource>(img);
    }
  }

  return src;
}

} // ::atv
