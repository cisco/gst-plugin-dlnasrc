/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

/**
 * SECTION:element-souphttpsrc
 *
 * This plugin reads data from a remote location specified by a URI.
 * Supported protocols are 'http', 'https'.
 * 
 * An HTTP proxy must be specified by its URL.
 * If the "http_proxy" environment variable is set, its value is used.
 * If built with libsoup's GNOME integration features, the GNOME proxy
 * configuration will be used, or failing that, proxy autodetection.
 * The #GstDlnaSoupHTTPSrc:proxy property can be used to override the default.
 *
 * In case the #GstDlnaSoupHTTPSrc:iradio-mode property is set and the location is
 * an HTTP resource, souphttpsrc will send special Icecast HTTP headers to the
 * server to request additional Icecast meta-information.
 * If the server is not an Icecast server, it will behave as if the
 * #GstDlnaSoupHTTPSrc:iradio-mode property were not set. If it is, souphttpsrc will
 * output data with a media type of application/x-icy, in which case you will
 * need to use the #ICYDemux element as follow-up element to extract the Icecast
 * metadata and to determine the underlying media type.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v souphttpsrc location=https://some.server.org/index.html
 *     ! filesink location=/home/joe/server.html
 * ]| The above pipeline reads a web page from a server using the HTTPS protocol
 * and writes it to a local file.
 * |[
 * gst-launch -v souphttpsrc user-agent="FooPlayer 0.99 beta"
 *     automatic-redirect=false proxy=http://proxy.intranet.local:8080
 *     location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert
 *     ! audioresample ! alsasink
 * ]| The above pipeline will read and decode and play an mp3 file from a
 * web server using the HTTP protocol. If the server sends redirects,
 * the request fails instead of following the redirect. The specified
 * HTTP proxy server is used. The User-Agent HTTP request header
 * is set to a custom string instead of "GStreamer souphttpsrc."
 * |[
 * gst-launch -v souphttpsrc location=http://10.11.12.13/mjpeg
 *     do-timestamp=true ! multipartdemux
 *     ! image/jpeg,width=640,height=480 ! matroskamux
 *     ! filesink location=mjpeg.mkv
 * ]| The above pipeline reads a motion JPEG stream from an IP camera
 * using the HTTP protocol, encoded as mime/multipart image/jpeg
 * parts, and writes a Matroska motion JPEG file. The width and
 * height properties are set in the caps to provide the Matroska
 * multiplexer with the information to set this in the header.
 * Timestamps are set on the buffers as they arrive from the camera.
 * These are used by the mime/multipart demultiplexer to emit timestamps
 * on the JPEG-encoded video frame buffers. This allows the Matroska
 * multiplexer to timestamp the frames in the resulting file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>             /* atoi() */
#endif
#include <gst/gstelement.h>
//#include <gst/gst-i18n-plugin.h>
#ifdef HAVE_LIBSOUP_GNOME
#include <libsoup/soup-gnome.h>
#else
#include <libsoup/soup.h>
#endif
#include "gstdlnasouphttpsrc.h"
//#include <gst/tag/tag.h>

#define GSTREAMER_010

GST_DEBUG_CATEGORY_STATIC (gst_dlna_soup_http_src_debug);
#define GST_CAT_DEFAULT gst_dlna_soup_http_src_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_IS_LIVE,
  PROP_USER_AGENT,
  PROP_AUTOMATIC_REDIRECT,
  PROP_PROXY,
  PROP_USER_ID,
  PROP_USER_PW,
  PROP_PROXY_ID,
  PROP_PROXY_PW,
  PROP_COOKIES,
  PROP_TIMEOUT,
  PROP_EXTRA_HEADERS,
  PROP_DLNA_SEEKABLE,
  PROP_DLNA_SIZE,
  PROP_DLNA_DURATION
};

#define DEFAULT_USER_AGENT           "GStreamer souphttpsrc "

static void gst_dlna_soup_http_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_dlna_soup_http_src_finalize (GObject * gobject);

static void gst_dlna_soup_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dlna_soup_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_dlna_soup_http_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_dlna_soup_http_src_start (GstBaseSrc * bsrc);
static gboolean gst_dlna_soup_http_src_stop (GstBaseSrc * bsrc);
static gboolean gst_dlna_soup_http_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static gboolean gst_dlna_soup_http_src_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_dlna_soup_http_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_dlna_soup_http_src_query (GstBaseSrc * bsrc, GstQuery * query);

static gboolean gst_dlna_soup_http_src_event (GstBaseSrc * bsrc, GstEvent * event);
static gboolean gst_dlna_soup_http_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_dlna_soup_http_src_unlock_stop (GstBaseSrc * bsrc);
#ifdef GSTREAMER_010
static gboolean gst_dlna_soup_http_src_set_location (GstDlnaSoupHTTPSrc * src,
    const gchar * uri);
#else
static gboolean gst_dlna_soup_http_src_set_location (GstDlnaSoupHTTPSrc * src,
    const gchar * uri, GError ** error);
#endif
static gboolean gst_dlna_soup_http_src_set_proxy (GstDlnaSoupHTTPSrc * src,
    const gchar * uri);
