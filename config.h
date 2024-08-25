/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */


/* xscreensaver, Copyright © 1991-2022 Jamie Zawinski.
 * Generate this file by running 'configure' rather than editing it by hand.
 */


/* Define this to allow root to unlock, when not using PAM. */
/* #undef ALLOW_ROOT_PASSWD */

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
#define ENABLE_NLS 1

/* This is the name of the gettext package to use. */
#define GETTEXT_PACKAGE "xscreensaver"

/* Define this if gettimeofday takes two arguments. */
#define GETTIMEOFDAY_TWO_ARGS 1

/* Define this for Solaris getpwanam. */
/* #undef HAVE_ADJUNCT_PASSWD */

/* Define to 1 if you have the Mac OS X function
   CFLocaleCopyPreferredLanguages in the CoreFoundation framework. */
/* #undef HAVE_CFLOCALECOPYPREFERREDLANGUAGES */

/* Define to 1 if you have the Mac OS X function CFPreferencesCopyAppValue in
   the CoreFoundation framework. */
/* #undef HAVE_CFPREFERENCESCOPYAPPVALUE */

/* Define to 1 if you have the <crypt.h> header file. */
#define HAVE_CRYPT_H 1

/* Define if the GNU dcgettext() function is already present or preinstalled.
   */
#define HAVE_DCGETTEXT 1

/* Define to 1 if you have the <dirent.h> header file, and it defines 'DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define this if you have the X Double Buffer Extension. */
#define HAVE_DOUBLE_BUFFER_EXTENSION 1

/* Define this if you have the XDPMS extension. */
#define HAVE_DPMS_EXTENSION 1

/* Define this if OpenGL bindings are via EGL instead of GLX. */
#define HAVE_EGL 1

/* Define this for DEC/SCO "Enhanced Security" with getprpwnam. */
/* #undef HAVE_ENHANCED_PASSWD */

/* Define to 1 if you have the 'fcntl' function. */
#define HAVE_FCNTL 1

/* Define this if you have the ffmpeg libraries. */
#define HAVE_FFMPEG 1

/* Define this if you have forkpty. */
#define HAVE_FORKPTY 1

/* Define this if you have GDK-Pixbuf. */
#define HAVE_GDK_PIXBUF 1

/* Define this if you have the gdk_pixbuf_apply_embedded_orientation function
   (gdk-pixbuf 2.12). */
#define HAVE_GDK_PIXBUF_APPLY_EMBEDDED_ORIENTATION 1

/* Define this if you have GDK-Pixbuf-Xlib. */
/* #undef HAVE_GDK_PIXBUF_XLIB */

/* Define to 1 if you have the 'getaddrinfo' function. */
#define HAVE_GETADDRINFO 1

/* Define to 1 if you have the 'getcwd' function. */
#define HAVE_GETCWD 1

/* Define this if you have getifaddrs. */
#define HAVE_GETIFADDRS 1

/* Define if the GNU gettext() function is already present or preinstalled. */
#define HAVE_GETTEXT 1

/* Define this if you have gettimeofday. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the 'getwd' function. */
#define HAVE_GETWD 1

/* Define this if you have OpenGL. */
#define HAVE_GL 1

/* Define this if the OpenGL version is 1.1 or newer. */
#define HAVE_GLBINDTEXTURE 1

/* Define this if you have the GL Extrusion library. */
/* #undef HAVE_GLE */

/* Define this if GL Extrusion is version 3. */
/* #undef HAVE_GLE3 */

/* Define this if the native OpenGL library is OpenGLES 1.x. */
#define HAVE_GLES 1

/* Define this if the native OpenGL library is OpenGLES 2.x. */
#define HAVE_GLES2 1

/* Define this if the native OpenGL library is OpenGLES 3.x. */
#define HAVE_GLES3 1

/* Define this if OpenGL supports the OpenGL Shading Language. */
#define HAVE_GLSL 1

/* Define this if you have Gtk 2.x. */
#define HAVE_GTK 1

