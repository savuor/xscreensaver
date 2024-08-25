
def compile_args(include_dirs, flags, defs, out, src):
    args = ["gcc"]
    args.extend(["-I" + s for s in include_dirs])
    args.extend(flags)
    args.extend(["-D" + s for s in defs])
    args.extend(["-o", out])
    args.append(src)
    return args

def link_args(flags, link_dirs, libs, objs, out):
    args = ["gcc"]
    args.extend(flags)
    args.extend(["-o", out])
    args.extend(objs)
    args.extend(["-L" + s for s in link_dirs])
    args.extend(["-l" + s for s in libs])
    return args

include_dirs = [
    ".",
    "./hacks",
    "./utils",
    "/usr/include/at-spi-2.0 ",
    "/usr/include/at-spi2-atk/2.0",
    "/usr/include/atk-1.0",
    "/usr/include/blkid",
    "/usr/include/cairo",
    "/usr/include/dbus-1.0",
    "/usr/include/freetype2",
    "/usr/include/fribidi",
    "/usr/include/gdk-pixbuf-2.0",
    "/usr/include/gio-unix-2.0",
    "/usr/include/glib-2.0",
    "/usr/include/gtk-3.0",
    "/usr/include/harfbuzz",
    "/usr/include/libmount",
    "/usr/include/libpng16",
    "/usr/include/libxml2",
    "/usr/include/pango-1.0",
    "/usr/include/pixman-1",
    "/usr/include/uuid",
    "/usr/lib/x86_64-linux-gnu/dbus-1.0/include",
    "/usr/lib/x86_64-linux-gnu/glib-2.0/include",
]

common_flags = [
    "-std=gnu99",
    "-pedantic",
    "-Wall",
    "-Wnested-externs",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Wno-overlength-strings",
    "-pthread"
]
#compile_flags = common_flags + ["-g", "-O2", "-c"]
compile_flags = common_flags + ["-O2", "-c"]
link_flags = common_flags

common_defs = [
    "HAVE_CONFIG_H"
]

objects = {
    "analogtv-cli"   : ("./hacks/analogtv-cli.c",   "./hacks/analogtv-cli.o",   ["ANALOGTV_SCALE=1", "NO_CONSTRAIN_RATIO", "STANDALONE"]),
    "analogtv"       : ("./hacks/analogtv.c",       "./hacks/analogtv2.o",      ["ANALOGTV_SCALE=1", "NO_CONSTRAIN_RATIO", "STANDALONE"]),
    "ximage-loader"  : ("./hacks/ximage-loader.c",  "./hacks/ximage-loader.o",  ["STANDALONE"]),
    "recanim"        : ("./hacks/recanim.c",        "./hacks/recanim.o",        ["STANDALONE"]),
    "ffmpeg-out"     : ("./hacks/ffmpeg-out.c",     "./hacks/ffmpeg-out.o",     ["STANDALONE"]),
    "yarandom"       : ("./utils/yarandom.c",       "./utils/yarandom.o",       []),
    "aligned_malloc" : ("./utils/aligned_malloc.c", "./utils/aligned_malloc.o", []),
    "thread_util"    : ("./utils/thread_util.c",    "./utils/thread_util.o",    [] ),
    "font-retry"     : ("./utils/font-retry.c",     "./utils/font-retry.o",     ["USE_XFT"]),
}

for k, v in objects.items():
    args = compile_args(include_dirs, compile_flags, common_defs + v[2], v[1], v[0])
    print (" ".join(args))


link_dirs = [
 "/usr/local/lib"
]
libs = [
    "pthread", "gdk_pixbuf-2.0", "gio-2.0", "gobject-2.0", "glib-2.0",
    "avutil", "avcodec", "avformat", "swscale", "swresample",
    "SM", "ICE", "Xft", "Xt", "X11", "Xext", "m"
]
objs = [v[1] for v in objects.values()]
args = link_args(link_flags, link_dirs, libs, objs, "./hacks/analogtv-cli")
print (" ".join(args))