//static char *gst_dlna_soup_http_src_unicodify (const char *str);
static gboolean gst_dlna_soup_http_src_build_message (GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_cancel_message (GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_queue_message (GstDlnaSoupHTTPSrc * src);
static gboolean gst_dlna_soup_http_src_add_range_header (GstDlnaSoupHTTPSrc * src,
    guint64 offset);
static void gst_dlna_soup_http_src_session_unpause_message (GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_session_pause_message (GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_session_close (GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_parse_status (SoupMessage * msg,
    GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_chunk_free (gpointer gstbuf);
static SoupBuffer *gst_dlna_soup_http_src_chunk_allocator (SoupMessage * msg,
    gsize max_len, gpointer user_data);
static void gst_dlna_soup_http_src_got_chunk_cb (SoupMessage * msg,
    SoupBuffer * chunk, GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_response_cb (SoupSession * session,
    SoupMessage * msg, GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_got_headers_cb (SoupMessage * msg,
    GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_got_body_cb (SoupMessage * msg,
    GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_finished_cb (SoupMessage * msg,
    GstDlnaSoupHTTPSrc * src);
static void gst_dlna_soup_http_src_authenticate_cb (SoupSession * session,
    SoupMessage * msg, SoupAuth * auth, gboolean retrying,
    GstDlnaSoupHTTPSrc * src);

static gboolean _print_extra_headers (GstDlnaSoupHTTPSrc * src);

#ifdef GSTREAMER_010
static void
_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_dlna_soup_http_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_dlna_soup_http_src_debug, "dlnasouphttpsrc", 0,
      "DLNA SOUP HTTP src");
}

GST_BOILERPLATE_FULL (GstDlnaSoupHTTPSrc, gst_dlna_soup_http_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _do_init);

static void
gst_dlna_soup_http_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  gst_element_class_set_details_simple (element_class, "HTTP client source",
      "Source/Network",
      "Receive data as a client over the network via HTTP using SOUP",
      "Wouter Cloetens <wouter@mind.be>");
}
#else
#define gst_dlna_soup_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDlnaSoupHTTPSrc, gst_dlna_soup_http_src,
    GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_dlna_soup_http_src_uri_handler_init));
#endif

static void
gst_dlna_soup_http_src_class_init (GstDlnaSoupHTTPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

#ifndef GSTREAMER_010
  GstElementClass *gstelement_class;
  gstelement_class = (GstElementClass *) klass;
#endif

  gobject_class->set_property = gst_dlna_soup_http_src_set_property;
  gobject_class->get_property = gst_dlna_soup_http_src_get_property;
  gobject_class->finalize = gst_dlna_soup_http_src_finalize;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to read from", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "Value of the User-Agent HTTP request header field",
          DEFAULT_USER_AGENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_AUTOMATIC_REDIRECT,
      g_param_spec_boolean ("automatic-redirect", "automatic-redirect",
          "Automatically follow HTTP redirects (HTTP Status Code 3xx)",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "HTTP proxy server URI", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_USER_ID,
      g_param_spec_string ("user-id", "user-id",
          "HTTP location URI user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PW,
      g_param_spec_string ("user-pw", "user-pw",
          "HTTP location URI user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_ID,
      g_param_spec_string ("proxy-id", "proxy-id",
          "HTTP proxy URI user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_PW,
      g_param_spec_string ("proxy-pw", "proxy-pw",
          "HTTP proxy URI user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COOKIES,
      g_param_spec_boxed ("cookies", "Cookies", "HTTP request cookies",
          G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "is-live", "Act like a live source",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "Value in seconds to timeout a blocking I/O (0 = No timeout).", 0,
          3600, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXTRA_HEADERS,
      g_param_spec_boxed ("extra-headers", "Extra Headers",
          "Extra headers to append to the HTTP request",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


 
  g_object_class_install_property (gobject_class,
      PROP_DLNA_SEEKABLE,
      g_param_spec_boolean ("seekable", "DLNA supported seeks",
          "Content is seekable based on DLNA Response headers",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,
      PROP_DLNA_SIZE,
      g_param_spec_uint64("content-size", "DLNA Content Size",
           "Content size according to DLNA HTTP Headers",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,
      PROP_DLNA_DURATION,
      g_param_spec_uint64("content-duration", "DLNA Content Duration",
           "Total normal play time of content according to DLNA HTTP Headers",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_do_seek);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_query);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_event);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dlna_soup_http_src_create);

  #ifndef GSTREAMER_010
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class, "HTTP client source",
      "Source/Network",
      "Receive data as a client over the network via HTTP using SOUP",
      "Wouter Cloetens <wouter@mind.be>");
  GST_DEBUG_CATEGORY_INIT (gst_dlna_soup_http_src_debug, "dlnasouphttpsrc", 0,
      "DLNA SOUP HTTP src");
  #endif
}

static void
gst_dlna_soup_http_src_reset (GstDlnaSoupHTTPSrc * src)
{
  src->interrupted = FALSE;
  src->retry = FALSE;
  src->have_size = FALSE;
  src->seekable = FALSE;
  src->read_position = 0;
  src->request_position = 0;
  src->content_size = 0;
  src->request_rate = 1.0;
  src->current_rate = 1.0;

  gst_caps_replace (&src->src_caps, NULL);
  g_free (src->iradio_name);
  src->iradio_name = NULL;
  g_free (src->iradio_genre);
  src->iradio_genre = NULL;
  g_free (src->iradio_url);
  src->iradio_url = NULL;
  }

static void
#ifdef GSTREAMER_010
gst_dlna_soup_http_src_init (GstDlnaSoupHTTPSrc * src,
		                     GstDlnaSoupHTTPSrcClass * g_class)
#else
gst_dlna_soup_http_src_init (GstDlnaSoupHTTPSrc * src)
#endif
{
  const gchar *proxy;

  src->location = NULL;
  src->automatic_redirect = TRUE;
  src->user_agent = g_strdup (DEFAULT_USER_AGENT);
  src->user_id = NULL;
  src->user_pw = NULL;
  src->proxy_id = NULL;
  src->proxy_pw = NULL;
  src->cookies = NULL;
  src->iradio_mode = FALSE;
  src->loop = NULL;
  src->context = NULL;
  src->session = NULL;
  src->msg = NULL;
  proxy = g_getenv ("http_proxy");
  if (proxy && !gst_dlna_soup_http_src_set_proxy (src, proxy)) {
    GST_WARNING_OBJECT (src,
        "The proxy in the http_proxy env var (\"%s\") cannot be parsed.",
        proxy);
  }

  gst_dlna_soup_http_src_reset (src);

  // Sets the default format of the source. This will be the format used
  // for sending NEW_SEGMENT events and for performing seeks.
  //gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
  gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_BYTES);
}

static void
gst_dlna_soup_http_src_finalize (GObject * gobject)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "finalize");

  g_free (src->location);
  g_free (src->user_agent);
  if (src->proxy != NULL) {
    soup_uri_free (src->proxy);
  }
  g_free (src->user_id);
  g_free (src->user_pw);
  g_free (src->proxy_id);
  g_free (src->proxy_pw);
  g_strfreev (src->cookies);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_dlna_soup_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);

      if (location == NULL) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }
#ifdef GSTREAMER_010
      if (!gst_dlna_soup_http_src_set_location (src, location)) {
#else
      if (!gst_dlna_soup_http_src_set_location (src, location, NULL)) {
#endif
        GST_WARNING ("badly formatted location");
        goto done;
      }
      break;
    }
    case PROP_USER_AGENT:
      if (src->user_agent)
        g_free (src->user_agent);
      src->user_agent = g_value_dup_string (value);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      src->automatic_redirect = g_value_get_boolean (value);
      break;
    case PROP_PROXY:
    {
      const gchar *proxy;

      proxy = g_value_get_string (value);

      if (proxy == NULL) {
        GST_WARNING ("proxy property cannot be NULL");
        goto done;
      }
      if (!gst_dlna_soup_http_src_set_proxy (src, proxy)) {
        GST_WARNING ("badly formatted proxy URI");
        goto done;
      }
      break;
    }
    case PROP_COOKIES:
      g_strfreev (src->cookies);
      src->cookies = g_strdupv (g_value_get_boxed (value));
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    case PROP_USER_ID:
      if (src->user_id)
        g_free (src->user_id);
      src->user_id = g_value_dup_string (value);
      break;
    case PROP_USER_PW:
      if (src->user_pw)
        g_free (src->user_pw);
      src->user_pw = g_value_dup_string (value);
      break;
    case PROP_PROXY_ID:
      if (src->proxy_id)
        g_free (src->proxy_id);
      src->proxy_id = g_value_dup_string (value);
      break;
    case PROP_PROXY_PW:
      if (src->proxy_pw)
        g_free (src->proxy_pw);
      src->proxy_pw = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint (value);
      break;
    case PROP_EXTRA_HEADERS:{

      GST_INFO_OBJECT(src, "Set extra headers property called");

      const GstStructure *s = gst_value_get_structure (value);
      if (!s)
      {
          GST_ERROR_OBJECT(src, "Supplied value for extra headers was NULL");
          break;
      }

      if (src->extra_headers)
        gst_structure_free (src->extra_headers);

      src->extra_headers = s ? gst_structure_copy (s) : NULL;

      // Print out extra headers that were set
      if (src->extra_headers)
      {
          GST_LOG_OBJECT(src, "Printing out extra headers");
    	  _print_extra_headers(src);
      }
      else
      {
          GST_ERROR_OBJECT(src, "Extra headers were not added");
      }

      break;
    }
    case PROP_DLNA_SEEKABLE:
      src->seekable = g_value_get_boolean (value);
      break;

    case PROP_DLNA_SIZE:
        src->content_size = g_value_get_uint64(value);
        GST_INFO_OBJECT(src, "New content size set: %llu.",
                src->content_size);
        if (src->content_size > 0)
        {
        	src->have_size = TRUE;
        }
        else
        {
           	src->have_size = FALSE;
        }
        GST_INFO_OBJECT(src, "Have size?: %d", src->have_size);
        break;

    case PROP_DLNA_DURATION:
        src->content_duration = g_value_get_uint64(value);
        GST_INFO_OBJECT(src, "New content duration set: %llu.",
                src->content_duration);
        break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
done:
  return;
}

static void
gst_dlna_soup_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_USER_AGENT:
      g_value_set_string (value, src->user_agent);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      g_value_set_boolean (value, src->automatic_redirect);
      break;
    case PROP_PROXY:
      if (src->proxy == NULL)
        g_value_set_static_string (value, "");
      else {
        char *proxy = soup_uri_to_string (src->proxy, FALSE);

        g_value_set_string (value, proxy);
        g_free (proxy);
      }
      break;
    case PROP_COOKIES:
      g_value_set_boxed (value, g_strdupv (src->cookies));
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    case PROP_USER_ID:
      g_value_set_string (value, src->user_id);
      break;
    case PROP_USER_PW:
      g_value_set_string (value, src->user_pw);
      break;
    case PROP_PROXY_ID:
      g_value_set_string (value, src->proxy_id);
      break;
    case PROP_PROXY_PW:
      g_value_set_string (value, src->proxy_pw);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, src->timeout);
      break;
    case PROP_EXTRA_HEADERS:
      gst_value_set_structure (value, src->extra_headers);
      break;
    case PROP_DLNA_SEEKABLE:
      g_value_set_boolean (value, src->seekable);
      break;
    case PROP_DLNA_SIZE:
      g_value_set_uint64(value, src->content_size);
      break;
    case PROP_DLNA_DURATION:
      g_value_set_uint64(value, src->content_duration);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
static gchar *
gst_dlna_soup_http_src_unicodify (const gchar * str)
{
  //const gchar *env_vars[] = { "GST_ICY_TAG_ENCODING",
  //  "GST_TAG_ENCODING", NULL
  //};

  //return tag_freeform_string_to_utf8 (str, -1, env_vars);
  return NULL;
}
*/
static void
gst_dlna_soup_http_src_cancel_message (GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  if (src->msg != NULL) {
    src->session_io_status = GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED;
    GST_INFO_OBJECT (src, "STATE: CANCELLED");
    soup_session_cancel_message (src->session, src->msg, SOUP_STATUS_CANCELLED);
  }
  src->session_io_status = GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE;
  GST_INFO_OBJECT (src, "STATE: IDLE");
  src->msg = NULL;
}

static void
gst_dlna_soup_http_src_queue_message (GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  // *TODO* - are we calling this with null msg?
  soup_session_queue_message (src->session, src->msg,
      (SoupSessionCallback) gst_dlna_soup_http_src_response_cb, src);
  src->session_io_status = GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_QUEUED;
  GST_INFO_OBJECT (src, "STATE: QUEUED");
}

static gboolean
gst_dlna_soup_http_src_add_range_header (GstDlnaSoupHTTPSrc * src, guint64 offset)
{
  gchar buf[64];

  gint rc;
  GST_INFO_OBJECT(src, "Adding range header: %llu.", offset);

  soup_message_headers_remove (src->msg->request_headers, "Range");
  if (offset) {
    rc = g_snprintf (buf, sizeof (buf), "bytes=%" G_GUINT64_FORMAT "-", offset);
    if (rc > sizeof (buf) || rc < 0)
      return FALSE;
    soup_message_headers_append (src->msg->request_headers, "Range", buf);
  }
  src->read_position = offset;
  return TRUE;
}

static gboolean
_append_extra_header (GQuark field_id, const GValue * value, gpointer user_data)
{
	GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (user_data);
	GST_LOG_OBJECT(src, "called");

	const gchar *field_name = g_quark_to_string (field_id);
	gchar *field_content = NULL;

	if (G_VALUE_TYPE (value) == G_TYPE_STRING)
	{
		field_content = g_value_dup_string (value);
	}
	else
	{
		GValue dest = { 0, };

		g_value_init (&dest, G_TYPE_STRING);
		if (g_value_transform (value, &dest))
		{
			field_content = g_value_dup_string (&dest);
		}
	}

	if (field_content == NULL)
	{
		GST_ERROR_OBJECT (src, "extra-headers field '%s' contains no value "
				"or can't be converted to a string", field_name);
		return FALSE;
	}

	GST_DEBUG_OBJECT (src, "Appending extra header: \"%s: %s\"", field_name,
			field_content);
	soup_message_headers_append (src->msg->request_headers, field_name,
			field_content);

	g_free (field_content);

	return TRUE;
}

static gboolean
_append_extra_headers (GQuark field_id, const GValue * value, gpointer user_data)
{
	GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (user_data);
	GST_LOG_OBJECT(src, "called");
	if (G_VALUE_TYPE (value) == GST_TYPE_ARRAY)
	{
		guint n = gst_value_array_get_size (value);
		guint i;

		for (i = 0; i < n; i++)
		{
			const GValue *v = gst_value_array_get_value (value, i);

			if (!_append_extra_header (field_id, v, user_data))
				return FALSE;
		}
	}
	else if (G_VALUE_TYPE (value) == GST_TYPE_LIST)
	{
		guint n = gst_value_list_get_size (value);
		guint i;

		for (i = 0; i < n; i++)
		{
			const GValue *v = gst_value_list_get_value (value, i);

			if (!_append_extra_header (field_id, v, user_data))
				return FALSE;
		}
	}
	else
	{
		return _append_extra_header (field_id, value, user_data);
	}

	return TRUE;
}


static gboolean
_print_extra_headers (GstDlnaSoupHTTPSrc* src)
{
	GST_LOG_OBJECT(src, "called");

	// Convert GstStructure to GValue
	GValue value = { 0 };
	g_value_init(&value, GST_TYPE_STRUCTURE);
	gst_value_set_structure(&value, src->extra_headers);

	if (G_VALUE_TYPE (&value) == GST_TYPE_ARRAY)
	{
		guint n = gst_value_array_get_size (&value);
		guint i;

		for (i = 0; i < n; i++)
		{
			//const GValue *v = gst_value_array_get_value (value, i);

			//if (!_append_extra_header (field_id, v, user_data))
			//	return FALSE;
		}
	}
	else if (G_VALUE_TYPE (&value) == GST_TYPE_LIST)
	{
		guint n = gst_value_list_get_size (&value);
		guint i;

		for (i = 0; i < n; i++)
		{
			//const GValue *v = gst_value_list_get_value (value, i);

			//if (!_append_extra_header (field_id, v, user_data))
			//	return FALSE;
		}
	}
	else
	{
		//return _append_extra_header (field_id, value, user_data);
	}

	return TRUE;
}

static gboolean
gst_dlna_soup_http_src_add_extra_headers (GstDlnaSoupHTTPSrc * src)
{
	GST_LOG_OBJECT (src, "Called");
	if (!src->extra_headers)
	{
		// No extra headers, just return
		GST_LOG_OBJECT (src, "No extra headers to be added");
		return TRUE;
	}
	GST_DEBUG_OBJECT (src, "Calling append extra headers");
	return gst_structure_foreach (src->extra_headers, _append_extra_headers, src);
}


static void
gst_dlna_soup_http_src_session_unpause_message (GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "Called");

  // Resumes HTTP I/O
  soup_session_unpause_message (src->session, src->msg);
}

static void
gst_dlna_soup_http_src_session_pause_message (GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "Called");

  soup_session_pause_message (src->session, src->msg);
}

static void
gst_dlna_soup_http_src_session_close (GstDlnaSoupHTTPSrc * src)
{
  if (src->session) {
    soup_session_abort (src->session);  /* This unrefs the message. */
    g_object_unref (src->session);
    src->session = NULL;
    src->msg = NULL;
  }
}

static void
gst_dlna_soup_http_src_authenticate_cb (SoupSession * session, SoupMessage * msg,
    SoupAuth * auth, gboolean retrying, GstDlnaSoupHTTPSrc * src)
{
  if (!retrying) {
    /* First time authentication only, if we fail and are called again with retry true fall through */
    if (msg->status_code == SOUP_STATUS_UNAUTHORIZED) {
      if (src->user_id && src->user_pw)
        soup_auth_authenticate (auth, src->user_id, src->user_pw);
    } else if (msg->status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      if (src->proxy_id && src->proxy_pw)
        soup_auth_authenticate (auth, src->proxy_id, src->proxy_pw);
    }
  }
}

static void
gst_dlna_soup_http_src_headers_foreach (const gchar * name, const gchar * val,
    gpointer src)
{
  GST_DEBUG_OBJECT (src, " %s: %s", name, val);
}

static void
gst_dlna_soup_http_src_got_headers_cb (SoupMessage * msg, GstDlnaSoupHTTPSrc * src)
{
  const char *value;
  //GstTagList *tag_list = NULL;
  GstBaseSrc *basesrc;
  guint64 newsize;
  GHashTable *params = NULL;

  GST_DEBUG_OBJECT (src, "Printing out headers received in response:");
  soup_message_headers_foreach (msg->response_headers,
      gst_dlna_soup_http_src_headers_foreach, src);

  if (msg->status_code == 407 && src->proxy_id && src->proxy_pw)
    return;

  if (src->automatic_redirect && SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
    GST_DEBUG_OBJECT (src, "%u redirect to \"%s\"", msg->status_code,
#ifdef GSTREAMER_010
        soup_message_headers_get (msg->response_headers, "Location"));
#else
        soup_message_headers_get_one (msg->response_headers, "Location"));
#endif
    return;
  }

  if (msg->status_code == SOUP_STATUS_UNAUTHORIZED)
    return;

  src->session_io_status = GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING;
  GST_INFO_OBJECT (src, "STATE: RUNNING");

  // Parse Content-Length.
  if (soup_message_headers_get_encoding (msg->response_headers) ==
      SOUP_ENCODING_CONTENT_LENGTH) {
    newsize = src->request_position +
        soup_message_headers_get_content_length (msg->response_headers);
    if (!src->have_size || (src->content_size != newsize)) {
      src->content_size = newsize;
      src->have_size = TRUE;
      src->seekable = TRUE;
      GST_DEBUG_OBJECT (src, "size = %" G_GUINT64_FORMAT, src->content_size);

      basesrc = GST_BASE_SRC_CAST (src);
#ifdef GSTREAMER_010
      gst_segment_set_duration (&basesrc->segment, GST_FORMAT_BYTES,
          src->content_size);
#else
      basesrc->segment.duration = src->content_size;
#endif
      gst_element_post_message (GST_ELEMENT (src),
#ifdef GSTREAMER_010
      gst_message_new_duration (GST_OBJECT (src), GST_FORMAT_BYTES,
                                    src->content_size));
#else
      gst_message_new_duration_changed (GST_OBJECT (src)));
#endif
    }
  }

  // Icecast stuff
  //tag_list = gst_tag_list_new ();

  if ((value =
          soup_message_headers_get (msg->response_headers,
              "icy-metaint")) != NULL)
  {
    gint icy_metaint = atoi (value);

    GST_DEBUG_OBJECT (src, "icy-metaint: %s (parsed: %d)", value, icy_metaint);
    if (icy_metaint > 0)
    {
      if (src->src_caps)
        gst_caps_unref (src->src_caps);

      src->src_caps = gst_caps_new_simple ("application/x-icy",
          "metadata-interval", G_TYPE_INT, icy_metaint, NULL);
#ifndef GSTREAMER_010
      gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
#endif
    }
  }
  if ((value =
          soup_message_headers_get_content_type (msg->response_headers,
              &params)) != NULL)
  {
    GST_DEBUG_OBJECT (src, "Content-Type: %s", value);
    if (g_ascii_strcasecmp (value, "audio/L16") == 0)
    {
      gint channels = 2;
      gint rate = 44100;
      char *param;

      if (src->src_caps)
        gst_caps_unref (src->src_caps);

      param = g_hash_table_lookup (params, "channels");
      if (param != NULL)
        channels = atol (param);

      param = g_hash_table_lookup (params, "rate");
      if (param != NULL)
        rate = atol (param);

#ifdef GSTREAMER_010
      src->src_caps = gst_caps_new_simple ("audio/x-raw-int",
          "channels", G_TYPE_INT, channels,
          "rate", G_TYPE_INT, rate,
          "width", G_TYPE_INT, 16,
          "depth", G_TYPE_INT, 16,
          "signed", G_TYPE_BOOLEAN, TRUE,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
#else
      src->src_caps = gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, "S16BE",
          "layout", G_TYPE_STRING, "interleaved",
          "channels", G_TYPE_INT, channels, "rate", G_TYPE_INT, rate, NULL);
#endif
    }
    else
    {
      // Set the Content-Type field on the caps
      if (src->src_caps)
      {
#ifdef GSTREAMER_010
        gst_caps_set_simple (src->src_caps, "content-type", G_TYPE_STRING,
            value, NULL);
#else
        src->src_caps = gst_caps_make_writable (src->src_caps);
        gst_caps_set_simple (src->src_caps, "content-type", G_TYPE_STRING,
          value, NULL);
        gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
#endif
      }
    }
  }

  if (params != NULL)
    g_hash_table_destroy (params);
/*
  if ((value =
#ifdef GSTREAMER_010
         soup_message_headers_get (msg->response_headers,
#else
         soup_message_headers_get_one (msg->response_headers,
#endif
              "icy-name")) != NULL)
  {
    g_free (src->iradio_name);
    src->iradio_name = gst_dlna_soup_http_src_unicodify (value);
    if (src->iradio_name)
    {
#ifdef GSTREAMER_010
      g_object_notify (G_OBJECT (src), "iradio-name");
#endif
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
          src->iradio_name, NULL);
    }
  }

  if ((value =
#ifdef GSTREAMER_010
          soup_message_headers_get (msg->response_headers,
#else
          soup_message_headers_get_one (msg->response_headers,
#endif
              "icy-genre")) != NULL)
  {
    g_free (src->iradio_genre);
    src->iradio_genre = gst_dlna_soup_http_src_unicodify (value);
    if (src->iradio_genre)
    {
#ifdef GSTREAMER_010
      g_object_notify (G_OBJECT (src), "iradio-genre");
#endif
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
          src->iradio_genre, NULL);
    }
  }
  #ifdef GSTREAMER_010
  if ((value = soup_message_headers_get (msg->response_headers, "icy-url"))
  #else
  if ((value = soup_message_headers_get_one (msg->response_headers, "icy-url"))
  #endif
      != NULL)
  {
    g_free (src->iradio_url);
    src->iradio_url = gst_dlna_soup_http_src_unicodify (value);
    if (src->iradio_url)
    {
 #ifdef GSTREAMER_010
      g_object_notify (G_OBJECT (src), "iradio-url");
 #endif
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_LOCATION,
          src->iradio_url, NULL);
    }
  }
  if (!gst_tag_list_is_empty (tag_list))
  {
    GST_DEBUG_OBJECT (src,
        "calling gst_element_found_tags with %" GST_PTR_FORMAT, tag_list);
#ifdef GSTREAMER_010
    gst_element_found_tags (GST_ELEMENT_CAST (src), tag_list);
#else
    gst_pad_push_event (GST_BASE_SRC_PAD (src), gst_event_new_tag (tag_list));
#endif
  }
  else
  {
 #ifdef GSTREAMER_010
     gst_tag_list_free (tag_list);
 #else
     gst_tag_list_unref (tag_list);
 #endif
  }
  */

  // Handle HTTP errors.
  gst_dlna_soup_http_src_parse_status (msg, src);

  // Check if Range header was respected.
  // *TODO* - fix this up since can't include range when playspeed is included
  /*
  if (src->ret == GST_FLOW_CUSTOM_ERROR &&
      src->read_position && msg->status_code != SOUP_STATUS_PARTIAL_CONTENT)
  {
    src->seekable = FALSE;
    GST_ERROR_OBJECT (src, "Server does not accept Range HTTP header, URL: %s",
    		src->location);
    src->ret = GST_FLOW_ERROR;
  }
  */
}

/* Have body. Signal EOS. */
static void
gst_dlna_soup_http_src_got_body_cb (SoupMessage * msg, GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got body, but not for current message");
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* Probably a redirect. */
    return;
  }
  GST_DEBUG_OBJECT (src, "got body");
  #ifdef GSTREAMER_010
  src->ret = GST_FLOW_UNEXPECTED;
  #else
  src->ret = GST_FLOW_EOS;
  #endif
  if (src->loop)
    g_main_loop_quit (src->loop);
  GST_LOG_OBJECT (src, "Pausing");
  gst_dlna_soup_http_src_session_pause_message (src);
}

