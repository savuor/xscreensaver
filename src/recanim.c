/* recanim, Copyright © 2014-2023 Jamie Zawinski <jwz@jwz.org>
 * Record animation frames of the running screenhack.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#include "precomp.h"

#include "screenhackI.h"

#include "recanim.h"

typedef struct record_anim_state record_anim_state;

extern record_anim_state *screenhack_record_anim_init (Screen *, Window,
                                                       int frames);
extern void screenhack_record_anim (record_anim_state *);
extern void screenhack_record_anim_free (record_anim_state *);

extern time_t screenhack_record_anim_time (time_t *);
extern void screenhack_record_anim_gettimeofday (struct timeval *
                                                 , struct timezone *
                                                 );


#include "ffmpeg-out.h"


#undef gettimeofday  /* wrapped by recanim.h */
#undef time

struct record_anim_state {
  Screen *screen;
  Window window;
  int frame_count;
  int target_frames;
  int fps;
  XWindowAttributes xgwa;
  char *title;
  int secs_elapsed;
  int fade_frames;
  double start_time;
  XImage *img;

  char *outfile;
  ffmpeg_out_state *ffst;
};


static double
double_time (void)
{
  struct timeval now;
  struct timezone tzp;
  gettimeofday(&now, &tzp);

  return (now.tv_sec + ((double) now.tv_usec * 0.000001));
}


/* Some of the hacks set their timing based on the real-world wall clock,
   so to make the animations record at a sensible speed, we need to slow
   down that clock by discounting the time taken up by snapshotting and
   saving the frame.
 */
static double recanim_time_warp = 0;

void
screenhack_record_anim_gettimeofday (struct timeval *tv
                                     , struct timezone *tz

                                     )
{
  gettimeofday (tv, tz);
  tv->tv_sec  -= (time_t) recanim_time_warp;
  tv->tv_usec -= 1000000 * (recanim_time_warp - (time_t) recanim_time_warp);
}

time_t
screenhack_record_anim_time (time_t *o)
{
  struct timeval tv;
  struct timezone tz;

  screenhack_record_anim_gettimeofday (&tv, &tz);
  if (o) *o = tv.tv_sec;
  return tv.tv_sec;
}


record_anim_state *
screenhack_record_anim_init (Screen *screen, Window window, int target_frames)
{
  record_anim_state *st;

  if (target_frames <= 0) return 0;

  st = (record_anim_state *) calloc (1, sizeof(*st));

  st->fps = 30;
  st->screen = screen;
  st->window = window;
  st->target_frames = target_frames;
  st->start_time = double_time();
  st->frame_count = 0;
  st->fade_frames = st->fps * 1.5;

  if (st->fade_frames >= (st->target_frames / 2) - st->fps)
    st->fade_frames = 0;

  custom_XGetWindowAttributes (st->window, &st->xgwa);

  st->img = custom_XCreateImage (st->xgwa.depth,
                          ZPixmap, 0, 0, st->xgwa.width, st->xgwa.height,
                          8, 0);

  st->img->data = (char *) calloc (st->img->height, st->img->bytes_per_line);

  {
    char fn[1024];
    struct stat s;

    const char *soundtrack = 0;
#   define ST "images/drives-200.mp3"
    soundtrack = ST;
    if (stat (soundtrack, &s)) soundtrack = 0;
    if (! soundtrack) soundtrack = "../" ST;
    if (stat (soundtrack, &s)) soundtrack = 0;
    if (! soundtrack) soundtrack = "../../" ST;
    if (stat (soundtrack, &s)) soundtrack = 0;

    sprintf (fn, "%s.%s", progname, "mp4");
    unlink (fn);

    st->outfile = strdup (fn);
    st->ffst = ffmpeg_out_init (st->outfile, soundtrack,
                                st->xgwa.width, st->xgwa.height,
                                3, False);
  }

  return st;
}


/* Fade to black. Assumes data is 3-byte packed.
 */
static void
fade_frame (record_anim_state *st, unsigned char *data, double ratio)
{
  int x, y, i;
  int w = st->xgwa.width;
  int h = st->xgwa.height;
  unsigned char *s = data;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      for (i = 0; i < 3; i++)
        *s++ *= ratio;
}


