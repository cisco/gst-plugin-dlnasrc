dnl required version of autoconf
AC_PREREQ([2.61])

dnl TODO: fill in your package name and package version here
AC_INIT([gstdlnasrc],[1.0.0])

# This will detect the host that is building this software, it will enable us to switch to cross compile mode 
# when necessary
AC_CANONICAL_HOST

# Get system canonical name
# This is necessary for us to cross compile
AC_DEFINE_UNQUOTED(OS, "${host}", [cpu-machine-OS])

#
# If the OS is Linux then enable the GNU system extentions
# This must be done early in the configuration steps.
AC_USE_SYSTEM_EXTENSIONS

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=0.10.16
GSTPB_REQUIRED=0.10.16

AC_CONFIG_SRCDIR([src/gstdlnasrc.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([1.11 subdir-objects])

AM_SILENT_RULES([yes])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
#PKG_CHECK_MODULES(GST, [
#  gstreamer-1.0 >= $GST_REQUIRED
#  gstreamer-base-1.0 >= $GST_REQUIRED
#  gstreamer-controller-1.0 >= $GST_REQUIRED
#], [
#  AC_SUBST(GST_CFLAGS)
#  AC_SUBST(GST_LIBS)
#], [
#  AC_MSG_ERROR([
#      You need to install or upgrade the GStreamer development
#      packages on your system. On debian-based systems these are
#      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
#      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
#      or similar. The minimum version required is $GST_REQUIRED.
#  ])
#])

PKG_CHECK_MODULES(GST, [
  gstreamer-0.10 >= $GST_REQUIRED
  gstreamer-base-0.10 >= $GST_REQUIRED
  gstreamer-controller-0.10 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer0.10-dev and libgstreamer-plugins-base0.10-dev.
      on RPM-based systems gstreamer0.10-devel, libgstreamer0.10-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])


dnl *** soup ***
PKG_CHECK_MODULES([SOUP], [ libsoup-2.4 >= 2.3.2 ], [
  AC_SUBST(SOUP_CFLAGS)
  AC_SUBST(SOUP_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the libsoup on your system. 
      The minimum version required is 2.3.2.
  ])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
#CFLAGS="$CFLAGS -Wall -Werror"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-0.10/plugins"
else
  plugindir="\$(libdir)/gstreamer-0.10"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
# Here is a nice little print to tell the guy configuring your project
# how things are configured.
echo \
"-------------------------------------------------

 ${PACKAGE_NAME} Version ${PACKAGE_VERSION}

 Prefix: '${prefix}'.
 Compiler: '${CC} ${CFLAGS} '
 Building for debug: '${debugit}'
 CC      : ${CC}
 CPP     : ${CPP}
 CFLAGS  : ${CFLAGS} ${LIBCURL_CFLAGS}   
 CPPFLAGS  : ${CPPFLAGS} $LIBCURL_CPPFLAGS

 LINKER  : $LD $LDFLAGS $LIBS

 LD      : $LD
 LDFLAGS : $LDFLAGS ${LIBCURL}
 LIBS    : $LIBS
 PTHREAD : $PTHREAD_LIBS


 Now type 'make @<:@<target>@:>@'
   where the optional <target> is:
     all                - build all binaries
     install            - install everything
     clean              - removes all built source files.
     distclean          - removes everything configure created

 Notes: To build in Verbose mode execute : make V=1

--------------------------------------------------"