/* Finished. Signal EOS. */
static void
gst_dlna_soup_http_src_finished_cb (SoupMessage * msg, GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "finished, but not for current message");
    return;
  }
  GST_LOG_OBJECT (src, "finished");
  #ifdef GSTREAMER_010
  src->ret = GST_FLOW_UNEXPECTED;
  #else
  src->ret = GST_FLOW_EOS;
  #endif
  if (src->session_io_status == GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED) {
    /* gst_dlna_soup_http_src_cancel_message() triggered this; probably a seek
     * that occurred in the QUEUEING state; i.e. before the connection setup
     * was complete. Do nothing */
  } else if (src->session_io_status ==
      GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING && src->read_position > 0) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
    src->ret = GST_FLOW_CUSTOM_ERROR;
  } else if (G_UNLIKELY (src->session_io_status !=
          GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* FIXME: reason_phrase is not translated, add proper error message */
    //GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
    //    ("%s", msg->reason_phrase),
    //    ("libsoup status code %d", msg->status_code));
  }
  if (src->loop)
    g_main_loop_quit (src->loop);
}

/* Buffer lifecycle management.
 *
 * gst_dlna_soup_http_src_create() runs the GMainLoop for this element, to let
 * Soup take control.
 * A GstBuffer is allocated in gst_dlna_soup_http_src_chunk_allocator() and
 * associated with a SoupBuffer.
 * Soup reads HTTP data in the GstBuffer's data buffer.
 * The gst_dlna_soup_http_src_got_chunk_cb() is then called with the SoupBuffer.
 * That sets gst_dlna_soup_http_src_create()'s return argument to the GstBuffer,
 * increments its refcount (to 2), pauses the flow of data from the HTTP
 * source to prevent gst_dlna_soup_http_src_got_chunk_cb() from being called
 * again and breaks out of the GMainLoop.
 * Because the SOUP_MESSAGE_OVERWRITE_CHUNKS flag is set, Soup frees the
 * SoupBuffer and calls gst_dlna_soup_http_src_chunk_free(), which decrements the
 * refcount (to 1).
 * gst_dlna_soup_http_src_create() returns the GstBuffer. It will be freed by a
 * downstream element.
 * If Soup fails to read HTTP data, it does not call
 * gst_dlna_soup_http_src_got_chunk_cb(), but still frees the SoupBuffer and
 * calls gst_dlna_soup_http_src_chunk_free(), which decrements the GstBuffer's
 * refcount to 0, freeing it.
 */