void
screenhack_record_anim (record_anim_state *st)
{
  double start_time = double_time();
  int obpl    = st->img->bytes_per_line;
  char *odata = st->img->data;

  /* Under XQuartz we can't just do XGetImage on the Window, we have to
     go through an intermediate Pixmap first.  I don't understand why.
     Also, the fucking resize handle shows up as black.  God dammit.
     A workaround for that is to temporarily remove /opt/X11/bin/quartz-wm
   */
  XCopyArea (0, st->window, 0, 0, 0, 0,
             st->xgwa.width, st->xgwa.height, 0, 0);
  XGetSubImage (0, 0, 0, 0, st->xgwa.width, st->xgwa.height,
                ~0L, ZPixmap, st->img, 0, 0);

  /* Convert BGRA to BGR */
  if (st->img->bytes_per_line == st->img->width * 4)
    {
      const char *in = st->img->data;
      char *out = st->img->data;
      int x, y;
      int w = st->img->width;
      int h = st->img->height;
      for (y = 0; y < h; y++)
        {
          const char *in2 = in;
          for (x = 0; x < w; x++)
            {
              *out++ = in2[0];
              *out++ = in2[1];
              *out++ = in2[2];
              in2 += 4;
            }
          in += st->img->bytes_per_line;
        }
      st->img->bytes_per_line = w * 3;
    }
  else if (st->img->bytes_per_line != st->img->width * 3)
    abort();

  if (st->frame_count < st->fade_frames)
    fade_frame (st, (unsigned char *) st->img->data,
                (double) st->frame_count / st->fade_frames);
  else if (st->frame_count >= st->target_frames - st->fade_frames)
    fade_frame (st, (unsigned char *) st->img->data,
                (double) (st->target_frames - st->frame_count - 1) /
                st->fade_frames);

  ffmpeg_out_add_frame (st->ffst, st->img);
  st->img->bytes_per_line = obpl;
  st->img->data = odata;

# ifndef HAVE_JWXYZ		/* Put percent done in window title */
  {
    double now     = double_time();
    double dur     = st->target_frames / (double) st->fps;
    double ratio   = (st->frame_count + 1) / (double) st->target_frames;
    double encoded = dur * ratio;
    double elapsed = now - st->start_time;
    double rate    = encoded / elapsed;
    double remain  = (elapsed / ratio) - elapsed;

    if (st->title && st->secs_elapsed != (int) elapsed)
      {
        char *t2 = (char *) malloc (strlen(st->title) + 100);
        sprintf (t2,
                 "%s: encoded"
                 " %d:%02d:%02d of"
                 " %d:%02d:%02d at"
                 " %d%% speed;"
                 " %d:%02d:%02d elapsed,"
                 " %d:%02d:%02d remaining",
                 st->title,

                 ((int)  encoded) / (60*60),
                 (((int) encoded) / 60) % 60,
                 ((int)  encoded) % 60,

                 ((int)  dur) / (60*60),
                 (((int) dur) / 60) % 60,
                 ((int)  dur) % 60,

                 (int) (100 * rate),

                 ((int)  elapsed) / (60*60),
                 (((int) elapsed) / 60) % 60,
                 ((int)  elapsed) % 60,

                 ((int)  remain) / (60*60),
                 (((int) remain) / 60) % 60,
                 ((int)  remain) % 60
                 );
        XStoreName (0, st->window, t2);
        XSync (0, 0);
        free (t2);
        st->secs_elapsed = elapsed;
      }
  }
# endif /* !HAVE_JWXYZ */

  if (++st->frame_count >= st->target_frames)
    screenhack_record_anim_free (st);

  recanim_time_warp += double_time() - start_time;
}


void
screenhack_record_anim_free (record_anim_state *st)
{
  struct stat s;

  fprintf (stderr, "%s: wrote %d frames\n", progname, st->frame_count);

  free (st->img->data);
  st->img->data = 0;
  custom_XDestroyImage (st->img);

  ffmpeg_out_close (st->ffst);

  if (stat (st->outfile, &s))
    {
      fprintf (stderr, "%s: %s was not created\n", progname, st->outfile);
      exit (1);
    }

  fprintf (stderr, "%s: wrote %s (%.1f MB)\n", progname, st->outfile,
           s.st_size / (float) (1024 * 1024));

  if (st->title)
    free (st->title);
  free (st->outfile);
  free (st);
  exit (0);
}