/* Define this for HPUX so-called "Secure Passwords". */
/* #undef HAVE_HPUX_PASSWD */

/* Define this if ping uses struct icmp.icmp_id. */
#define HAVE_ICMP 1

/* Define this if ping uses struct icmphdr.un.echo.id. */
#define HAVE_ICMPHDR 1

/* Define if you have the iconv() function and it works. */
/* #undef HAVE_ICONV */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define this if you have the JPEG Group's JPEG library. */
#define HAVE_JPEGLIB 1

/* Define this to emulate OpenGL 1.3 in terms of OpenGLES 1.x. */
/* #undef HAVE_JWZGLES */

/* Define this if you want to use Kerberos authentication to lock/unlock the
   screen instead of PAM. */
/* #undef HAVE_KERBEROS */

/* Define this if you have Kerberos 5. */
/* #undef HAVE_KERBEROS5 */

/* Define this if your system has libcap. */
/* #undef HAVE_LIBCAP */

/* Define this if you have libelogind. */
/* #undef HAVE_LIBELOGIND */

/* Define this if you have the Portable Network Graphics library. */
#define HAVE_LIBPNG 1

/* Define this if you have libsystemd. */
/* #undef HAVE_LIBSYSTEMD */

/* Using the MIT-SCREEN-SAVER extension means that the X server will crash at
   random times, and fading and hysteresis won't work. Don't use this. You'll
   be sorry. See comment in xscreensaver.c. */
/* #undef HAVE_MIT_SAVER_EXTENSION */

/* Define this if you have Motif. */
/* #undef HAVE_MOTIF */

/* Define to 1 if you have the <ndir.h> header file, and it defines 'DIR'. */
/* #undef HAVE_NDIR_H */

/* Define to 1 if you have the 'nice' function. */
#define HAVE_NICE 1

/* Define this if you have Pluggable Authentication Modules. */
/* #undef HAVE_PAM */

/* Define this if you have pam_fail_delay. */
/* #undef HAVE_PAM_FAIL_DELAY */

/* Define this if you have a Linux-like /proc/interrupts file which can be
   examined to determine when keyboard activity has occurred, and you use PS/2
   input devices instead of USB. */
/* #undef HAVE_PROC_INTERRUPTS */

/* Define this if you have a Linux-like /proc/.../oom_score_adj file which can
   be adjusted by root to exempt us from the out-of-memory killer. */
#define HAVE_PROC_OOM 1

/* Define this if you have POSIX threads. */
#define HAVE_PTHREAD 1

/* Have PTHREAD_PRIO_INHERIT. */
#define HAVE_PTHREAD_PRIO_INHERIT 1

/* Define to 1 if you have the <pty.h> header file. */
#define HAVE_PTY_H 1

/* Define to 1 if you have the 'putenv' function. */
#define HAVE_PUTENV 1

/* Define this if your system uses getpwnam_shadow. */
/* #undef HAVE_PWNAM_SHADOW_PASSWD */

/* Define this if you have the Resize and Rotate extension. */
#define HAVE_RANDR 1

/* Define this if the RANDR library is version 1.2 or newer. */
#define HAVE_RANDR_12 1

/* Define to 1 if you have the 'realpath' function. */
#define HAVE_REALPATH 1

/* Define this to enable recording of videos. */
/* #undef HAVE_RECORD_ANIM */

/* Define to 1 if you have the 'sbrk' function. */
#define HAVE_SBRK 1

/* Define to 1 if you have the 'select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the 'setlocale' function. */
#define HAVE_SETLOCALE 1

/* Define to 1 if you have the 'setpriority' function. */
#define HAVE_SETPRIORITY 1

/* Define to 1 if you have the 'setrlimit' function. */
#define HAVE_SETRLIMIT 1

/* Define this if you have the SGI SCREEN_SAVER extension. */
/* #undef HAVE_SGI_SAVER_EXTENSION */