#ifndef GSTREAMER_010
typedef struct
{
  GstBuffer *buffer;
  GstMapInfo map;
} SoupGstChunk;
#endif
static void
#ifdef GSTREAMER_010
gst_dlna_soup_http_src_chunk_free (gpointer gstbuf)
{
  gst_buffer_unref (GST_BUFFER_CAST (gstbuf));
#else
gst_dlna_soup_http_src_chunk_free (gpointer user_data)
{
  SoupGstChunk *chunk = (SoupGstChunk *) user_data;

  gst_buffer_unmap (chunk->buffer, &chunk->map);
  gst_buffer_unref (chunk->buffer);
  g_slice_free (SoupGstChunk, chunk);
#endif
}

static SoupBuffer *
gst_dlna_soup_http_src_chunk_allocator (SoupMessage * msg, gsize max_len,
    gpointer user_data)
{
  GstDlnaSoupHTTPSrc *src = (GstDlnaSoupHTTPSrc *) user_data;
  GstBaseSrc *basesrc = GST_BASE_SRC_CAST (src);
  GstBuffer *gstbuf;
  SoupBuffer *soupbuf;
  gsize length;
  GstFlowReturn rc;

#ifndef GSTREAMER_010
  SoupGstChunk *chunk;
#endif

  if (max_len)
    length = MIN (basesrc->blocksize, max_len);
  else
    length = basesrc->blocksize;
  GST_LOG_OBJECT (src, "alloc %" G_GSIZE_FORMAT " bytes <= %" G_GSIZE_FORMAT,
      length, max_len);

#ifdef GSTREAMER_010
  rc = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
      GST_BUFFER_OFFSET_NONE, length,
      src->src_caps ? src->src_caps :
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)), &gstbuf);
#else
  rc = GST_BASE_SRC_CLASS (parent_class)->alloc (basesrc, -1, length, &gstbuf);
