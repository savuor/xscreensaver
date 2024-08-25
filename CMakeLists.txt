cmake_minimum_required(VERSION 3.5)

project(analogtv-xscr C)

add_compile_options(
    "-std=gnu99"
    "-pedantic"
    "-Wall"
    "-Wnested-externs"
    "-Wstrict-prototypes"
    "-Wmissing-prototypes"
    "-Wno-overlength-strings"
    "-pthread"
    "-O2"
)

include_directories(
    "."
    "./src"
    "/usr/include/gdk-pixbuf-2.0"
    "/usr/include/gio-unix-2.0"
    "/usr/include/glib-2.0"
    "/usr/lib/x86_64-linux-gnu/glib-2.0/include"
)

link_libraries(
    "pthread"
    "gdk_pixbuf-2.0"
    "gio-2.0"
    "gobject-2.0"
    "avutil"
    "avcodec"
    "avformat"
    "swscale"
    "swresample"
    "X11"
    "m"
)

add_executable(analogtv-cli
    "./src/analogtv-cli.c"
    "./src/analogtv.c"
    "./src/ximage-loader.c"
    "./src/recanim.c"
    "./src/ffmpeg-out.c"
    "./src/yarandom.c"
    "./src/aligned_malloc.c"
    "./src/thread_util.c"
)