/* Define this if you have the SGI-VIDEO-CONTROL extension. */
/* #undef HAVE_SGI_VC_EXTENSION */

/* Define this if your system uses /etc/shadow. */
#define HAVE_SHADOW_PASSWD 1

/* Define to 1 if you have the 'sigaction' function. */
#define HAVE_SIGACTION 1

/* Define to 1 if you have the `sigtimedwait' function. */
/* #undef HAVE_SIGTIMEDWAIT */

/* Define to 1 if you have the 'sqrtf' function. */
/* #undef HAVE_SQRTF */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if 'sa_len' is a member of 'struct sockaddr'. */
/* #undef HAVE_STRUCT_SOCKADDR_SA_LEN */

/* Define to 1 if you have the 'syslog' function. */
#define HAVE_SYSLOG 1

/* Define to 1 if you have the <sys/dir.h> header file, and it defines 'DIR'.
   */
/* #undef HAVE_SYS_DIR_H */

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines 'DIR'.
   */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/termios.h> header file. */
#define HAVE_SYS_TERMIOS_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the 'uname' function. */
#define HAVE_UNAME 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <util.h> header file. */
/* #undef HAVE_UTIL_H */

/* Define this if you have XF86VidModeGetViewPort, for virtual desktops. */
#define HAVE_XF86VMODE 1

/* Define this if you have XF86VidModeSetGamma, for fading. */
#define HAVE_XF86VMODE_GAMMA 1

/* Define this if you have XF86VidModeSetGammaRamp, for fading. */
#define HAVE_XF86VMODE_GAMMA_RAMP 1

/* Define this if you have libXft2. */
#define HAVE_XFT 1

/* Define this if you have XHPDisableReset, which allows Ctrl-Sh-Reset to be
   temporarily disabled. */
/* #undef HAVE_XHPDISABLERESET */

/* Define this if you have the X11R5 XIDLE extension. */
/* #undef HAVE_XIDLE_EXTENSION */

/* Define this if you have the Xinerama extension. */
#define HAVE_XINERAMA 1

/* Define this if you have the XInput2 extension. */
#define HAVE_XINPUT 1

/* Define this if you have the X Keyboard Extension. */
#define HAVE_XKB 1

/* Define this if you have the XmComboBox Motif 2.0 widget. */
/* #undef HAVE_XMCOMBOBOX */

/* Define this if you have the XML library. */
#define HAVE_XML 1

/* Define this if you have the X Shared Memory Extension. */
#define HAVE_XSHM_EXTENSION 1

/* Define this if you have the function Xutf8DrawString. */
#define HAVE_XUTF8DRAWSTRING 1

/* Define this to remove the option of locking the screen at all. */
/* #undef NO_LOCKING */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* Whether PAM should check the result of account modules when authenticating.
   */
/* #undef PAM_CHECK_ACCOUNT_TYPE */

/* The name of the "xscreensaver" PAM service. */
/* #undef PAM_SERVICE_NAME */

/* Define if pam_strerror requires two arguments. */
/* #undef PAM_STRERROR_TWO_ARGS */

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* Return type of signal handlers */
#define RETSIGTYPE void

/* Define to 1 if all of the C89 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Stare into the void. */
/* #undef XPointer */

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define to 1 on platforms where this makes off_t a 64-bit type. */
/* #undef _LARGE_FILES */

/* Number of bits in time_t, on hosts where this is settable. */
/* #undef _TIME_BITS */

/* Define to 1 on platforms where this makes time_t a 64-bit type. */
/* #undef __MINGW_USE_VC2005_COMPAT */

/* Define to 'int' if <sys/types.h> does not define. */
/* #undef mode_t */

/* Define as a signed integer type capable of holding a process identifier. */
/* #undef pid_t */

/* Define as 'unsigned int' if <stddef.h> doesn't define. */
/* #undef size_t */

/* Define to `int' if <sys/types.h> or <sys/socket.h> does not define. */
/* #undef socklen_t */