#endif
  if (G_UNLIKELY (rc != GST_FLOW_OK)) {
    /* Failed to allocate buffer. Stall SoupSession and return error code
     * to create(). */
    src->ret = rc;
    g_main_loop_quit (src->loop);
    return NULL;
  }

#ifdef GSTREAMER_010
  soupbuf = soup_buffer_new_with_owner (GST_BUFFER_DATA (gstbuf), length,
      gstbuf, gst_dlna_soup_http_src_chunk_free);
#else
  chunk = g_slice_new0 (SoupGstChunk);
  chunk->buffer = gstbuf;
  gst_buffer_map (gstbuf, &chunk->map, GST_MAP_READWRITE);
  soupbuf = soup_buffer_new_with_owner (chunk->map.data, chunk->map.size,
      chunk, gst_dlna_soup_http_src_chunk_free);
#endif

  return soupbuf;
}

static void
gst_dlna_soup_http_src_got_chunk_cb (SoupMessage * msg, SoupBuffer * chunk,
    GstDlnaSoupHTTPSrc * src)
{
  GstBaseSrc *basesrc;
  guint64 new_position;

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got chunk, but not for current message");
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* Probably a redirect. */
    return;
  }
  basesrc = GST_BASE_SRC_CAST (src);
  GST_LOG_OBJECT (src, "got chunk of %" G_GSIZE_FORMAT " bytes",
      chunk->length);

  /* Extract the GstBuffer from the SoupBuffer and set its fields. */
  *src->outbuf = GST_BUFFER_CAST (soup_buffer_get_owner (chunk));
#ifdef GSTREAMER_010
  GST_BUFFER_SIZE (*src->outbuf) = chunk->length;
  GST_BUFFER_OFFSET (*src->outbuf) = basesrc->segment.last_stop;

  gst_buffer_set_caps (*src->outbuf,
      (src->src_caps) ? src->src_caps :
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)));
#else
 gst_buffer_resize (*src->outbuf, 0, chunk->length);
  GST_BUFFER_OFFSET (*src->outbuf) = basesrc->segment.position;
#endif

  gst_buffer_ref (*src->outbuf);

  new_position = src->read_position + chunk->length;
  if (G_LIKELY (src->request_position == src->read_position))
    src->request_position = new_position;
  src->read_position = new_position;

  src->ret = GST_FLOW_OK;
  g_main_loop_quit (src->loop);
  GST_LOG_OBJECT (src, "Pausing");
  gst_dlna_soup_http_src_session_pause_message (src);
}

static void
gst_dlna_soup_http_src_response_cb (SoupSession * session, SoupMessage * msg,
    GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got response %d: %s, but not for current message",
        msg->status_code, msg->reason_phrase);
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)
      && SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
    /* Ignore redirections. */
    return;
  }
  GST_DEBUG_OBJECT (src, "got response %d: %s", msg->status_code,
      msg->reason_phrase);
  if (src->session_io_status == GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING &&
      src->read_position > 0) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
  } else
    gst_dlna_soup_http_src_parse_status (msg, src);
  /* The session's SoupMessage object expires after this callback returns. */
  src->msg = NULL;
  g_main_loop_quit (src->loop);
}

