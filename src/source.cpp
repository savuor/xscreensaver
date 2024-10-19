#include "precomp.hpp"

#include "source.hpp"

#include <opencv2/imgproc.hpp>

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


void ImageSource::setOutSize(cv::Size _outSize)
{
  outSize = _outSize;

  if (resizedImg.size() != outSize)
  {
    double r1 = (double) outSize.width / outSize.height;
    double r2 = (double) resizedImg.cols / resizedImg.rows;
    int w2, h2;
    if (r1 > r2)
    {
      w2 = outSize.height * r2;
      h2 = outSize.height;
    }
    else
    {
      w2 = outSize.width;
      h2 = outSize.width / r2;
    }
    cv::resize(img, resizedImg, cv::Size(w2, h2));
  }
}