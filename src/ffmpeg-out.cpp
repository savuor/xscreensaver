/* ffmpeg-out, Copyright © 2023-2024 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 *
 * Writing a sequence of XImages to an output MP4 file.
 * Created: 28 Apr 2023 by dmo2118@gmail.com
 */

#include "precomp.hpp"

#include "fixed-funcs.hpp"
#include "ffmpeg-out.hpp"

#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

struct ffmpeg_out_state {
  cv::Ptr<cv::VideoWriter> writer;
};


ffmpeg_out_state *
ffmpeg_out_init (const char *outfile, int output_width, int output_height)
{
  ffmpeg_out_state *ffst = (ffmpeg_out_state *) calloc (1, sizeof(*ffst));

  //const enum AVCodecID video_codec = AV_CODEC_ID_H264;
  //const enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;

  ffst->writer = cv::makePtr<cv::VideoWriter>(outfile + std::string("_ocv.avi"), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                                              30, cv::Size(output_width, output_height));

  if (ffst->writer->isOpened())
  {
    printf("good\n");
  }
  else
  {
    printf("bad\n");
  }

  return ffst;
}


void
ffmpeg_out_add_frame (ffmpeg_out_state *ffst, XImage *img)
{
  cv::Mat m(img->height, img->width, CV_8UC4, (void*)img->data, img->bytes_per_line);
  cv::Mat m3;
  cvtColor(m, m3, cv::COLOR_BGRA2BGR);
  ffst->writer->write(m3);
}


void
ffmpeg_out_close (ffmpeg_out_state *ffst)
{
  ffst->writer->release();
}