/*
#define SOUP_HTTP_SRC_ERROR(src,soup_msg,cat,code,error_message)     \
  GST_ELEMENT_ERROR ((src), cat, code, ("%s", error_message),        \
      ("%s (%d), URL: %s", (soup_msg)->reason_phrase,                \
          (soup_msg)->status_code, (src)->location));
*/

static void
gst_dlna_soup_http_src_parse_status (SoupMessage * msg, GstDlnaSoupHTTPSrc * src)
{
  if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code)) {
    switch (msg->status_code) {
      case SOUP_STATUS_CANT_RESOLVE:
      case SOUP_STATUS_CANT_RESOLVE_PROXY:
        //SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, NOT_FOUND,
        //    ("Could not resolve server name."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANT_CONNECT:
      case SOUP_STATUS_CANT_CONNECT_PROXY:
        //SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
        //    ("Could not establish connection to server."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_SSL_FAILED:
        //SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
        //    ("Secure connection setup failed."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_IO_ERROR:
        //SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
        //    ("A network error occured, or the server closed the connection "
        //        "unexpectedly."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_MALFORMED:
        //SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
        //    ("Server sent bad data."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANCELLED:
        /* No error message when interrupted by program. */
        break;
    }
  } else if (SOUP_STATUS_IS_CLIENT_ERROR (msg->status_code) ||
      SOUP_STATUS_IS_REDIRECTION (msg->status_code) ||
      SOUP_STATUS_IS_SERVER_ERROR (msg->status_code)) {
    /* Report HTTP error. */
    /* FIXME: reason_phrase is not translated and not suitable for user
     * error dialog according to libsoup documentation.
     * FIXME: error code (OPEN_READ vs. READ) should depend on http status? */
    //GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
    //    ("%s", msg->reason_phrase),
    //    ("%s (%d), URL: %s", msg->reason_phrase, msg->status_code,
    //        src->location));
    src->ret = GST_FLOW_ERROR;
  }
}

/**
 * Create a new HTTP GET Request which will be a soup message.
 */
static gboolean
gst_dlna_soup_http_src_build_message (GstDlnaSoupHTTPSrc * src)
{
  GST_LOG_OBJECT (src, "called");

  src->msg = soup_message_new (SOUP_METHOD_GET, src->location);
  if (!src->msg) {
    //GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
    //    ("Error parsing URL."), ("URL: %s", src->location));
    return FALSE;
  }
  src->session_io_status = GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE;
  GST_INFO_OBJECT (src, "STATE: IDLE");
  soup_message_headers_append (src->msg->request_headers, "Connection",
      "close");
  #ifdef GSTREAMER_010
  if (src->iradio_mode) {
  #endif
    soup_message_headers_append (src->msg->request_headers, "icy-metadata",
        "1");
  #ifdef GSTREAMER_010
  }
#endif
  if (src->cookies) {
    gchar **cookie;

    for (cookie = src->cookies; *cookie != NULL; cookie++) {
      soup_message_headers_append (src->msg->request_headers, "Cookie",
          *cookie);
    }
  }

  // *TODO* - don't we want this for 1.0?  Need better logic here
  #ifdef GSTREAMER_010
  soup_message_headers_append (src->msg->request_headers,
      "transferMode.dlna.org", "Streaming");
  #endif

  src->retry = FALSE;

  // Setup callbacks
  g_signal_connect (src->msg, "got_headers",
      G_CALLBACK (gst_dlna_soup_http_src_got_headers_cb), src);
  g_signal_connect (src->msg, "got_body",
      G_CALLBACK (gst_dlna_soup_http_src_got_body_cb), src);
  g_signal_connect (src->msg, "finished",
      G_CALLBACK (gst_dlna_soup_http_src_finished_cb), src);
  g_signal_connect (src->msg, "got_chunk",
      G_CALLBACK (gst_dlna_soup_http_src_got_chunk_cb), src);

  // Setup soup message
  soup_message_set_flags (src->msg, SOUP_MESSAGE_OVERWRITE_CHUNKS |
      (src->automatic_redirect ? 0 : SOUP_MESSAGE_NO_REDIRECT));
  soup_message_set_chunk_allocator (src->msg,
      gst_dlna_soup_http_src_chunk_allocator, src, NULL);

  // DLNA server's don't like range header with playspeed
  if (0)
  {
	  gst_dlna_soup_http_src_add_range_header (src, src->request_position);
  }

  gst_dlna_soup_http_src_add_extra_headers (src);

  GST_DEBUG_OBJECT (src, "request headers:");
  soup_message_headers_foreach (src->msg->request_headers,
      gst_dlna_soup_http_src_headers_foreach, src);

  return TRUE;
}

static GstFlowReturn
gst_dlna_soup_http_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
	GstDlnaSoupHTTPSrc *src;

	src = GST_DLNA_SOUP_HTTP_SRC (psrc);

	// See if change has been requested
	if (src->msg &&
	   ((src->request_position != src->read_position) ||	// position change requested
	    (src->current_rate != src->request_rate)))			// rate change requested
	{
		GST_INFO_OBJECT (src, "Got msg and position/rate change");

		// *TODO* - need to verify position change is valid
		//if (src->content_size != 0 && src->request_position >= src->content_size)
		if (src->content_size == 0)
		{
			GST_WARNING_OBJECT (src, "Seeking behind the end of file -- EOS");
#ifdef GSTREAMER_010
			return GST_FLOW_UNEXPECTED;
#else
			return GST_FLOW_EOS;
#endif
		}
		// If session hasn't started yet, just update headers
		else if (src->session_io_status ==
				GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE)
		{
			// *TODO* - should we let dlnasrc update headers as appropriate?  Or do it here also?
			// This is redundant and doesn't address rate
			GST_DEBUG_OBJECT (src, "No active session, just add range header");

			// *TODO* - do we really need to add range header here?
			if (0)
			{
				gst_dlna_soup_http_src_add_range_header (src, src->request_position);
			}
		}
		else
		{
			// *TODO* - this is all that really should be done here
			GST_DEBUG_OBJECT (src, "Seek from position %" G_GUINT64_FORMAT
					" to %" G_GUINT64_FORMAT ": requeueing connection request",
					src->read_position, src->request_position);

			// Set status to idle and msg to NULL
			gst_dlna_soup_http_src_cancel_message (src);

			// Set current rate to requested rate
			src->current_rate = src->request_rate;
		}
	}

	// If message is NULL, create a new message request
	if (!src->msg)
	{
		GST_INFO_OBJECT (src, "No message, building message");
		if (!gst_dlna_soup_http_src_build_message (src))
		{
			GST_ERROR_OBJECT (src, "Problems building message");
			return GST_FLOW_ERROR;
		}
	}

	// ??? what r we doing here?
	src->ret = GST_FLOW_CUSTOM_ERROR;
	src->outbuf = outbuf;
	do
	{
		if (src->interrupted)
		{
			GST_DEBUG_OBJECT (src,
			  "interrupted, leaving ret = custom error to continue in loop");
			break;
		}
		if (src->retry)
		{
			GST_DEBUG_OBJECT (src, "Reconnecting");
			// Build another message
			// *TODO* - have we cleaned up properly from last msg?
			if (!gst_dlna_soup_http_src_build_message (src))
			{
				GST_ERROR_OBJECT (src,
					"Returning error since unable to create new message request for retry");
				return GST_FLOW_ERROR;
			}
			src->retry = FALSE;
			continue;
		}

		if (!src->msg)
		{
			// *TODO* - why does this indicate EOS?
			GST_DEBUG_OBJECT (src, "EOS reached");
			break;
		}

		switch (src->session_io_status)
		{
		case GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE:
			GST_DEBUG_OBJECT (src, "Queueing connection request");
			gst_dlna_soup_http_src_queue_message (src);
			break;
		case GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_QUEUED:
			GST_DEBUG_OBJECT (src, "IO Status queued");
			break;
		case GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING:
			// Normal state when creating buffers
			GST_LOG_OBJECT (src, "Running");
			gst_dlna_soup_http_src_session_unpause_message (src);
			break;
		case GST_DLNA_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED:
			/* Impossible. */
			break;
		}

		// *TODO* - what does this do??? starting up loop here?
		if (src->ret == GST_FLOW_CUSTOM_ERROR)
			g_main_loop_run (src->loop);

	} while (src->ret == GST_FLOW_CUSTOM_ERROR);

	// Got out of above loop with custom error, either interrupted or EOS
	if (src->ret == GST_FLOW_CUSTOM_ERROR)
	{
		GST_DEBUG_OBJECT (src, "Returning due to interrupted or EOS");
#ifdef GSTREAMER_010
		src->ret = GST_FLOW_UNEXPECTED;
#else
		src->ret = GST_FLOW_EOS;
#endif
	}
	return src->ret;
}

static gboolean
gst_dlna_soup_http_src_start (GstBaseSrc * bsrc)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (bsrc);

  GST_INFO_OBJECT (src, "start(\"%s\")", src->location);

  if (!src->location) {
    //GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (("No URL set.")),
    //    ("Missing location property"));
    return FALSE;
  }

  src->context = g_main_context_new ();

  src->loop = g_main_loop_new (src->context, TRUE);
  if (!src->loop) {
    //GST_ELEMENT_ERROR (src, LIBRARY, INIT,
    //    (NULL), ("Failed to start GMainLoop"));
    g_main_context_unref (src->context);
    return FALSE;
  }

  if (src->proxy == NULL) {
    src->session =
        soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
        src->context, SOUP_SESSION_USER_AGENT, src->user_agent,
        SOUP_SESSION_TIMEOUT, src->timeout,
#ifdef HAVE_LIBSOUP_GNOME
        SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_PROXY_RESOLVER_GNOME,
#endif
        NULL);
  } else {
    src->session =
        soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
        src->context, SOUP_SESSION_PROXY_URI, src->proxy,
        SOUP_SESSION_TIMEOUT, src->timeout,
        SOUP_SESSION_USER_AGENT, src->user_agent, NULL);
  }

  if (!src->session) {
    //GST_ELEMENT_ERROR (src, LIBRARY, INIT,
    //    (NULL), ("Failed to create async session"));
    return FALSE;
  }

  g_signal_connect (src->session, "authenticate",
      G_CALLBACK (gst_dlna_soup_http_src_authenticate_cb), src);
  return TRUE;
}

static gboolean
gst_dlna_soup_http_src_stop (GstBaseSrc * bsrc)
{
  GstDlnaSoupHTTPSrc *src;

  src = GST_DLNA_SOUP_HTTP_SRC (bsrc);
  GST_INFO_OBJECT (src, "called");

  gst_dlna_soup_http_src_session_close (src);
  if (src->loop) {
    g_main_loop_unref (src->loop);
    g_main_context_unref (src->context);
    src->loop = NULL;
    src->context = NULL;
  }
  if (src->extra_headers) {
    gst_structure_free (src->extra_headers);
    src->extra_headers = NULL;
  }

  gst_dlna_soup_http_src_reset (src);
  return TRUE;
}

/* Interrupt a blocking request. */
static gboolean
gst_dlna_soup_http_src_unlock (GstBaseSrc * bsrc)
{
  GstDlnaSoupHTTPSrc *src;

  src = GST_DLNA_SOUP_HTTP_SRC (bsrc);
  GST_LOG_OBJECT (src, "unlock()");

  src->interrupted = TRUE;
  if (src->loop)
    g_main_loop_quit (src->loop);
  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_dlna_soup_http_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstDlnaSoupHTTPSrc *src;

  src = GST_DLNA_SOUP_HTTP_SRC (bsrc);
  GST_LOG_OBJECT (src, "unlock_stop()");

  src->interrupted = FALSE;
  return TRUE;
}

static gboolean
gst_dlna_soup_http_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstDlnaSoupHTTPSrc *src;

  src = GST_DLNA_SOUP_HTTP_SRC (bsrc);

  if (src->have_size) {
    GST_INFO_OBJECT (src, "get_size() = %" G_GUINT64_FORMAT,
        src->content_size);
    *size = src->content_size;
    return TRUE;
  }
  GST_INFO_OBJECT (src, "get_size() = FALSE");
  return FALSE;
}

static gboolean
gst_dlna_soup_http_src_is_seekable (GstBaseSrc * bsrc)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (bsrc);
  GST_INFO_OBJECT (src, "seekable: %d", src->seekable);

  return src->seekable;
}

/**
 * According to GStreamer design doc, to support Server-side trick modes:
 *
 * - Need to reopen connection to server with new rate and positions
 * - Set rate in segment to 1 so client side trick mode is not enabled
 *  Update info in segment so downstream elements know what is required.
 *
 *  NOTE:
 * 		- Base src just updates segment->time
 * 		- Filesrc doesn't have a do_seek method so some other element must do
 * 		  something with the rate change???
 */
static gboolean
gst_dlna_soup_http_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (bsrc);

#ifdef GSTREAMER_010
  GST_INFO_OBJECT (src, "do_seek to (%" G_GUINT64_FORMAT ") using format: %s",
		  segment->start, gst_format_get_name(segment->format));
#else
  GST_DEBUG_OBJECT (src, "do_seek(%" G_GUINT64_FORMAT ")",
      segment->start);
#endif

  // Make sure this is seekable
  if (!src->seekable)
  {
	  GST_WARNING_OBJECT (src, "Not seekable");
	  return FALSE;
  }

  // Assuming dlnasrc verified request is valid so no verifications is needed here
  if (0)
  {
	  if ((src->read_position == segment->start) && (segment->format == GST_FORMAT_BYTES)) {
	    GST_INFO_OBJECT (src, "Seeking to current read position");
	    return TRUE;
	  }

	  /*
	  if (segment->rate < 0.0 || segment->format != GST_FORMAT_BYTES) {
	    GST_WARNING_OBJECT (src, "Invalid seek segment");
	    return FALSE;
	  }
	  */

	  if (src->content_size != 0 && segment->start >= src->content_size &&
			  segment->format == GST_FORMAT_BYTES) {
	    GST_WARNING_OBJECT (src, "Seeking behind end of file, will go to EOS soon");
	  }
	  // *TODO* - need something similar for time

	  /* Wait for create() to handle the jump in offset. */
	  if (segment->format == GST_FORMAT_BYTES)
	  {
		  src->request_position = segment->start;
	  }
	  else if (segment->format == GST_FORMAT_TIME)
	  {
		  // *TODO* - something with time
		  // Need to reopen new connection & issue new get request
		  // Set rate to 1.0 so client side trick mode is not enabled
		  src->request_position = segment->start;
	  }
  }

  // Current request will be canceled in _create() since rate change will be detected
  // *TODO* - is this really right?  Or should we be modifying state & let it cancel???
  if (src->current_rate != segment->rate)
  {
	  GST_INFO_OBJECT (src, "Rate change requested: %3.1f", segment->rate);
	  src->request_rate = segment->rate;
  }
  // *TODO* - add checks for time and byte positions, not just rate

  // New request will be issued in _create()

  // Set stop position to valid value
  if ((segment->stop != -1) && (segment->stop < segment->start))
  {
	  // This prevents initial 1x playback from starting
	  // *TODO* - need to fix this up
	  GST_WARNING_OBJECT (src, "Adjusting stop from %lld to %lld",
			  segment->stop, (segment->start + 1));
	  segment->stop = segment->start + 1;
  }
  // Set rate to 1.0 to prevent client-side trick mode processing
  if (segment->rate != 1.0)
  {
	  segment->applied_rate = src->request_rate;
 	  GST_INFO_OBJECT (src, "Set segment rate %3.1f back to 1.0 to inhibit client-side trick modes",
			  segment->rate);
	  segment->rate = 1.0;
 }

  GST_WARNING_OBJECT (src, "Performed actions necessary for seek");
  return TRUE;
}

gboolean
gst_dlna_soup_http_src_event(GstBaseSrc * src,
					   GstEvent  *event)
{
	GstDlnaSoupHTTPSrc *dsrc = GST_DLNA_SOUP_HTTP_SRC (src);
	gboolean ret = FALSE;

	switch (GST_EVENT_TYPE (event))
	{
	case GST_EVENT_SEEK:
		GST_INFO_OBJECT(dsrc, "Got src event: %s", GST_EVENT_TYPE_NAME(event));
		break;
	case GST_EVENT_NAVIGATION:
		GST_INFO_OBJECT(dsrc, "Got src event: %s", GST_EVENT_TYPE_NAME(event));
		break;
	default:
		break;
	}
	if (!ret)
	{
		ret = GST_BASE_SRC_CLASS (parent_class)->event (src, event);
	}
	return ret;
}

static gboolean
gst_dlna_soup_http_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
	GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (bsrc);
	gboolean ret = FALSE;
	GstFormat src_fmt, dest_fmt;
	gint64 src_val, dest_val;

	GstClockTime min, max;
    gboolean live;

	switch (GST_QUERY_TYPE (query))
	{
	case GST_QUERY_LATENCY:

		ret = gst_base_src_query_latency (bsrc, &live, &min, &max);

		// *TODO* - hard code to false for now
		live = FALSE;

		// Set min and max clock times

		GST_DEBUG_OBJECT (src, "report latency: live %d, min %" GST_TIME_FORMAT
				", max %" GST_TIME_FORMAT, live, GST_TIME_ARGS (min),
				GST_TIME_ARGS (max));

		gst_query_set_latency (query, live, min, max);
		break;

	case GST_QUERY_CONVERT:

		gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);

		// Print out info about conversion that has been requested
		GST_INFO_OBJECT(src, "Got conversion query: src fmt %s, dest fmt %s, src val %lld, dest val %lld",
				gst_format_get_name(src_fmt), gst_format_get_name(dest_fmt), src_val, dest_val);

		if (src_fmt == dest_fmt)
		{
			dest_val = src_val;
			GST_INFO_OBJECT(src, "Setting dest to src");
			ret = TRUE;
		}
		else
		{
			// Handle simple case
			if (src_val == 0)
			{
				dest_val = 1;
				GST_INFO_OBJECT(src, "Setting dest to zero");
				ret = TRUE;
			}
			else
			{
				// *TODO* - Figure out conversion here, do a head & get response
				GST_INFO_OBJECT(src, "Doing nothing");
				ret = FALSE;
			}
		}
		gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
		break;

	default:
		// Log queries to get an idea of what ends up here
		GST_INFO_OBJECT(src, "Src query: %s", GST_QUERY_TYPE_NAME(query));
		break;
	}

	// If not handled here, let base have a chance
	if (!ret)
	{
		ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
	}
	return ret;
}

/*
 * static gboolean
gst_videomixer_query_latency (GstVideoMixer * mix, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;

  res = TRUE;
  done = FALSE;
  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  // Take maximum of all latency values
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (mix));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);

        GstQuery *peerquery;

        GstClockTime min_cur, max_cur;

        gboolean live_cur;

        peerquery = gst_query_new_latency ();

         res &= gst_pad_peer_query (pad, peerquery);

        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    GST_DEBUG_OBJECT (mix, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}
 */

static gboolean
#ifdef GSTREAMER_010
gst_dlna_soup_http_src_set_location (GstDlnaSoupHTTPSrc * src, const gchar * uri)
#else
gst_dlna_soup_http_src_set_location (GstDlnaSoupHTTPSrc * src, const gchar * uri,
    GError ** error)
#endif
{
  GST_INFO_OBJECT (src, "called");
#ifndef GSTREAMER_010
  const char *alt_schemes[] = { "icy://", "icyx://" };
  guint i;
#endif

  if (src->location) {
    g_free (src->location);
    src->location = NULL;
  }
#ifndef GSTREAMER_010
  if (uri == NULL)
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (alt_schemes); i++) {
    if (g_str_has_prefix (uri, alt_schemes[i])) {
      src->location =
          g_strdup_printf ("http://%s", uri + strlen (alt_schemes[i]));
      return TRUE;
  }
  }
#endif
  src->location = g_strdup (uri);

  return TRUE;
}

static gboolean
gst_dlna_soup_http_src_set_proxy (GstDlnaSoupHTTPSrc * src, const gchar * uri)
{
  if (src->proxy) {
    soup_uri_free (src->proxy);
    src->proxy = NULL;
  }
  if (g_str_has_prefix (uri, "http://")) {
    src->proxy = soup_uri_new (uri);
  } else {
    gchar *new_uri = g_strconcat ("http://", uri, NULL);

    src->proxy = soup_uri_new (new_uri);
    g_free (new_uri);
  }

  return TRUE;
}

static guint
#ifdef GSTREAMER_010
gst_dlna_soup_http_src_uri_get_type (void)
#else
gst_dlna_soup_http_src_uri_get_type (GType type)
#endif
{
  return GST_URI_SRC;
}

#ifdef GSTREAMER_010
static gchar **
gst_dlna_soup_http_src_uri_get_protocols (void)
{
  static const gchar *protocols[] = { "http", "https", NULL };
  return (gchar **) protocols;
}
#else
static const gchar *const *
gst_dlna_soup_http_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "http", "https", "icy", "icyx", NULL };
  return protocols;
}
#endif

#ifdef GSTREAMER_010
static const gchar *
gst_dlna_soup_http_src_uri_get_uri (GstURIHandler * handler)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (handler);

  return src->location;
}
#else
static gchar *
gst_dlna_soup_http_src_uri_get_uri (GstURIHandler * handler)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->location);
}
#endif

#ifdef GSTREAMER_010
static gboolean
gst_dlna_soup_http_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (handler);

  return gst_dlna_soup_http_src_set_location (src, uri);
}
#else
static gboolean
gst_dlna_soup_http_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstDlnaSoupHTTPSrc *src = GST_DLNA_SOUP_HTTP_SRC (handler);

  return gst_dlna_soup_http_src_set_location (src, uri, error);
}
#endif

static void
gst_dlna_soup_http_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_dlna_soup_http_src_uri_get_type;
  iface->get_protocols = gst_dlna_soup_http_src_uri_get_protocols;
  iface->get_uri = gst_dlna_soup_http_src_uri_get_uri;
  iface->set_uri = gst_dlna_soup_http_src_uri_set_uri;
}

/*********************************************/
/**********                         **********/
/********** Code to support plugin  **********/
/**********                         **********/
/*********************************************/
#if 1

/* entry point to initialize the plug-in
 * initialize the plug-in itselfd
 * register the element factories and other features
 */
static gboolean
dlna_soup_http_src_init (GstPlugin * dlna_soup_http_src)
{
	GST_DEBUG_CATEGORY_INIT(gst_dlna_soup_http_src_debug, "dlnasouphttpsrc", 0, "MPEG+DLNA SOUP HTTP Player");

	return gst_element_register ((GstPlugin *)dlna_soup_http_src, "dlnasouphttpsrc",
				GST_RANK_NONE, GST_TYPE_DLNA_SOUP_HTTP_SRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dlnasouphttpsrc"
#endif

/* gstreamer looks for this structure to register eisss
 *
 * exchange the string 'Template eiss' with your eiss description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef GSTREAMER_010
    "dlnasouphttpsrc",
#else
    dlnasouphttpsrc,
#endif
    "MPEG+DLNA SOUP HTTP Source",
    (GstPluginInitFunc)dlna_soup_http_src_init,
    VERSION,
    "LGPL",
    "gst-cablelabs_ri",
    "http://gstreamer.net/"
)

#endif

