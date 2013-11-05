/* Copyright (C) 2013 Cable Television Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CABLE TELEVISION LABS INC. OR ITS
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <stdio.h>
#include <gst/gst.h>
#include <glib-object.h>
#include <libsoup/soup.h>

#include "gstdlnasrc.h"

enum
{
  PROP_0,
  PROP_URI,
  PROP_CL_NAME,
  PROP_SUPPORTED_RATES,
  PROP_DTCP_BLOCKSIZE,
};

#define DLNA_SRC_CL_NAME "dlnasrc"
#define DEFAULT_DTCP_BLOCKSIZE       524288

#define ELEMENT_NAME_SOUP_HTTP_SRC "soup-http-source"
#define ELEMENT_NAME_DTCP_DECRYPTER "dtcp-decrypter"

#define MAX_HTTP_BUF_SIZE 2048
static const gchar CRLF[] = "\r\n";

static const gchar COLON[] = ":";

static const gchar *HEAD_RESPONSE_HEADERS[] = {
  "HTTP/",                      /* 0 */
  "VARY",                       /* 1 */
  "TIMESEEKRANGE.DLNA.ORG",     /* 2 */
  "TRANSFERMODE.DLNA.ORG",      /* 3 */
  "DATE",                       /* 4 */
  "CONTENT-TYPE",               /* 5 */
  "SERVER",                     /* 6 */
  "TRANSFER-ENCODING",          /* 7 */
  "CONTENTFEATURES.DLNA.ORG",   /* 8 */
  "CONTENT-RANGE.DTCP.COM",     /* 9 */
  "PRAGMA",                     /* 10 */
  "CACHE-CONTROL",              /* 11 */
  "CONTENT-LENGTH",             /* 12 */
  "ACCEPT-RANGES",              /* 13 */
  "CONTENT-RANGE",              /* 14 */
  "AVAILABLESEEKRANGE.DLNA.ORG" /* 15 */
};

/* Constants which represent indices in HEAD_RESPONSE_HEADERS string array
  NOTE: Needs to stay in sync with HEAD_RESPONSE_HEADERS */
#define HEADER_INDEX_HTTP 0
#define HEADER_INDEX_VARY 1
#define HEADER_INDEX_TIMESEEKRANGE 2
#define HEADER_INDEX_TRANSFERMODE 3
#define HEADER_INDEX_DATE 4
#define HEADER_INDEX_CONTENT_TYPE 5
#define HEADER_INDEX_SERVER 6
#define HEADER_INDEX_TRANSFER_ENCODING 7
#define HEADER_INDEX_CONTENTFEATURES 8
#define HEADER_INDEX_DTCP_RANGE 9
#define HEADER_INDEX_PRAGMA 10
#define HEADER_INDEX_CACHE_CONTROL 11
#define HEADER_INDEX_CONTENT_LENGTH 12
#define HEADER_INDEX_ACCEPT_RANGES 13
#define HEADER_INDEX_CONTENT_RANGE 14
#define HEADER_INDEX_AVAILABLE_RANGE 15

/* Count in HEAD_RESPONSE_HEADERS and HEADER_INDEX_* constants */
static const gint HEAD_RESPONSE_HEADERS_CNT = 16;

/* Subfield headers which specify ranges */
static const gchar *RANGE_HEADERS[] = {
  "NPT",                        /* 0 */
  "BYTES",                      /* 1 */
  "CLEARTEXTBYTES",             /* 2 */
};

#define HEADER_INDEX_NPT 0
#define HEADER_INDEX_BYTES 1
#define HEADER_INDEX_CLEAR_TEXT 2

/* Subfield headers within ACCEPT-RANGES */
static const gchar *ACCEPT_RANGES_BYTES = "BYTES";

/* Subfield headers within CONTENTFEATURES.DLNA.ORG */
static const gchar *CONTENT_FEATURES_HEADERS[] = {
  "DLNA.ORG_PN",                /* 0 */
  "DLNA.ORG_OP",                /* 1 */
  "DLNA.ORG_PS",                /* 2 */
  "DLNA.ORG_FLAGS",             /* 3 */
  "DLNA.ORG_CI",                /* 4 */
};

#define HEADER_INDEX_PN 0
#define HEADER_INDEX_OP 1
#define HEADER_INDEX_PS 2
#define HEADER_INDEX_FLAGS 3
#define HEADER_INDEX_CI 4

/* Subfield headers with CONTENT-TYPE */
static const gchar *CONTENT_TYPE_HEADERS[] = {
  "DTCP1HOST",                  /* 0 */
  "DTCP1PORT",                  /* 1 */
  "CONTENTFORMAT",              /* 2 */
  "APPLICATION/X-DTCP1"         /* 3 */
};

#define HEADER_INDEX_DTCP_HOST 0
#define HEADER_INDEX_DTCP_PORT 1
#define HEADER_INDEX_CONTENT_FORMAT 2
#define HEADER_INDEX_APP_DTCP 3


/**
 * DLNA Flag parameters defined in DLNA spec
 * primary flags - 8 hexadecimal digits representing 32 binary flags
 * protocol info dlna org flags represented by primary flags followed
 * by reserved data of 24 hexadecimal digits (zeros)
 */
static const gint SP_FLAG = 1 << 31;    /* Sender Paced Flag - content src is clock  */
static const gint LOP_NPT = 1 << 30;    /* Limited Operations Flags: Time-Based Seek */
static const gint LOP_BYTES = 1 << 29;  /* Limited Operations Flags: Byte-Based Seek */
static const gint PLAYCONTAINER_PARAM = 1 << 28;        /* DLNA PlayContainer Flag */
static const gint S0_INCREASING = 1 << 27;      /* UCDAM s0 Increasing Flag - content has no fixed beginning */
static const gint SN_INCREASING = 1 << 26;      /* UCDAM sN Increasing Flag - content has no fixed ending */
static const gint RTSP_PAUSE = 1 << 25; /* Pause media operation support for RTP Serving Endpoints */
static const gint TM_S = 1 << 24;       /* Streaming Mode Flag - av content must have this set */
static const gint TM_I = 1 << 23;       /* Interactive Mode Flag */
static const gint TM_B = 1 << 22;       /* Background Mode Flag */
static const gint HTTP_STALLING = 1 << 21;      /* HTTP Connection Stalling Flag */
static const gint DLNA_V15_FLAG = 1 << 20;      /* DLNA v1.5 versioning flag */
static const gint LP_FLAG = 1 << 16;    /* Link Content Flag */
static const gint CLEARTEXTBYTESEEK_FULL_FLAG = 1 << 15;        /* Support for Full RADA ClearTextByteSeek header */
static const gint LOP_CLEARTEXTBYTES = 1 << 14; /* Support for Limited RADA ClearTextByteSeek header */

static const int RESERVED_FLAGS_LENGTH = 24;

#define HTTP_STATUS_OK 200
#define HTTP_STATUS_CREATED 201
#define HTTP_STATUS_PARTIAL 206
#define HTTP_STATUS_BAD_REQUEST 400
#define HTTP_STATUS_NOT_ACCEPTABLE 406

#define HEADER_GET_CONTENT_FEATURES_TITLE "getcontentFeatures.dlna.org"
#define HEADER_GET_CONTENT_FEATURES_VALUE "1"

#define HEADER_GET_AVAILABLE_SEEK_RANGE_TITLE "getAvailableSeekRange.dlna.org"
#define HEADER_GET_AVAILABLE_SEEK_RANGE_VALUE "1"

#define HEADER_RANGE_BYTES_TITLE "Range"
#define HEADER_RANGE_BYTES_VALUE "bytes=0-"

#define HEADER_DTCP_RANGE_BYTES_TITLE "Range.dtcp.com"
#define HEADER_DTCP_RANGE_BYTES_VALUE "bytes=0-"

#define HEADER_TIME_SEEK_RANGE_TITLE "TimeSeekRange.dlna.org"
#define HEADER_TIME_SEEK_RANGE_VALUE "npt=0-"

static GstStaticPadTemplate gst_dlna_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static void gst_dlna_src_dispose (GObject * object);

static void gst_dlna_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);

static void gst_dlna_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static gboolean gst_dlna_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_dlna_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstStateChangeReturn gst_dlna_src_change_state (GstElement * element,
    GstStateChange transition);

static void gst_dlna_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static gboolean dlna_src_uri_assign (GstDlnaSrc * dlna_src, const gchar * uri,
    GError ** error);

static gboolean dlna_src_uri_init (GstDlnaSrc * dlna_src);

static gboolean dlna_src_uri_gather_info (GstDlnaSrc * dlna_src);

static gboolean dlna_src_setup_bin (GstDlnaSrc * dlna_src);

static gboolean dlna_src_setup_dtcp (GstDlnaSrc * dlna_src);

static gboolean dlna_src_soup_session_open (GstDlnaSrc * dlna_src);
static void dlna_src_soup_session_close (GstDlnaSrc * dlna_src);
static void dlna_src_soup_log_msg (GstDlnaSrc * dlna_src);
static gboolean
dlna_src_soup_issue_head (GstDlnaSrc * dlna_src, gsize header_array_size,
    gchar * headers[][2], GstDlnaSrcHeadResponse * head_response,
    gboolean do_update_overall_info);

static void dlna_src_head_response_free (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response);

static gboolean dlna_src_head_response_parse (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response);

static gint dlna_src_head_response_get_field_idx (GstDlnaSrc * dlna_src,
    const gchar * field_str);

static gboolean dlna_src_head_response_assign_field_value (GstDlnaSrc *
    dlna_src, GstDlnaSrcHeadResponse * head_response, gint idx,
    const gchar * field_value);

static gboolean dlna_src_head_response_parse_time_seek (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str);

static gboolean
dlna_src_head_response_parse_available_range (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str);

static gboolean dlna_src_head_response_parse_content_features (GstDlnaSrc *
    dlna_src, GstDlnaSrcHeadResponse * head_response, gint idx,
    const gchar * field_str);

static gboolean dlna_src_head_response_parse_profile (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str);

static gboolean dlna_src_head_response_parse_operations (GstDlnaSrc *
    dlna_src, GstDlnaSrcHeadResponse * head_response, gint idx,
    const gchar * field_str);

static gboolean dlna_src_head_response_parse_playspeeds (GstDlnaSrc *
    dlna_src, GstDlnaSrcHeadResponse * head_response, gint idx,
    const gchar * field_str);

static gboolean dlna_src_head_response_parse_flags (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str);

static gboolean
dlna_src_head_response_parse_conversion_indicator (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str);

static gboolean dlna_src_head_response_parse_content_type (GstDlnaSrc *
    dlna_src, GstDlnaSrcHeadResponse * head_response, gint idx,
    const gchar * field_str);

static gboolean dlna_src_head_response_is_flag_set (GstDlnaSrc * dlna_src,
    const gchar * flags_str, gint flag);

static gboolean dlna_src_update_overall_info (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response);

static gboolean dlna_src_head_response_init_struct (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse ** head_response);

static void dlna_src_head_response_struct_to_str (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, GString * struct_str);

static void dlna_src_struct_append_header_value_str (GString * struct_str,
    gchar * title, gchar * value);

static void dlna_src_struct_append_header_value_bool (GString * struct_str,
    gchar * title, gboolean value);

static void dlna_src_struct_append_header_value_guint (GString * struct_str,
    gchar * title, guint value);

static void dlna_src_struct_append_header_value_guint64 (GString * struct_str,
    gchar * title, guint64 value);

static void dlna_src_struct_append_header_value_str_guint64 (GString *
    struct_str, gchar * title, gchar * value_str, guint64 value);

static gboolean dlna_src_handle_event_seek (GstDlnaSrc * dlna_src,
    GstPad * pad, GstEvent * event);

static gboolean dlna_src_handle_query_duration (GstDlnaSrc * dlna_src,
    GstQuery * query);

static gboolean dlna_src_handle_query_seeking (GstDlnaSrc * dlna_src,
    GstQuery * query);

static gboolean dlna_src_handle_query_segment (GstDlnaSrc * dlna_src,
    GstQuery * query);

static gboolean dlna_src_handle_query_convert (GstDlnaSrc * dlna_src,
    GstQuery * query);

static gboolean dlna_src_parse_byte_range (GstDlnaSrc * dlna_src,
    const gchar * field_str, gint header_idx, guint64 * start_byte,
    guint64 * end_byte, guint64 * total_bytes);

static gboolean
dlna_src_parse_npt_range (GstDlnaSrc * dlna_src, const gchar * npt_str,
    gchar ** start_str, gchar ** stop_str, gchar ** total_str,
    guint64 * start, guint64 * stop, guint64 * total);

static gboolean dlna_src_is_change_valid (GstDlnaSrc * dlna_src, gfloat rate,
    GstFormat format, guint64 start,
    GstSeekType start_type, guint64 stop, GstSeekType stop_type);

static gboolean dlna_src_is_rate_supported (GstDlnaSrc * dlna_src, gfloat rate);

static gboolean dlna_src_adjust_http_src_headers (GstDlnaSrc * dlna_src,
    gfloat rate, GstFormat format, guint64 start, guint64 stop,
    guint32 new_seqnum);

static gboolean dlna_src_npt_to_nanos (GstDlnaSrc * dlna_src, gchar * string,
    guint64 * media_time_nanos);

static gboolean
dlna_src_convert_bytes_to_npt_nanos (GstDlnaSrc * dlna_src, guint64 bytes,
    guint64 * npt_nanos);

static gboolean
dlna_src_convert_npt_nanos_to_bytes (GstDlnaSrc * dlna_src, guint64 npt_nanos,
    guint64 * bytes);

static void
dlna_src_nanos_to_npt (GstDlnaSrc * dlna_src, guint64 npt_nanos,
    GString * npt_str);

#define gst_dlna_src_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (GstDlnaSrc, gst_dlna_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_dlna_src_uri_handler_init));

void
gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data);

GST_DEBUG_CATEGORY_STATIC (gst_dlna_src_debug);
#define GST_CAT_DEFAULT gst_dlna_src_debug

/*
 * Initializes (only called once) the class associated with this element from within
 * gstreamer framework.  Installs properties and assigns specific
 * methods for function pointers.  Also defines detailed info
 * associated with this element.  The purpose of the *_class_init
 * method is to register the plugin with the GObject system.
 *
 * @param	klass	class representation of this element
 */
static void
gst_dlna_src_class_init (GstDlnaSrcClass * klass)
{
  GObjectClass *gobject_klass;

  gobject_klass = (GObjectClass *) klass;

  GstElementClass *gstelement_klass;
  gstelement_klass = (GstElementClass *) klass;
  gst_element_class_set_static_metadata (gstelement_klass,
      "HTTP/DLNA client source 2/20/13 7:37 AM",
      "Source/Network",
      "Receive data as a client via HTTP with DLNA extensions",
      "Eric Winkelman <e.winkelman@cablelabs.com>");

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&gst_dlna_src_pad_template));

  gobject_klass->set_property = gst_dlna_src_set_property;
  gobject_klass->get_property = gst_dlna_src_get_property;

  g_object_class_install_property (gobject_klass, PROP_URI,
      g_param_spec_string ("uri", "Stream URI",
          "Sets URI A/V stream", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_klass, PROP_CL_NAME,
      g_param_spec_string ("cl_name",
          "CableLabs name",
          "CableLabs name used to verify playbin selected source",
          NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_SUPPORTED_RATES,
      g_param_spec_boxed ("supported_rates",
          "Supported Playspeed rates",
          "List of supported playspeed rates of DLNA server content",
          G_TYPE_ARRAY, G_PARAM_READABLE));

  g_object_class_install_property (gobject_klass, PROP_DTCP_BLOCKSIZE,
      g_param_spec_uint ("dtcp_blocksize", "DTCP Block size",
          "Size in bytes to read per buffer when content is dtcp encrypted (-1 = default)",
          0, G_MAXUINT, DEFAULT_DTCP_BLOCKSIZE, G_PARAM_READWRITE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_dlna_src_dispose);
  gstelement_klass->change_state = gst_dlna_src_change_state;
}

/*
 * Initializes a specific instance of this element, called when object
 * is created from within gstreamer framework.
 *
 * @param dlna_src	specific instance of element to intialize
 */
static void
gst_dlna_src_init (GstDlnaSrc * dlna_src)
{
  GST_INFO_OBJECT (dlna_src, "Initializing");

  dlna_src->http_src = NULL;
  dlna_src->dtcp_decrypter = NULL;
  dlna_src->src_pad = NULL;

  dlna_src->cl_name = g_strdup (DLNA_SRC_CL_NAME);
  dlna_src->dtcp_blocksize = DEFAULT_DTCP_BLOCKSIZE;
  dlna_src->src_pad = NULL;
  dlna_src->dtcp_key_storage = NULL;
  dlna_src->uri = NULL;
  dlna_src->soup_session = NULL;
  dlna_src->soup_msg = NULL;

  dlna_src->is_uri_initialized = FALSE;
  dlna_src->is_live = FALSE;
  dlna_src->is_encrypted = FALSE;

  dlna_src->byte_seek_supported = FALSE;
  dlna_src->byte_start = 0;
  dlna_src->byte_end = 0;
  dlna_src->byte_total = 0;

  dlna_src->time_seek_supported = FALSE;
  dlna_src->npt_start_nanos = 0;
  dlna_src->npt_end_nanos = 0;
  dlna_src->npt_duration_nanos = 0;
  dlna_src->npt_start_str = NULL;
  dlna_src->npt_end_str = NULL;
  dlna_src->npt_duration_str = NULL;

  dlna_src->rate = 1.0;
  dlna_src->requested_rate = 1.0;
  dlna_src->requested_format = GST_FORMAT_BYTES;
  dlna_src->requested_start = 0;
  dlna_src->requested_stop = -1;

  dlna_src->handled_time_seek_seqnum = FALSE;
  dlna_src->time_seek_event_start = 0;
  dlna_src->time_seek_seqnum = 0;

  GST_LOG_OBJECT (dlna_src, "Initialization complete");
}

/**
 * Called by framework when tearing down pipeline
 *
 * @param object  element to destroy
 */
static void
gst_dlna_src_dispose (GObject * object)
{
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (object);

  GST_INFO_OBJECT (dlna_src, " Disposing the dlna src");

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * Method called by framework to set this element's properties
 *
 * @param	object	set property of this element
 * @param	prop_id	identifier of property to set
 * @param	value	set property to this value
 * @param	pspec	description of property type
 */
static void
gst_dlna_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (object);

  GST_INFO_OBJECT (dlna_src, "Setting property: %d", prop_id);

  switch (prop_id) {

    case PROP_URI:
    {
      if (!dlna_src_uri_assign (dlna_src, g_value_get_string (value), NULL)) {
        GST_ELEMENT_ERROR (dlna_src, RESOURCE, READ,
            ("%s() - unable to set URI: %s",
                __FUNCTION__, g_value_get_string (value)), NULL);
      }
      break;
    }
    case PROP_DTCP_BLOCKSIZE:
      dlna_src->dtcp_blocksize = g_value_get_uint (value);
      GST_INFO_OBJECT (dlna_src, "Set DTCP blocksize: %d",
          dlna_src->dtcp_blocksize);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * Retrieves current value of property associated with supplied element instance.
 *
 * @param	object	get property value of this element
 * @param	prop_id	get property identified by this supplied id
 * @param	value	returned current value of property
 * @param	pspec	description of property type
 */
static void
gst_dlna_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (object);

  int i = 0;
  GArray *garray = NULL;
  gfloat rate = 0;
  int psCnt = 0;

  switch (prop_id) {

    case PROP_URI:
      GST_LOG_OBJECT (dlna_src, "Getting property: uri");
      if (dlna_src->uri != NULL) {
        g_value_set_string (value, dlna_src->uri);
        GST_LOG_OBJECT (dlna_src, "Get property returning: %s",
            g_value_get_string (value));
      }
      break;

    case PROP_CL_NAME:
      GST_LOG_OBJECT (dlna_src,
          "Getting property: CableLab's assigned src name");
      if (dlna_src->cl_name != NULL) {
        g_value_set_string (value, dlna_src->cl_name);
      }
      break;

    case PROP_SUPPORTED_RATES:
      GST_INFO_OBJECT (dlna_src,
          "Supported rates info not available, make sure URI is initialized");
      if (!dlna_src_uri_init (dlna_src))
        GST_ERROR_OBJECT (dlna_src, "Problems initializing URI");

      GST_LOG_OBJECT (dlna_src, "Getting property: supported rates");
      if ((dlna_src->server_info != NULL) &&
          (dlna_src->server_info->content_features != NULL) &&
          (dlna_src->server_info->content_features->playspeeds_cnt > 0)) {
        psCnt = dlna_src->server_info->content_features->playspeeds_cnt;
        garray = g_array_sized_new (TRUE, TRUE, sizeof (gfloat), psCnt);
        for (i = 0; i < psCnt; i++) {
          rate = dlna_src->server_info->content_features->playspeeds[i];
          g_array_append_val (garray, rate);
          GST_LOG_OBJECT (dlna_src, "Rate %d: %f", (i + 1),
              g_array_index (garray, gfloat, i));
        }

        memset (value, 0, sizeof (*value));
        g_value_init (value, G_TYPE_ARRAY);
        g_value_take_boxed (value, garray);
      }
      break;

    case PROP_DTCP_BLOCKSIZE:
      g_value_set_uint (value, dlna_src->dtcp_blocksize);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static GstStateChangeReturn
gst_dlna_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (element);

  GST_INFO_OBJECT (dlna_src, "Changing state from %s to %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INFO_OBJECT (dlna_src, "Make sure URI is initialized");
      if (!dlna_src_uri_init (dlna_src)) {
        GST_ERROR_OBJECT (dlna_src, "Problems initializing URI");
        return ret;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (dlna_src, "Problems with parent class state change");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dlna_src_soup_session_close (dlna_src);

      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* De-allocate non-stream-specific resources (libs, mem) */
      break;
    default:
      break;
  }

  return ret;
}

/**
 * Processes the supplied event
 *
 * @param pad	    pad where event was received
 * @param parent    parent of pad
 * @param event	    received event
 *
 * @return	true if event was handled, false otherwise
 */
static gboolean
gst_dlna_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_INFO_OBJECT (dlna_src, "Got src event: %s",
          GST_EVENT_TYPE_NAME (event));
      ret = dlna_src_handle_event_seek (dlna_src, pad, event);
      break;

    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (dlna_src, "Got src event: %s",
          GST_EVENT_TYPE_NAME (event));
      break;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (dlna_src, "Got src event: %s",
          GST_EVENT_TYPE_NAME (event));
      break;

    case GST_EVENT_QOS:
    case GST_EVENT_LATENCY:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_RECONFIGURE:
      /* Just call default handler to handle */
      break;

    default:
      GST_DEBUG_OBJECT (dlna_src, "Unsupported event: %s",
          GST_EVENT_TYPE_NAME (event));
      break;
  }

  /* If not handled, pass on to default pad handler */
  if (!ret) {
    ret = gst_pad_event_default (pad, parent, event);
  }

  return ret;
}

/**
 * Handles queries on the pad
 *
 * @param pad       pad where query was received
 * @param parent    parent of pad
 * @param event     received query
 *
 * @return true if query could be performed, false otherwise
 */
static gboolean
gst_dlna_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dlna_src, "Got src query: %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      ret = dlna_src_handle_query_duration (dlna_src, query);
      break;

    case GST_QUERY_SEEKING:
      ret = dlna_src_handle_query_seeking (dlna_src, query);
      break;

    case GST_QUERY_SEGMENT:
      ret = dlna_src_handle_query_segment (dlna_src, query);
      break;

    case GST_QUERY_CONVERT:
      ret = dlna_src_handle_query_convert (dlna_src, query);
      break;

    case GST_QUERY_URI:
      GST_INFO_OBJECT (dlna_src, "query uri");
      gst_query_set_uri (query, dlna_src->uri);
      ret = TRUE;
      break;

    case GST_QUERY_FORMATS:
      GST_INFO_OBJECT (dlna_src, "format query");
      gst_query_set_formats (query, 2, GST_FORMAT_BYTES, GST_FORMAT_TIME);
      ret = TRUE;
      break;

    case GST_QUERY_LATENCY:
      /* Don't know latency, let some other element handle this */
      break;

    case GST_QUERY_POSITION:
      /* Don't know current position in stream, let some other element handle this */
      break;

    default:
      GST_LOG_OBJECT (dlna_src,
          "Got unsupported src query: %s, passing to default handler",
          GST_QUERY_TYPE_NAME (query));
      break;
  }

  if (!ret) {
    ret = gst_pad_query_default (pad, parent, query);
  }

  return ret;
}

/**
 * Responds to a duration query by returning the size of content/stream
 *
 * @param	dlna_src	this element
 * @param	query		received duration query to respond to
 *
 * @return	true if responded to query, false otherwise
 */
static gboolean
dlna_src_handle_query_duration (GstDlnaSrc * dlna_src, GstQuery * query)
{
  gboolean ret = FALSE;
  gint64 duration = 0;
  GstFormat format;

  GST_LOG_OBJECT (dlna_src, "Called");

  if ((dlna_src->uri == NULL) || (dlna_src->server_info == NULL)) {
    GST_INFO_OBJECT (dlna_src,
        "No URI and/or HEAD response info, unable to handle query");
    return FALSE;
  }

  gst_query_parse_duration (query, &format, &duration);

  if (format == GST_FORMAT_BYTES) {
    if (dlna_src->byte_total) {
      gst_query_set_duration (query, GST_FORMAT_BYTES, dlna_src->byte_total);
      ret = TRUE;
      GST_DEBUG_OBJECT (dlna_src,
          "Duration in bytes for this content on the server: %"
          G_GUINT64_FORMAT, dlna_src->byte_total);
    } else
      GST_DEBUG_OBJECT (dlna_src,
          "Duration in bytes not available for content item");
  } else if (format == GST_FORMAT_TIME) {
    if (dlna_src->npt_duration_nanos) {
      gst_query_set_duration (query, GST_FORMAT_TIME,
          dlna_src->npt_duration_nanos);
      ret = TRUE;
      GST_DEBUG_OBJECT (dlna_src,
          "Duration in media time for this content on the server, npt: %"
          GST_TIME_FORMAT ", nanosecs: %" G_GUINT64_FORMAT,
          GST_TIME_ARGS (dlna_src->npt_duration_nanos),
          dlna_src->npt_duration_nanos);
    } else
      GST_DEBUG_OBJECT (dlna_src,
          "Duration in media time not available for content item");
  } else {
    GST_DEBUG_OBJECT (dlna_src,
        "Got duration query with non-supported format type: %s, passing to default handler",
        gst_format_get_name (format));
  }
  return ret;
}

/**
 * Responds to a seeking query by returning whether or not seeking is supported
 *
 * @param	dlna_src	this element
 * @param	query		received seeking query to respond to
 *
 * @return	true if responded to query, false otherwise
 */
static gboolean
dlna_src_handle_query_seeking (GstDlnaSrc * dlna_src, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFormat format;
  gboolean supports_seeking = FALSE;
  gint64 seek_start = 0;
  gint64 seek_end = 0;

  GST_DEBUG_OBJECT (dlna_src, "Called");

  if ((dlna_src->uri == NULL) || (dlna_src->server_info == NULL)) {
    GST_INFO_OBJECT (dlna_src,
        "No URI and/or HEAD response info, unable to handle query");
    return FALSE;
  }

  gst_query_parse_seeking (query, &format, &supports_seeking, &seek_start,
      &seek_end);

  if (format == GST_FORMAT_BYTES) {
    if (dlna_src->byte_seek_supported) {
      gst_query_set_seeking (query, GST_FORMAT_BYTES, TRUE,
          dlna_src->byte_start, dlna_src->byte_end);
      ret = TRUE;

      GST_INFO_OBJECT (dlna_src,
          "Byte seeks supported for this content by the server, start %"
          G_GUINT64_FORMAT ", end %" G_GUINT64_FORMAT,
          dlna_src->byte_start, dlna_src->byte_end);

    } else
      GST_INFO_OBJECT (dlna_src,
          "Seeking in bytes not available for content item");
  } else if (format == GST_FORMAT_TIME) {
    if (dlna_src->time_seek_supported) {
      gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE,
          dlna_src->npt_start_nanos, dlna_src->npt_end_nanos);
      ret = TRUE;

      GST_DEBUG_OBJECT (dlna_src,
          "Time based seeks supported for this content by the server, start %"
          GST_TIME_FORMAT ", end %" GST_TIME_FORMAT,
          GST_TIME_ARGS (dlna_src->npt_start_nanos),
          GST_TIME_ARGS (dlna_src->npt_end_nanos));
    } else
      GST_DEBUG_OBJECT (dlna_src,
          "Seeking in media time not available for content item");
  } else {
    GST_DEBUG_OBJECT (dlna_src,
        "Got seeking query with non-supported format type: %s, passing to default handler",
        GST_QUERY_TYPE_NAME (query));
  }

  return ret;
}

/**
 * Responds to a segment query by returning rate along with start and stop
 *
 * @param	dlna_src	this element
 * @param	query		received segment query to respond to
 *
 * @return	true if responded to query, false otherwise
 */
static gboolean
dlna_src_handle_query_segment (GstDlnaSrc * dlna_src, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFormat format;
  gdouble rate = 1.0;
  gint64 start = 0;
  gint64 end = 0;

  GST_LOG_OBJECT (dlna_src, "Called");

  if ((dlna_src->uri == NULL) || (dlna_src->server_info == NULL)) {
    GST_INFO_OBJECT (dlna_src,
        "No URI and/or HEAD response info, unable to handle query");
    return FALSE;
  }
  gst_query_parse_segment (query, &rate, &format, &start, &end);

  if (format == GST_FORMAT_BYTES) {
    if (dlna_src->byte_seek_supported) {

      gst_query_set_segment (query, dlna_src->rate, GST_FORMAT_BYTES,
          dlna_src->byte_start, dlna_src->byte_end);
      ret = TRUE;

      GST_DEBUG_OBJECT (dlna_src,
          "Segment info in bytes for this content, rate %f, start %"
          G_GUINT64_FORMAT ", end %" G_GUINT64_FORMAT,
          dlna_src->rate, dlna_src->byte_start, dlna_src->byte_end);
    } else
      GST_DEBUG_OBJECT (dlna_src,
          "Segment info in bytes not available for content item");
  } else if (format == GST_FORMAT_TIME) {

    if (dlna_src->time_seek_supported) {
      gst_query_set_segment (query, dlna_src->rate, GST_FORMAT_TIME,
          dlna_src->npt_start_nanos, dlna_src->npt_end_nanos);
      ret = TRUE;

      GST_DEBUG_OBJECT (dlna_src,
          "Time based segment info for this content by the server, rate %f, start %"
          GST_TIME_FORMAT ", end %" GST_TIME_FORMAT,
          dlna_src->rate,
          GST_TIME_ARGS (dlna_src->npt_start_nanos),
          GST_TIME_ARGS (dlna_src->npt_end_nanos));
    } else
      GST_DEBUG_OBJECT (dlna_src,
          "Segment info in media time not available for content item");
  } else {
    GST_DEBUG_OBJECT (dlna_src,
        "Got segment query with non-supported format type: %s, passing to default handler",
        GST_QUERY_TYPE_NAME (query));
  }

  return ret;
}

/**
 * Responds to a convert query by issuing head and returning conversion value
 *
 * @param	dlna_src	this element
 * @param	query		received query to respond to
 *
 * @return	true if responded to query, false otherwise
 */
static gboolean
dlna_src_handle_query_convert (GstDlnaSrc * dlna_src, GstQuery * query)
{
  /* Always return true since no other element can do this */
  gboolean ret = TRUE;
  GstFormat src_fmt, dest_fmt;
  gint64 src_val, dest_val;
  gint64 start_byte = 0;
  gint64 start_npt = 0;

  GST_LOG_OBJECT (dlna_src, "Called");

  if (dlna_src->uri == NULL || !dlna_src->time_seek_supported) {
    GST_INFO_OBJECT (dlna_src, "Not enough info to handle conversion query");
    return FALSE;
  }

  gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);

  GST_DEBUG_OBJECT (dlna_src,
      "Got conversion query: src fmt: %s, dest fmt: %s, src val: %"
      G_GUINT64_FORMAT ", dest: val %" G_GUINT64_FORMAT,
      gst_format_get_name (src_fmt),
      gst_format_get_name (dest_fmt), src_val, dest_val);

  if (src_fmt == GST_FORMAT_BYTES) {
    start_byte = src_val;
  } else if (src_fmt == GST_FORMAT_TIME) {
    start_npt = src_val;
  } else {
    GST_WARNING_OBJECT (dlna_src,
        "Got segment query with non-supported format type: %s",
        GST_QUERY_TYPE_NAME (query));
    return ret;
  }

  if (dest_fmt == GST_FORMAT_TIME) {
    if (!dlna_src_convert_bytes_to_npt_nanos (dlna_src, start_byte,
            (guint64 *) & dest_val)) {
      GST_WARNING_OBJECT (dlna_src, "Problems converting to time");
      return ret;
    }
  } else if (dest_fmt == GST_FORMAT_BYTES) {
    if (!dlna_src_convert_npt_nanos_to_bytes (dlna_src, start_npt,
            (guint64 *) & dest_val)) {
      GST_WARNING_OBJECT (dlna_src, "Problems converting to bytes");
      return ret;
    }
  } else {
    GST_INFO_OBJECT (dlna_src, "Unsupported format: %s",
        gst_format_get_name (dest_fmt));
    return FALSE;
  }

  gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);

  return ret;
}


/**
 * Perform action necessary when seek event is received
 *
 * @param dlna_src		this element
 * @param seek_event	seek event which has been received
 *
 * @return	true if this event has been handled, false otherwise
 */
static gboolean
dlna_src_handle_event_seek (GstDlnaSrc * dlna_src, GstPad * pad,
    GstEvent * event)
{
  GST_LOG_OBJECT (dlna_src, "Handle seek event");

  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type;
  gint64 start;
  GstSeekType stop_type;
  gint64 stop;
  guint32 new_seqnum;
  gboolean convert_start = FALSE;

  if ((dlna_src->uri == NULL) || (dlna_src->server_info == NULL)) {
    GST_INFO_OBJECT (dlna_src,
        "No URI and/or HEAD response info, event handled");
    return TRUE;
  }

  new_seqnum = gst_event_get_seqnum (event);
  if (new_seqnum == dlna_src->time_seek_seqnum) {
    if (dlna_src->handled_time_seek_seqnum) {
      GST_INFO_OBJECT (dlna_src, "Already processed seek event %d", new_seqnum);
      return FALSE;
    } else
      /* *TODO* - see dlnasrc issue #63
         Got same event now byte based since some element converted it to bytes
         Convert here if time seek is supported since it is more accurate */
    if (dlna_src->time_seek_supported) {
      convert_start = TRUE;
      GST_INFO_OBJECT (dlna_src,
          "Set flag to convert time based seek to byte since server does support time seeks");
    } else
      GST_INFO_OBJECT (dlna_src,
          "Unable to convert time based seek to byte since server does not support time seeks");
  } else {
    dlna_src->handled_time_seek_seqnum = FALSE;
    dlna_src->time_seek_seqnum = new_seqnum;
    dlna_src->time_seek_event_start = 0;
  }

  gst_event_parse_seek (event, &rate, (GstFormat *) & format,
      (GstSeekFlags *) & flags,
      (GstSeekType *) & start_type, (gint64 *) & start,
      (GstSeekType *) & stop_type, (gint64 *) & stop);

  GST_INFO_OBJECT (dlna_src,
      "Got Seek event %d: rate: %3.1f, format: %s, flags: %d, start type: %d,  start: %"
      G_GUINT64_FORMAT ", stop type: %d, stop: %"
      G_GUINT64_FORMAT, gst_event_get_seqnum (event), rate,
      gst_format_get_name (format), flags, start_type, start, stop_type, stop);

  if (convert_start) {
    GST_INFO_OBJECT (dlna_src,
        "Supplied start byte %" G_GUINT64_FORMAT
        " ignored, converting seek event start time %" GST_TIME_FORMAT
        " to bytes", start, GST_TIME_ARGS (dlna_src->time_seek_event_start));
    if (!dlna_src_convert_npt_nanos_to_bytes (dlna_src,
            dlna_src->time_seek_event_start, (guint64 *) & start)) {
      GST_WARNING_OBJECT (dlna_src, "Problems converting to bytes");
      return TRUE;
    }
  }

  if (!dlna_src_is_change_valid
      (dlna_src, rate, format, start, start_type, stop, stop_type)) {
    GST_WARNING_OBJECT (dlna_src, "Requested change is invalid, event handled");

    return TRUE;
  }
  /* *TODO* - is this needed here??? Assign play rate to supplied rate */
  dlna_src->rate = rate;

  dlna_src->requested_rate = rate;
  dlna_src->requested_format = format;
  dlna_src->requested_start = start;
  dlna_src->requested_stop = GST_CLOCK_TIME_NONE;
  if (stop > -1)
    dlna_src->requested_stop = stop;

  if (dlna_src->is_live && stop < 0) {
    dlna_src->requested_stop = dlna_src->npt_end_nanos;
    GST_INFO_OBJECT (dlna_src,
        "Set requested stop to end of range since content is live: %"
        G_GUINT64_FORMAT, dlna_src->requested_stop);
  }

  if ((dlna_src->uri) && (dlna_src->server_info) &&
      (dlna_src->server_info->content_features != NULL)) {
    if (!dlna_src_adjust_http_src_headers (dlna_src, dlna_src->requested_rate,
            dlna_src->requested_format, dlna_src->requested_start,
            dlna_src->requested_stop, new_seqnum)) {
      GST_ERROR_OBJECT (dlna_src, "Problems adjusting soup http src headers");
      /* Returning true to prevent further processing */
      return TRUE;
    }
  } else
    GST_INFO_OBJECT (dlna_src,
        "No header adjustments since server only supports Range requests");

  GST_DEBUG_OBJECT (dlna_src,
      "returning false to make sure souphttpsrc gets chance to process");
  return FALSE;
}

/**
 * Determines if the requested rate and/or position change is valid.  Seek type is
 * ignored since the position is always treated as absolute since a position must be
 * included as part of the HTTP request otherwise zero is assumed.
 *
 * @param	dlna_src    this element
 * @param	rate        new requested rate
 * @param	format      format of change, either bytes or time
 * @param	start       new starting position, either in bytes or time depending on format
 * @param   start_type  starting seek type, either none, relative or absolute
 * @param   stop        new stop position, either in bytes or time depending on format
 * @param   stop_type   stop seek type, either none, relative or absolute
 *
 * @return	true if change is valid, false otherwise
 *
 */
static gboolean
dlna_src_is_change_valid (GstDlnaSrc * dlna_src, gfloat rate,
    GstFormat format, guint64 start,
    GstSeekType start_type, guint64 stop, GstSeekType stop_type)
{
  gchar *live_content_head_request_headers[][2] =
      { {HEADER_GET_AVAILABLE_SEEK_RANGE_TITLE,
      HEADER_GET_AVAILABLE_SEEK_RANGE_VALUE}
  };

  gsize live_content_head_request_headers_size = 1;
  GST_INFO_OBJECT (dlna_src, "Called");

  if ((rate == 1.0) || (dlna_src_is_rate_supported (dlna_src, rate))) {
    GST_INFO_OBJECT (dlna_src, "New rate of %4.1f is supported by server",
        rate);
  } else {
    GST_WARNING_OBJECT (dlna_src, "Rate of %4.1f is not supported by server",
        rate);
    return FALSE;
  }

  if (format == GST_FORMAT_BYTES) {
    if (dlna_src->byte_seek_supported) {

      if (start < dlna_src->byte_start || start > dlna_src->byte_end) {
        GST_WARNING_OBJECT (dlna_src,
            "Specified start byte %" G_GUINT64_FORMAT
            " is not valid, valid range: %" G_GUINT64_FORMAT
            " to %" G_GUINT64_FORMAT, start,
            dlna_src->byte_start, dlna_src->byte_end);
        return FALSE;
      } else
        GST_INFO_OBJECT (dlna_src,
            "Specified start byte %"
            G_GUINT64_FORMAT " is valid, valid range: %" G_GUINT64_FORMAT
            " to %" G_GUINT64_FORMAT, start,
            dlna_src->byte_start, dlna_src->byte_end);
    } else {
      GST_WARNING_OBJECT (dlna_src, "Server does not support byte based seeks");
      return FALSE;
    }
  } else if (format == GST_FORMAT_TIME) {
    if (dlna_src->time_seek_supported) {

      if (dlna_src->is_live) {
        GST_INFO_OBJECT (dlna_src, "Update live content range info");
        if (!dlna_src_soup_issue_head (dlna_src,
                live_content_head_request_headers_size,
                live_content_head_request_headers, dlna_src->server_info,
                TRUE)) {
          GST_ERROR_OBJECT (dlna_src,
              "Problems issuing HEAD request to get live content information");
          return FALSE;
        }

        g_object_set (G_OBJECT (dlna_src->http_src), "content-size",
            dlna_src->byte_total, NULL);
        GST_INFO_OBJECT (dlna_src,
            "Set HTTP src content size: %" G_GUINT64_FORMAT,
            dlna_src->byte_total);
      }

      if (start < dlna_src->npt_start_nanos || start > dlna_src->npt_end_nanos) {
        GST_WARNING_OBJECT (dlna_src,
            "Specified start time %" GST_TIME_FORMAT
            " is not valid, valid range: %" GST_TIME_FORMAT
            " to %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
            GST_TIME_ARGS (dlna_src->npt_start_nanos),
            GST_TIME_ARGS (dlna_src->npt_end_nanos));
        return FALSE;
      }
    } else {
      GST_WARNING_OBJECT (dlna_src, "Server does not support time based seeks");
      return FALSE;
    }
  } else {
    GST_WARNING_OBJECT (dlna_src, "Supplied format type is not supported: %d",
        format);
    return FALSE;
  }

  GST_DEBUG_OBJECT (dlna_src, "Requested change is valid");

  return TRUE;
}

/**
 * Determines if current rate is supported by server based on current
 * URI and HEAD response.
 *
 * @param dlna_src		this element
 * @param rate			requested rate
 *
 * @return	true if requested rate is supported by server, false otherwise
 */
static gboolean
dlna_src_is_rate_supported (GstDlnaSrc * dlna_src, gfloat rate)
{
  gboolean is_supported = FALSE;
  int i = 0;

  if (!dlna_src->time_seek_supported) {
    GST_WARNING_OBJECT (dlna_src,
        "Unable to change rate, not supported by server");
    return FALSE;
  }

  /* Look through list of server supported playspeeds and verify rate is supported */
  for (i = 0; i < dlna_src->server_info->content_features->playspeeds_cnt; i++) {
    if (dlna_src->server_info->content_features->playspeeds[i] == rate) {
      is_supported = TRUE;
      break;
    }
  }

  return is_supported;
}

/**
 * Create extra headers to supply to soup http src based on requested starting
 * postion and rate.  Instruct souphttpsrc to exclude range header if it conflicts
 * with any of the extra headers which have been added.
 *
 * @param	dlna_src 	this element
 * @param	rate		requested rate to include in playspeed header
 * @param	format		create either time or byte based seek header
 * @param	start		starting position to include, will be either bytes or time depending on format
 * @param   stop        stopping position to include, will be -1 if unspecified
 *
 * @return	true if extra headers were successfully created, false otherwise
 */
static gboolean
dlna_src_adjust_http_src_headers (GstDlnaSrc * dlna_src, gfloat rate,
    GstFormat format, guint64 start, guint64 stop, guint32 new_seqnum)
{
  GValue struct_value = G_VALUE_INIT;
  GstStructure *extra_headers_struct;

  gboolean disable_range_header = FALSE;
  int i = 0;
  gchar *rateStr = NULL;

  const gchar *playspeed_field_name = "PlaySpeed.dlna.org";
  const gchar *playspeed_field_value_prefix = "speed=";
  gchar playspeed_field_value[64] = { 0 };

  const gchar *time_seek_range_field_name = "TimeSeekRange.dlna.org";
  const gchar *time_seek_range_field_value_prefix = "npt=";
  gchar time_seek_range_field_value[64] = { 0 };
  guint64 start_time_nanos;
  guint64 start_time_secs;
  guint64 stop_time_nanos = GST_CLOCK_TIME_NONE;
  guint64 stop_time_secs;

  const gchar *range_dtcp_field_name = "Range.dtcp.com";
  const gchar *range_dtcp_field_value_prefix = "bytes=";
  gchar range_dtcp_field_value[64] = { 0 };

  /* Make sure range header is included by default */
  GValue boolean_value = G_VALUE_INIT;
  g_value_init (&boolean_value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&boolean_value, FALSE);
  g_object_set_property (G_OBJECT (dlna_src->http_src), "exclude-range-header",
      &boolean_value);

  /* Create header structure with dlna transfer mode header */
  extra_headers_struct = gst_structure_new ("extraHeadersStruct",
      "transferMode.dlna.org", G_TYPE_STRING, "Streaming", NULL);
  if (extra_headers_struct == NULL) {
    GST_WARNING_OBJECT (dlna_src, "Problems creating extra headers structure");
    return FALSE;
  }

  /* If rate != 1.0, add playspeed header and time seek range header */
  if (rate != 1.0) {
    /* Get string representation of rate (use original values to make fractions easy like 1/3) */
    for (i = 0; i < dlna_src->server_info->content_features->playspeeds_cnt;
        i++) {
      if (dlna_src->server_info->content_features->playspeeds[i] == rate) {
        rateStr = dlna_src->server_info->content_features->playspeed_strs[i];
        break;
      }
    }
    if (rateStr == NULL) {
      GST_ERROR_OBJECT (dlna_src,
          "Unable to get string representation of supported rate: %lf", rate);
      return FALSE;
    }
    g_snprintf (playspeed_field_value, 64, "%s%s",
        playspeed_field_value_prefix, rateStr);

    gst_structure_set (extra_headers_struct, playspeed_field_name,
        G_TYPE_STRING, &playspeed_field_value, NULL);
    GST_INFO_OBJECT (dlna_src,
        "Adjust headers by including playspeed header value: %s",
        playspeed_field_value);
  }

  /* Add time seek header for all non 1x rates or for time based seeks */
  if (rate != 1.0 || (format == GST_FORMAT_TIME
          && dlna_src->time_seek_supported)) {
    if (format != GST_FORMAT_TIME) {
      if (!dlna_src_convert_bytes_to_npt_nanos (dlna_src, start,
              &start_time_nanos)) {
        GST_WARNING_OBJECT (dlna_src,
            "Problems converting start %" G_GUINT64_FORMAT " to npt ", start);
        return FALSE;
      } else
        GST_INFO_OBJECT (dlna_src,
            "Due to non 1x playspeed, converted start %" G_GUINT64_FORMAT
            " bytes into time %" G_GUINT64_FORMAT, start, start_time_nanos);

      if (stop > GST_CLOCK_TIME_NONE) {
        if (!dlna_src_convert_bytes_to_npt_nanos (dlna_src, stop,
                &stop_time_nanos)) {
          GST_WARNING_OBJECT (dlna_src,
              "Problems converting stop %" G_GUINT64_FORMAT " to npt ", stop);
          return FALSE;
        } else
          GST_INFO_OBJECT (dlna_src,
              "Due to non 1x playspeed, converted stop %" G_GUINT64_FORMAT
              " bytes into time %" G_GUINT64_FORMAT, start, start_time_nanos);
      } else
        GST_INFO_OBJECT (dlna_src, "Stop undefined, no conversion needed");
    } else {
      /* Already in time format, no conversion necessary */
      start_time_nanos = start;
      stop_time_nanos = stop;
      GST_INFO_OBJECT (dlna_src,
          "Using start %" G_GUINT64_FORMAT " and stop %" G_GUINT64_FORMAT,
          start_time_nanos, stop_time_nanos);
    }

    /* Convert time from nanos into secs for header value string */
    start_time_secs = start_time_nanos / GST_SECOND;

    if (stop_time_nanos == GST_CLOCK_TIME_NONE) {
      GST_INFO_OBJECT (dlna_src,
          "Stop time is undefined, just including start time");
      g_snprintf (time_seek_range_field_value, 64,
          "%s%" G_GUINT64_FORMAT ".0-", time_seek_range_field_value_prefix,
          start_time_secs);
    } else {
      stop_time_secs = stop_time_nanos / GST_SECOND;
      GST_INFO_OBJECT (dlna_src, "Including stop secs: %" G_GUINT64_FORMAT,
          stop_time_secs);
      g_snprintf (time_seek_range_field_value, 64,
          "%s%" G_GUINT64_FORMAT ".0-%" G_GUINT64_FORMAT ".0",
          time_seek_range_field_value_prefix, start_time_secs, stop_time_secs);
    }
    gst_structure_set (extra_headers_struct, time_seek_range_field_name,
        G_TYPE_STRING, &time_seek_range_field_value, NULL);

    GST_INFO_OBJECT (dlna_src,
        "Adjust headers by including TimeSeekRange header value: %s",
        time_seek_range_field_value);

    /* Set flag so range header is not included by souphttpsrc */
    disable_range_header = TRUE;

    /* Record seqnum since have handled this event as time based seek */
    dlna_src->handled_time_seek_seqnum = TRUE;
  } else
    GST_INFO_OBJECT (dlna_src,
        "Not adjusting with playspeed or time seek range ");

  if (format == GST_FORMAT_TIME && !dlna_src->time_seek_supported) {
    GST_INFO_OBJECT (dlna_src,
        "Saving start time incase get this event again in bytes in order to do acurate conversion");
    dlna_src->time_seek_event_start = start;
  }

  /* If dtcp protected content and rate = 1.0, add range.dtcp.com header */
  if (rate == 1.0 && format == GST_FORMAT_BYTES
      && dlna_src->is_encrypted && dlna_src->byte_seek_supported) {
    g_snprintf (range_dtcp_field_value, 64, "%s%" G_GUINT64_FORMAT "-",
        range_dtcp_field_value_prefix, start);

    gst_structure_set (extra_headers_struct, range_dtcp_field_name,
        G_TYPE_STRING, &range_dtcp_field_value, NULL);

    GST_INFO_OBJECT (dlna_src,
        "Adjust headers by including Range.dtcp.com header value: %s",
        range_dtcp_field_value);

    /* Set flag so range header is not included by souphttpsrc */
    disable_range_header = TRUE;
  } else
    GST_INFO_OBJECT (dlna_src, "Not adjusting with range.dtcp.com");

  g_value_init (&struct_value, GST_TYPE_STRUCTURE);
  gst_value_set_structure (&struct_value, extra_headers_struct);
  g_object_set_property (G_OBJECT (dlna_src->http_src), "extra-headers",
      &struct_value);
  gst_structure_free (extra_headers_struct);

  /* Disable range header if necessary */
  if (disable_range_header || !dlna_src->byte_seek_supported) {
    g_value_set_boolean (&boolean_value, TRUE);
    g_object_set_property (G_OBJECT (dlna_src->http_src),
        "exclude-range-header", &boolean_value);
    GST_INFO_OBJECT (dlna_src,
        "Adjust headers by excluding range header, flag: %d, supported: %d",
        disable_range_header, dlna_src->byte_seek_supported);
  }

  return TRUE;
}

static guint
gst_dlna_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_dlna_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "http", "https", NULL };
  return protocols;
}

static gchar *
gst_dlna_src_uri_get_uri (GstURIHandler * handler)
{
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (handler);
  return g_strdup (dlna_src->uri);
}

static gboolean
gst_dlna_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstDlnaSrc *dlna_src = GST_DLNA_SRC (handler);

  GST_INFO_OBJECT (dlna_src, "uri handler called to set uri: %s, current: %s",
      uri, dlna_src->uri);

  return dlna_src_uri_assign (dlna_src, uri, error);
}

static void
gst_dlna_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_dlna_src_uri_get_type;
  iface->get_protocols = gst_dlna_src_uri_get_protocols;
  iface->get_uri = gst_dlna_src_uri_get_uri;
  iface->set_uri = gst_dlna_src_uri_set_uri;
}

/**
 * Sets the URI property to the supplied value.  It is called either via the URI
 * handler interface set method or via setting the element's property.  The
 * additional setup (issuing HEAD and setting up souphttpsrc for GET) for the URI
 * is suppose to be performed when state changes from READY->PAUSED but it seems
 * that it must be performed here otherwise we get an error and playback fails due
 * to unlinked.
 */
static gboolean
dlna_src_uri_assign (GstDlnaSrc * dlna_src, const gchar * uri, GError ** error)
{
  if (dlna_src->uri) {
    g_free (dlna_src->uri);
    dlna_src->uri = NULL;
  }

  if (uri == NULL)
    return FALSE;

  dlna_src->uri = g_strdup (uri);

  return dlna_src_uri_init (dlna_src);
}

/**
 * Gathers the info about the URI and then sets up the bin, either including the
 * dtcp decrypter element or not based on HEAD response.
 */
static gboolean
dlna_src_uri_init (GstDlnaSrc * dlna_src)
{
  GST_INFO_OBJECT (dlna_src, "Called");

  if (dlna_src->is_uri_initialized) {
    GST_DEBUG_OBJECT (dlna_src, "Returning since URI is already initialized");
    return TRUE;
  }

  if (!dlna_src_uri_gather_info (dlna_src)) {
    GST_ERROR_OBJECT (dlna_src, "Problems gathering URI info");
    return FALSE;
  }

  if (!dlna_src_setup_bin (dlna_src)) {
    GST_ERROR_OBJECT (dlna_src, "Problems setting up dtcp elements");
    return FALSE;
  }

  dlna_src->is_uri_initialized = TRUE;

  return TRUE;
}

/**
 * Perform actions necessary based on supplied URI which is called by
 * playbin when this element is selected as source.
 *
 * @param dlna_src	this element
 * @param value		specified URI to use
 *
 * @return	true if uri is set without problems, false otherwise
 */
static gboolean
dlna_src_setup_bin (GstDlnaSrc * dlna_src)
{
  guint64 content_size;
  GstPad *pad = NULL;

  GST_INFO_OBJECT (dlna_src, "called");

  /* Setup souphttpsrc as source element for this bin */
  dlna_src->http_src =
      gst_element_factory_make ("souphttpsrc", ELEMENT_NAME_SOUP_HTTP_SRC);
  if (!dlna_src->http_src) {
    GST_ERROR_OBJECT (dlna_src,
        "The http soup source element could not be created.");
    return FALSE;
  }

  gst_bin_add (GST_BIN (&dlna_src->bin), dlna_src->http_src);

  if (dlna_src->uri) {
    GST_INFO_OBJECT (dlna_src, "Setting URI of souphttpsrc");
    g_object_set (G_OBJECT (dlna_src->http_src), "location", dlna_src->uri,
        NULL);
  } else
    GST_INFO_OBJECT (dlna_src, "Not setting URI of souphttpsrc");

  /* Setup dtcp element if necessary */
  if (dlna_src->is_encrypted) {
    GST_INFO_OBJECT (dlna_src, "Setting up dtcp");
    if (!dlna_src_setup_dtcp (dlna_src)) {
      GST_ERROR_OBJECT (dlna_src, "Problems setting up dtcp elements");
      return FALSE;
    }
    GST_INFO_OBJECT (dlna_src, "DTCP setup successful");
  } else
    GST_INFO_OBJECT (dlna_src, "No DTCP setup required");

  /* Create src ghost pad of dlna src so playbin will recognize element as a src */
  if (dlna_src->is_encrypted) {
    GST_DEBUG_OBJECT (dlna_src, "Getting decrypter src pad");
    pad = gst_element_get_static_pad (dlna_src->dtcp_decrypter, "src");
  } else {
    GST_DEBUG_OBJECT (dlna_src, "Getting http src pad");
    pad = gst_element_get_static_pad (dlna_src->http_src, "src");
  }
  if (!pad) {
    GST_ERROR_OBJECT (dlna_src,
        "Could not get pad to ghost pad for dlnasrc. Exiting.");
    return FALSE;
  }

  GST_DEBUG_OBJECT (dlna_src, "Got src pad to use for ghostpad of dlnasrc bin");
  dlna_src->src_pad = gst_ghost_pad_new ("src", pad);
  gst_pad_set_active (dlna_src->src_pad, TRUE);

  gst_element_add_pad (GST_ELEMENT (&dlna_src->bin), dlna_src->src_pad);
  gst_object_unref (pad);

  gst_pad_set_event_function (dlna_src->src_pad,
      (GstPadEventFunction) gst_dlna_src_event);

  gst_pad_set_query_function (dlna_src->src_pad,
      (GstPadQueryFunction) gst_dlna_src_query);

  if (dlna_src->byte_total && dlna_src->http_src) {
    content_size = dlna_src->byte_total;

    g_object_set (G_OBJECT (dlna_src->http_src), "content-size", content_size,
        NULL);
    GST_INFO_OBJECT (dlna_src, "Set HTTP src content size: %" G_GUINT64_FORMAT,
        content_size);
  }

  return TRUE;
}

/**
 * Setup dtcp decoder element and add to src in order to handle DTCP encrypted
 * content
 *
 * @param dlna_src	this element
 *
 * @return	true if successfully setup, false otherwise
 */
static gboolean
dlna_src_setup_dtcp (GstDlnaSrc * dlna_src)
{
  GST_INFO_OBJECT (dlna_src, "Setup for dtcp content");

  if (dlna_src->dtcp_decrypter) {
    GST_INFO_OBJECT (dlna_src, "Already setup for dtcp content");
    return TRUE;
  }

  GST_INFO_OBJECT (dlna_src, "Creating dtcp decrypter");
  dlna_src->dtcp_decrypter = gst_element_factory_make ("dtcpip",
      ELEMENT_NAME_DTCP_DECRYPTER);
  if (!dlna_src->dtcp_decrypter) {
    GST_ERROR_OBJECT (dlna_src,
        "The dtcp decrypter element could not be created. Exiting.");
    return FALSE;
  }

  g_object_set (G_OBJECT (dlna_src->dtcp_decrypter), "dtcp1host",
      dlna_src->server_info->dtcp_host, NULL);

  g_object_set (G_OBJECT (dlna_src->dtcp_decrypter), "dtcp1port",
      dlna_src->server_info->dtcp_port, NULL);

  gst_bin_add (GST_BIN (&dlna_src->bin), dlna_src->dtcp_decrypter);

  if (!gst_element_link_many
      (dlna_src->http_src, dlna_src->dtcp_decrypter, NULL)) {
    GST_ERROR_OBJECT (dlna_src, "Problems linking elements in src. Exiting.");
    return FALSE;
  }
  /* Setup the block size for dtcp */
  g_object_set (dlna_src->http_src, "blocksize", dlna_src->dtcp_blocksize,
      NULL);

  return TRUE;
}

/**
 * Initialize the URI which includes formulating a HEAD request
 * and parsing the response to get needed info about the URI.
 *
 * @param dlna_src	this element
 * @param value		specified URI to use
 *
 * @return	true if no problems encountered, false otherwise
 */
static gboolean
dlna_src_uri_gather_info (GstDlnaSrc * dlna_src)
{
  GString *struct_str = g_string_sized_new (MAX_HTTP_BUF_SIZE);

  gchar *content_features_head_request_headers[][2] =
      { {HEADER_GET_CONTENT_FEATURES_TITLE,
      HEADER_GET_CONTENT_FEATURES_VALUE}
  };
  gsize content_features_head_request_headers_array_size = 1;

  gchar *live_content_head_request_headers[][2] =
      { {HEADER_GET_AVAILABLE_SEEK_RANGE_TITLE,
      HEADER_GET_AVAILABLE_SEEK_RANGE_VALUE}
  };
  gsize live_content_head_request_headers_array_size = 1;

  gchar *time_seek_head_request_headers[][2] =
      { {HEADER_TIME_SEEK_RANGE_TITLE, HEADER_TIME_SEEK_RANGE_VALUE} };
  gsize time_seek_head_request_headers_array_size = 1;

  gchar *range_head_request_headers[][2] =
      { {HEADER_RANGE_BYTES_TITLE, HEADER_RANGE_BYTES_VALUE} };
  gsize range_head_request_headers_array_size = 1;

  gchar *dtcp_range_head_request_headers[][2] =
      { {HEADER_DTCP_RANGE_BYTES_TITLE, HEADER_DTCP_RANGE_BYTES_VALUE} };
  gsize dtcp_range_head_request_headers_array_size = 1;

  GST_INFO_OBJECT (dlna_src, "Called");

  if (!dlna_src->uri) {
    GST_DEBUG_OBJECT (dlna_src, "No URI set yet");
    return TRUE;
  }

  /* Make sure a soup session is open */
  if (!dlna_src_soup_session_open (dlna_src)) {
    GST_ERROR_OBJECT (dlna_src,
        "Problems initializing struct to store HEAD response");
    return FALSE;
  }

  /* Initialize server info */
  if (!dlna_src_head_response_init_struct (dlna_src, &dlna_src->server_info)) {
    GST_ERROR_OBJECT (dlna_src,
        "Problems initializing struct to store HEAD response");
    return FALSE;
  }

  /* Issue first head with just content features to determine what server supports */
  GST_INFO_OBJECT (dlna_src,
      "Issuing HEAD Request with content features to determine what server supports");

  if (!dlna_src_soup_issue_head (dlna_src,
          content_features_head_request_headers_array_size,
          content_features_head_request_headers, dlna_src->server_info, TRUE)) {
    GST_ERROR_OBJECT (dlna_src,
        "Problems issuing HEAD request to get content features");
    return FALSE;
  }

  /* Formulate second HEAD request to gather more info */
  if (dlna_src->is_live) {

    GST_INFO_OBJECT (dlna_src,
        "Issuing another HEAD request to get live content info");
    if (!dlna_src_soup_issue_head (dlna_src,
            live_content_head_request_headers_array_size,
            live_content_head_request_headers, dlna_src->server_info, TRUE)) {
      GST_ERROR_OBJECT (dlna_src,
          "Problems issuing HEAD request to get live content information");
      return FALSE;
    }
  } else if (dlna_src->time_seek_supported) {
    GST_INFO_OBJECT (dlna_src,
        "Issuing another HEAD request to get time seek info");
    if (!dlna_src_soup_issue_head (dlna_src,
            time_seek_head_request_headers_array_size,
            time_seek_head_request_headers, dlna_src->server_info, TRUE)) {
      GST_ERROR_OBJECT (dlna_src,
          "Problems issuing HEAD request to get time seek information");
      return FALSE;
    }
  } else if (dlna_src->byte_seek_supported && dlna_src->is_encrypted) {
    GST_INFO_OBJECT (dlna_src,
        "Issuing another HEAD request to get dtcp range info");
    if (!dlna_src_soup_issue_head (dlna_src,
            dtcp_range_head_request_headers_array_size,
            dtcp_range_head_request_headers, dlna_src->server_info, TRUE)) {
      GST_ERROR_OBJECT (dlna_src,
          "Problems issuing HEAD request to get range information");
      return FALSE;
    }
  } else if (dlna_src->byte_seek_supported) {
    GST_INFO_OBJECT (dlna_src,
        "Issuing another HEAD request to get range info");
    if (!dlna_src_soup_issue_head (dlna_src,
            range_head_request_headers_array_size,
            range_head_request_headers, dlna_src->server_info, TRUE)) {
      GST_ERROR_OBJECT (dlna_src,
          "Problems issuing HEAD request to get range information");
      return FALSE;
    }
  } else {
    GST_INFO_OBJECT (dlna_src, "Not issuing another HEAD request");
  }

  /* Print out results of HEAD request */
  dlna_src_head_response_struct_to_str (dlna_src, dlna_src->server_info,
      struct_str);

  GST_INFO_OBJECT (dlna_src, "Parsed HEAD Response into struct: %s",
      struct_str->str);

  return TRUE;
}

static gboolean
dlna_src_soup_issue_head (GstDlnaSrc * dlna_src, gsize header_array_size,
    gchar * headers[][2], GstDlnaSrcHeadResponse * head_response,
    gboolean do_update_overall_info)
{
  gint i;

  GST_INFO_OBJECT (dlna_src, "Creating soup message");
  dlna_src->soup_msg = soup_message_new (SOUP_METHOD_HEAD, dlna_src->uri);
  if (!dlna_src->soup_msg) {
    GST_WARNING_OBJECT (dlna_src,
        "Unable to create soup message for HEAD request");
    return FALSE;
  }

  GST_DEBUG_OBJECT (dlna_src, "Adding headers to soup message");
  for (i = 0; i < header_array_size; i++)
    soup_message_headers_append (dlna_src->soup_msg->request_headers,
        headers[i][0], headers[i][1]);

  GST_DEBUG_OBJECT (dlna_src, "Sending soup message");
  head_response->ret_code =
      soup_session_send_message (dlna_src->soup_session, dlna_src->soup_msg);

  /* Print out HEAD request & response */
  dlna_src_soup_log_msg (dlna_src);

  /* Make sure return code from HEAD response is some form of success */
  if (head_response->ret_code != HTTP_STATUS_OK &&
      head_response->ret_code != HTTP_STATUS_CREATED &&
      head_response->ret_code != HTTP_STATUS_PARTIAL) {

    GST_WARNING_OBJECT (dlna_src,
        "Error code received in HEAD response: %d %s",
        head_response->ret_code, head_response->ret_msg);
    return FALSE;
  }
  /* Parse HEAD response to gather info about URI content item */
  if (!dlna_src_head_response_parse (dlna_src, head_response)) {
    GST_WARNING_OBJECT (dlna_src, "Problems parsing HEAD response");
    return FALSE;
  }

  if (do_update_overall_info) {
    GST_INFO_OBJECT (dlna_src, "Updating overall info");
    /* Update info based on response to HEAD info */
    if (!dlna_src_update_overall_info (dlna_src, head_response))
      GST_WARNING_OBJECT (dlna_src, "Problems initializing content info");
  } else
    GST_INFO_OBJECT (dlna_src, "Not updating overall info");

  dlna_src->soup_msg = NULL;

  return TRUE;
}

/**
 * Assigns values to overall start, end and total based on the type of
 * content and HTTP header values that were returned.
 *
 * @param dlna_src  this element
 *
 * @return  true if no problems encountered, false otherwise
 */
static gboolean
dlna_src_update_overall_info (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response)
{
  GST_INFO_OBJECT (dlna_src, "Called");

  guint64 content_size;
  GString *npt_str = g_string_sized_new (32);

  if (!head_response) {
    GST_WARNING_OBJECT (dlna_src,
        "No head response, can't determine info about content");
    return FALSE;
  }

  if (head_response->content_features) {

    if (head_response->content_features->flag_so_increasing_set
        || head_response->content_features->flag_sn_increasing_set) {
      dlna_src->is_live = TRUE;
      GST_INFO_OBJECT (dlna_src,
          "Content is live since s0 and/or sN is increasing");
    }

    if (head_response->content_features->flag_link_protected_set) {
      dlna_src->is_encrypted = TRUE;
      GST_INFO_OBJECT (dlna_src,
          "Content is encrypted since link protected flag is set");
    }

    if (head_response->available_seek_cleartext_end) {
      dlna_src->byte_start = head_response->available_seek_cleartext_start;
      dlna_src->byte_end = head_response->available_seek_cleartext_end;
      dlna_src->byte_total =
          head_response->available_seek_cleartext_end -
          head_response->available_seek_cleartext_start;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from cleartext availableSeekRange.dlna.org");
    } else if (head_response->available_seek_end) {
      dlna_src->byte_start = head_response->available_seek_start;
      dlna_src->byte_end = head_response->available_seek_end;
      dlna_src->byte_total =
          head_response->available_seek_end -
          head_response->available_seek_start;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from availableSeekRange.dlna.org");
    } else if (head_response->dtcp_range_total) {
      dlna_src->byte_start = head_response->dtcp_range_start;
      dlna_src->byte_end = head_response->dtcp_range_end;
      dlna_src->byte_total = head_response->dtcp_range_total;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from Content-Range.dtcp.com");
    } else if (head_response->time_byte_seek_total) {
      dlna_src->byte_start = head_response->time_byte_seek_start;
      dlna_src->byte_end = head_response->time_byte_seek_end;
      dlna_src->byte_total = head_response->time_byte_seek_total;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from TimeSeekRange.dlna.org");
    }

    if (head_response->available_seek_npt_start_str) {
      dlna_src->npt_start_str =
          g_strdup (head_response->available_seek_npt_start_str);
      dlna_src->npt_start_nanos = head_response->available_seek_npt_start;
      dlna_src->npt_end_str =
          g_strdup (head_response->available_seek_npt_end_str);
      dlna_src->npt_end_nanos = head_response->available_seek_npt_end;
      dlna_src->npt_duration_nanos =
          head_response->available_seek_npt_end -
          head_response->available_seek_npt_start;
      dlna_src_nanos_to_npt (dlna_src, dlna_src->npt_duration_nanos, npt_str);
      dlna_src->npt_duration_str = g_strdup (npt_str->str);
      GST_INFO_OBJECT (dlna_src,
          "Time seek range values coming from availableSeekRange.dlna.org");
    } else if (head_response->time_seek_npt_start_str) {
      dlna_src->npt_start_nanos = head_response->time_seek_npt_start;
      dlna_src->npt_start_str =
          g_strdup (head_response->time_seek_npt_start_str);
      dlna_src->npt_end_nanos = head_response->time_seek_npt_end;
      dlna_src->npt_end_str = g_strdup (head_response->time_seek_npt_end_str);
      dlna_src->npt_duration_nanos = head_response->time_seek_npt_duration;
      dlna_src->npt_duration_str =
          g_strdup (head_response->time_seek_npt_duration_str);
      GST_INFO_OBJECT (dlna_src,
          "Time seek range values coming from TimeSeekRange.dlna.org");
    } else
      GST_INFO_OBJECT (dlna_src, "Time seek range values not available");
  }

  if (!dlna_src->byte_total) {
    if (head_response->content_range_total) {
      dlna_src->byte_start = head_response->content_range_start;
      dlna_src->byte_end = head_response->content_range_end;
      dlna_src->byte_total = head_response->content_range_total;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from Content-Range header");
    } else if (head_response->content_length) {
      dlna_src->byte_start = 0;
      dlna_src->byte_end = head_response->content_length;
      dlna_src->byte_total = head_response->content_length;
      GST_INFO_OBJECT (dlna_src,
          "Byte range values coming from content length, assuming start & stop");
    } else
      GST_INFO_OBJECT (dlna_src, "Byte seek range values not available");
  }

  if (head_response->content_features->op_time_seek_supported ||
      head_response->content_features->flag_limited_time_seek_set) {
    dlna_src->time_seek_supported = TRUE;
  }

  if (head_response->content_features->op_range_supported ||
      head_response->content_features->flag_full_clear_text_set ||
      head_response->content_features->flag_limited_byte_seek_set ||
      head_response->accept_byte_ranges)
    dlna_src->byte_seek_supported = TRUE;

  /* Make sure content size has been set for souphttpsrc */
  if (dlna_src->byte_total && dlna_src->http_src) {
    content_size = dlna_src->byte_total;

    g_object_set (G_OBJECT (dlna_src->http_src), "content-size", content_size,
        NULL);
    GST_INFO_OBJECT (dlna_src, "Set HTTP src content size: %" G_GUINT64_FORMAT,
        content_size);
  } else
    GST_INFO_OBJECT (dlna_src,
        "Unable set content size due to either null souphttpsrc or total == 0");

  return TRUE;
}

/**
 * Free the memory allocated to store head response.
 *
 * @param   dlna_src        this instance of element
 * @param   head_response   free memory associated with this struct
 */
static void
dlna_src_head_response_free (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response)
{
  int i = 0;
  if (head_response) {
    if (head_response->content_features) {
      if (head_response->content_features->profile)
        g_free (head_response->content_features->profile);

      if (head_response->content_features->playspeeds_cnt > 0) {
        for (i = 0; i < head_response->content_features->playspeeds_cnt; i++)
          g_free (head_response->content_features->playspeed_strs[i]);
      }
      g_free (head_response->content_features);
    }

    if (head_response->http_rev)
      g_free (head_response->http_rev);
    if (head_response->ret_msg)
      g_free (head_response->ret_msg);
    if (head_response->accept_ranges)
      g_free (head_response->accept_ranges);
    if (head_response->time_seek_npt_start_str)
      g_free (head_response->time_seek_npt_start_str);
    if (head_response->time_seek_npt_end_str)
      g_free (head_response->time_seek_npt_end_str);
    if (head_response->time_seek_npt_duration_str)
      g_free (head_response->time_seek_npt_duration_str);
    if (head_response->transfer_mode)
      g_free (head_response->transfer_mode);
    if (head_response->transfer_encoding)
      g_free (head_response->transfer_encoding);
    if (head_response->date)
      g_free (head_response->date);
    if (head_response->server)
      g_free (head_response->server);
    if (head_response->content_type)
      g_free (head_response->content_type);
    if (head_response->dtcp_host)
      g_free (head_response->dtcp_host);

    g_free (head_response);
  }
}

static gboolean
dlna_src_soup_session_open (GstDlnaSrc * dlna_src)
{
  GST_LOG_OBJECT (dlna_src, "Called");

  if (dlna_src->soup_session) {
    GST_DEBUG_OBJECT (dlna_src, "Session is already open");
    return TRUE;
  }
  /* *TODO* - dlnasrc issue #94 - Old version (need to upgrade to Libsoup 2.44)
     Creating sync version since need to wait for HEAD responses */
  dlna_src->soup_session = soup_session_sync_new ();

  if (!dlna_src->soup_session) {
    GST_ERROR_OBJECT (dlna_src, "Failed to create soup session");
    return FALSE;
  }
  return TRUE;
}

static void
dlna_src_soup_session_close (GstDlnaSrc * dlna_src)
{
  if (dlna_src->soup_session) {
    soup_session_abort (dlna_src->soup_session);
    g_object_unref (dlna_src->soup_session);
    dlna_src->soup_session = NULL;
    dlna_src->soup_msg = NULL;
  }
}

static void
dlna_src_soup_log_msg (GstDlnaSrc * dlna_src)
{
  const gchar *header_name, *header_value;
  SoupMessageHeadersIter iter;
  GString *log_str = g_string_sized_new (256);

  if (dlna_src->soup_msg) {
    g_string_append_printf (log_str, "\nREQUEST: %s %s HTTP/1.%d\n",
        dlna_src->soup_msg->method,
        soup_uri_to_string (soup_message_get_uri (dlna_src->soup_msg), TRUE),
        soup_message_get_http_version (dlna_src->soup_msg));

    log_str = g_string_append (log_str, "REQUEST HEADERS:\n");
    soup_message_headers_iter_init (&iter, dlna_src->soup_msg->request_headers);
    while (soup_message_headers_iter_next (&iter, &header_name, &header_value))
      g_string_append_printf (log_str, "%s: %s\n", header_name, header_value);

    g_string_append_printf (log_str, "RESPONSE: HTTP/1.%d %d %s\n",
        soup_message_get_http_version (dlna_src->soup_msg),
        dlna_src->soup_msg->status_code, dlna_src->soup_msg->reason_phrase);

    log_str = g_string_append (log_str, "RESPONSE HEADERS:\n");
    soup_message_headers_iter_init (&iter,
        dlna_src->soup_msg->response_headers);
    while (soup_message_headers_iter_next (&iter, &header_name, &header_value))
      g_string_append_printf (log_str, "%s: %s\n", header_name, header_value);

    GST_INFO_OBJECT (dlna_src, "%s", log_str->str);
  } else
    GST_INFO_OBJECT (dlna_src, "Null soup http message");
}

/**
 * Parse HEAD response into specific values related to URI content item.
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_parse (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response)
{
  int i = 0;
  const gchar *header_name, *header_value;
  SoupMessageHeadersIter iter;
  gint idx;
  const gchar *field_values[HEAD_RESPONSE_HEADERS_CNT];

  for (i = 0; i < HEAD_RESPONSE_HEADERS_CNT; i++)
    field_values[i] = NULL;

  soup_message_headers_iter_init (&iter, dlna_src->soup_msg->response_headers);
  while (soup_message_headers_iter_next (&iter, &header_name, &header_value)) {
    GST_LOG_OBJECT (dlna_src, "%s: %s", header_name, header_value);

    /* Look for field header contained in this string */
    idx = dlna_src_head_response_get_field_idx (dlna_src, header_name);

    /* If found field header, extract value */
    if (idx != -1)
      field_values[idx] = header_value;
    else
      GST_INFO_OBJECT (dlna_src, "No Idx found for Field:%s", header_name);
  }

  /* Parse value from each field header string */
  for (i = 0; i < HEAD_RESPONSE_HEADERS_CNT; i++) {
    if (field_values[i] != NULL) {
      dlna_src_head_response_assign_field_value (dlna_src, head_response, i,
          field_values[i]);
    }
  }

  return TRUE;
}

/**
 * Initialize structure to store HEAD Response
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_init_struct (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse ** head_response_ptr)
{
  GST_LOG_OBJECT (dlna_src, "Called");

  GstDlnaSrcHeadResponse *head_response = g_slice_new (GstDlnaSrcHeadResponse);
  head_response->content_features =
      g_slice_new (GstDlnaSrcHeadResponseContentFeatures);

  /* HTTP */
  head_response->http_rev_idx = HEADER_INDEX_HTTP;
  head_response->http_rev = NULL;
  head_response->ret_code = 0;
  head_response->ret_msg = NULL;

  /* TIMESEEKRANGE.DLNA.ORG */
  head_response->time_seek_idx = HEADER_INDEX_TIMESEEKRANGE;

  /* NPT portion of TIMESEEKRANGE.DLNA.ORG */
  head_response->npt_seek_idx = HEADER_INDEX_NPT;
  head_response->time_seek_npt_start_str = NULL;
  head_response->time_seek_npt_end_str = NULL;
  head_response->time_seek_npt_duration_str = NULL;
  head_response->time_seek_npt_start = 0;
  head_response->time_seek_npt_end = 0;
  head_response->time_seek_npt_duration = 0;

  /* BYTES portion of TIMESEEKRANGE.DLNA.ORG */
  head_response->byte_seek_idx = HEADER_INDEX_BYTES;
  head_response->time_byte_seek_start = 0;
  head_response->time_byte_seek_end = 0;
  head_response->time_byte_seek_total = 0;

  /* CLEARTEXTBYTES */
  head_response->clear_text_idx = HEADER_INDEX_CLEAR_TEXT;

  /* AVAILABLESEEKRANGE.DLNA.ORG */
  head_response->available_range_idx = HEADER_INDEX_AVAILABLE_RANGE;
  head_response->available_seek_npt_start_str = NULL;
  head_response->available_seek_npt_end_str = NULL;
  head_response->available_seek_npt_start = 0;
  head_response->available_seek_npt_end = 0;
  head_response->available_seek_start = 0;
  head_response->available_seek_end = 0;
  head_response->available_seek_cleartext_start = 0;
  head_response->available_seek_cleartext_end = 0;

  /* CONTENT RANGE DTCP */
  head_response->dtcp_range_idx = HEADER_INDEX_DTCP_RANGE;
  head_response->dtcp_range_start = 0;
  head_response->dtcp_range_end = 0;
  head_response->dtcp_range_total = 0;

  /* TRANSFERMODE.DLNA.ORG */
  head_response->transfer_mode_idx = HEADER_INDEX_TRANSFERMODE;
  head_response->transfer_mode = NULL;

  /* TRANSFER-ENCODING */
  head_response->transfer_encoding_idx = HEADER_INDEX_TRANSFER_ENCODING;
  head_response->transfer_encoding = NULL;

  /* DATE */
  head_response->date_idx = HEADER_INDEX_DATE;
  head_response->date = NULL;

  /* SERVER */
  head_response->server_idx = HEADER_INDEX_SERVER;
  head_response->server = NULL;

  /* CONTENT-LENGTH */
  head_response->content_length_idx = HEADER_INDEX_CONTENT_LENGTH;
  head_response->content_length = 0;

  /* ACCEPT-RANGES */
  head_response->accept_ranges_idx = HEADER_INDEX_ACCEPT_RANGES;
  head_response->accept_ranges = NULL;
  head_response->accept_byte_ranges = FALSE;

  /* CONTENT-RANGE */
  head_response->content_range_idx = HEADER_INDEX_CONTENT_RANGE;
  head_response->content_range_start = 0;
  head_response->content_range_end = 0;
  head_response->content_range_total = 0;

  /* CONTENT-TYPE */
  head_response->content_type_idx = HEADER_INDEX_CONTENT_TYPE;
  head_response->content_type = NULL;

  /* Addition subfields in CONTENT TYPE if dtcp encrypted */
  head_response->dtcp_host_idx = HEADER_INDEX_DTCP_HOST;
  head_response->dtcp_host = NULL;
  head_response->dtcp_port_idx = HEADER_INDEX_DTCP_PORT;
  head_response->dtcp_port = -1;
  head_response->content_format_idx = HEADER_INDEX_CONTENT_FORMAT;

  /* CONTENTFEATURES.DLNA.ORG */
  head_response->content_features_idx = HEADER_INDEX_CONTENTFEATURES;

  /* DLNA.ORG_PN */
  head_response->content_features->profile_idx = HEADER_INDEX_PN;
  head_response->content_features->profile = NULL;

  /* DLNA.ORG_OP */
  head_response->content_features->operations_idx = HEADER_INDEX_OP;
  head_response->content_features->op_time_seek_supported = FALSE;
  head_response->content_features->op_range_supported = FALSE;

  /* DLNA.ORG_PS */
  head_response->content_features->playspeeds_idx = HEADER_INDEX_PS;
  head_response->content_features->playspeeds_cnt = 0;

  /* DLNA.ORG_FLAGS */
  head_response->content_features->flags_idx = HEADER_INDEX_FLAGS;
  head_response->content_features->flag_sender_paced_set = FALSE;
  head_response->content_features->flag_limited_time_seek_set = FALSE;
  head_response->content_features->flag_limited_byte_seek_set = FALSE;
  head_response->content_features->flag_play_container_set = FALSE;
  head_response->content_features->flag_so_increasing_set = FALSE;
  head_response->content_features->flag_sn_increasing_set = FALSE;
  head_response->content_features->flag_rtsp_pause_set = FALSE;
  head_response->content_features->flag_streaming_mode_set = FALSE;
  head_response->content_features->flag_interactive_mode_set = FALSE;
  head_response->content_features->flag_background_mode_set = FALSE;
  head_response->content_features->flag_stalling_set = FALSE;
  head_response->content_features->flag_dlna_v15_set = FALSE;
  head_response->content_features->flag_link_protected_set = FALSE;
  head_response->content_features->flag_full_clear_text_set = FALSE;
  head_response->content_features->flag_limited_clear_text_set = FALSE;

  /* DLNA.ORG_CI */
  head_response->content_features->conversion_idx = HEADER_INDEX_CI;
  head_response->content_features->is_converted = FALSE;

  *head_response_ptr = head_response;
  return TRUE;
}

/**
 * Looks for a matching HEAD response field in supplied string.
 *
 * @param   dlna_src    this element instance
 * @param	field_str   look for HEAD response field in this string
 *
 * @return	index of matching HEAD response field,
 * 			-1 if does not contain a HEAD response field header
 */
static gint
dlna_src_head_response_get_field_idx (GstDlnaSrc * dlna_src,
    const gchar * field_str)
{
  gint idx = -1;
  int i = 0;

  GST_LOG_OBJECT (dlna_src, "Determine associated HEAD response field: %s",
      field_str);

  for (i = 0; i < HEAD_RESPONSE_HEADERS_CNT; i++) {
    if (strstr (g_ascii_strup (field_str, strlen (field_str)),
            HEAD_RESPONSE_HEADERS[i]) != NULL) {
      idx = i;
      break;
    }
  }

  return idx;
}

/**
 * Initialize associated value in HEAD response struct
 *
 * @param	dlna_src	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_assign_field_value (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_value)
{
  gboolean rc = TRUE;
  gchar tmp1[32] = { 0 };
  gchar tmp2[32] = { 0 };
  gint int_value = 0;
  gint ret_code = 0;
  guint64 guint64_value = 0;

  GST_LOG_OBJECT (dlna_src,
      "Store value received in HEAD response field for field %d - %s, value: %s",
      idx, HEAD_RESPONSE_HEADERS[idx], field_value);

  /* Get value based on index */
  switch (idx) {
    case HEADER_INDEX_TRANSFERMODE:
      head_response->transfer_mode = g_strdup (field_value);
      break;

    case HEADER_INDEX_DATE:
      head_response->date = g_strdup (field_value);
      break;

    case HEADER_INDEX_CONTENT_TYPE:
      if (!dlna_src_head_response_parse_content_type (dlna_src, head_response,
              idx, field_value)) {
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, value: %s",
            HEAD_RESPONSE_HEADERS[idx], field_value);
      }
      break;

    case HEADER_INDEX_CONTENT_LENGTH:
      if ((ret_code =
              sscanf (field_value, "%" G_GUINT64_FORMAT, &guint64_value)) != 1)
        GST_WARNING_OBJECT (dlna_src,
            "Problems parsing Content Length from HEAD response field header %s, value: %s, retcode: %d",
            HEAD_RESPONSE_HEADERS[idx], field_value, ret_code);
      else
        head_response->content_length = guint64_value;
      break;

    case HEADER_INDEX_ACCEPT_RANGES:
      head_response->accept_ranges = g_strdup (field_value);
      if (strstr (g_ascii_strup (head_response->accept_ranges,
                  strlen (head_response->accept_ranges)), ACCEPT_RANGES_BYTES))
        head_response->accept_byte_ranges = TRUE;
      break;

    case HEADER_INDEX_CONTENT_RANGE:
      if (!dlna_src_parse_byte_range (dlna_src, field_value, HEADER_INDEX_BYTES,
              &head_response->content_range_start,
              &head_response->content_range_end,
              &head_response->content_range_total))
        GST_WARNING_OBJECT (dlna_src,
            "Problems parsing Content Range from HEAD response field header %s, value: %s, retcode: %d",
            HEAD_RESPONSE_HEADERS[idx], field_value, ret_code);
      break;

    case HEADER_INDEX_SERVER:
      head_response->server = g_strdup (field_value);
      break;

    case HEADER_INDEX_TRANSFER_ENCODING:
      head_response->transfer_encoding = g_strdup (field_value);
      break;

    case HEADER_INDEX_HTTP:
      if ((ret_code =
              sscanf (field_value, "%31s %d %31[^\n]", tmp1, &int_value,
                  tmp2)) != 3) {
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, idx: %d, value: %s, retcode: %d, tmp: %s, %s",
            HEAD_RESPONSE_HEADERS[idx], idx, field_value, ret_code, tmp1, tmp2);
      } else {
        head_response->http_rev = g_strdup (tmp1);
        head_response->ret_code = int_value;
        head_response->ret_msg = g_strdup (tmp2);
      }
      break;

    case HEADER_INDEX_TIMESEEKRANGE:
      if (!dlna_src_head_response_parse_time_seek (dlna_src, head_response, idx,
              field_value))
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, value: %s",
            HEAD_RESPONSE_HEADERS[idx], field_value);
      break;

    case HEADER_INDEX_CONTENTFEATURES:
      if (!dlna_src_head_response_parse_content_features
          (dlna_src, head_response, idx, field_value))
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, value: %s",
            HEAD_RESPONSE_HEADERS[idx], field_value);
      break;

    case HEADER_INDEX_DTCP_RANGE:
      if (!dlna_src_parse_byte_range (dlna_src, field_value, HEADER_INDEX_BYTES,
              &head_response->dtcp_range_start,
              &head_response->dtcp_range_end, &head_response->dtcp_range_total))
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, value: %s",
            HEAD_RESPONSE_HEADERS[idx], field_value);
      break;

    case HEADER_INDEX_AVAILABLE_RANGE:
      if (!dlna_src_head_response_parse_available_range (dlna_src,
              head_response, idx, field_value))
        GST_WARNING_OBJECT (dlna_src,
            "Problems with HEAD response field header %s, value: %s",
            HEAD_RESPONSE_HEADERS[idx], field_value);
      break;

    case HEADER_INDEX_VARY:
    case HEADER_INDEX_PRAGMA:
    case HEADER_INDEX_CACHE_CONTROL:
      /* Ignore field values */
      break;

    default:
      GST_WARNING_OBJECT (dlna_src,
          "Unsupported HEAD response field idx %d: %s", idx, field_value);
  }

  return rc;
}

/**
 * TimeSeekRange header formatting as specified in DLNA 7.4.40.5:
 *
 * TimeSeekRange.dlna.org : npt=335.1-336.1/40445.4 bytes=1539686400-1540210688/304857907200
 *
 * The time seek range header can have two different formats
 * Either:
 * 	"npt = 1*DIGIT["."1*3DIGIT]
 *		ntp sec = 0.232, or 1 or 15 or 16.652 (leading at one or more digits,
 *		optionally followed by decimal point and 3 digits)
 * OR
 * 	"npt=00:00:00.000" where format is HH:MM:SS.mmm (hours, minutes, seconds, milliseconds)
 *
 * @param	dlna_src	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE
 */
static gboolean
dlna_src_head_response_parse_time_seek (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  /* Extract start and end NPT from TimeSeekRange header */
  if (!dlna_src_parse_npt_range (dlna_src, field_str,
          &head_response->time_seek_npt_start_str,
          &head_response->time_seek_npt_end_str,
          &head_response->time_seek_npt_duration_str,
          &head_response->time_seek_npt_start,
          &head_response->time_seek_npt_end,
          &head_response->time_seek_npt_duration))
    /* Just return, errors which have been logged already */
    return FALSE;

  /* Extract start and end bytes from TimeSeekRange header if present */
  if (strstr (g_ascii_strup (field_str, strlen (field_str)),
          RANGE_HEADERS[HEADER_INDEX_BYTES])) {
    if (!dlna_src_parse_byte_range (dlna_src, field_str, HEADER_INDEX_BYTES,
            &head_response->time_byte_seek_start,
            &head_response->time_byte_seek_end,
            &head_response->time_byte_seek_total))
      return FALSE;
  }

  return TRUE;
}

/**
 * AvailableSeekRange header formatting as specified in DLNA 7.5.4.3.2.20.7:
 *
 * availableSeekRange.dlna.org: 0 npt=0:00:00.000-0:00:48.716 bytes=0-5219255 cleartextbytes=0-5219255
 *
 * The time seek range portion can have two different formats
 * Either:
 *  "npt = 1*DIGIT["."1*3DIGIT]
 *      ntp sec = 0.232, or 1 or 15 or 16.652 (leading at one or more digits,
 *      optionally followed by decimal point and 3 digits)
 * OR
 *  "npt=00:00:00.000" where format is HH:MM:SS.mmm (hours, minutes, seconds, milliseconds)
 *
 * @param   dlna_src    this element instance
 * @param   idx         index which describes HEAD response field and type
 * @param   fieldStr    string containing HEAD response field header and value
 *
 * @return  returns TRUE
 */
static gboolean
dlna_src_head_response_parse_available_range (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  /* Extract start and end NPT from availableSeekRange header */
  if (!dlna_src_parse_npt_range (dlna_src, field_str,
          &head_response->available_seek_npt_start_str,
          &head_response->available_seek_npt_end_str,
          NULL,
          &head_response->available_seek_npt_start,
          &head_response->available_seek_npt_end, NULL))
    /* Just return, errors which have been logged already */
    return FALSE;

  /* Extract start and end bytes from availableSeekRange header using bytes */
  if (!dlna_src_parse_byte_range (dlna_src, field_str,
          HEADER_INDEX_BYTES, &head_response->available_seek_start,
          &head_response->available_seek_end, NULL))
    /* Just return, errors which have been logged already */
    return FALSE;
  /* Extract start and end bytes from availableSeekRange header using clear text bytes */
  if (!dlna_src_parse_byte_range (dlna_src, field_str,
          HEADER_INDEX_CLEAR_TEXT,
          &head_response->available_seek_cleartext_start,
          &head_response->available_seek_cleartext_end, NULL))
    /* Just return, errors which have been logged already */
    return FALSE;

  return TRUE;
}

/**
 * Parse the byte range which may be contained in the following headers:
 *
 * TimeSeekRange.dlna.org : npt=335.1-336.1/40445.4 bytes=1539686400-1540210688/304857907200
 *
 * Content-Range: bytes 0-1859295/1859295
 *
 * Content-Range.dtcp.com: bytes=0-9931928/9931929
 *
 * availableSeekRange.dlna.org: 0 npt=0:00:00.000-0:00:48.716 bytes=0-5219255 cleartextbytes=0-5219255
 *
 * @param   dlna_src    this element instance
 * @param   field_str   string containing HEAD response field header and value
 * @param   header_idx  index which identifies which byte header to use - bytes or cleartextbytes
 * @param   start_byte  starting byte position read from header response field
 * @param   end_byte    end byte position read from header response field
 * @param   total_bytes total bytes read from header response field
 *
 * @return  returns TRUE
 */
static gboolean
dlna_src_parse_byte_range (GstDlnaSrc * dlna_src,
    const gchar * field_str, gint header_index, guint64 * start_byte,
    guint64 * end_byte, guint64 * total_bytes)
{
  gchar *header = NULL;
  gchar *header_value = NULL;

  gint ret_code = 0;
  guint64 ullong1 = 0;
  guint64 ullong2 = 0;
  guint64 ullong3 = 0;

  /* Extract BYTES portion of header value */
  header =
      strstr (g_ascii_strup (field_str, strlen (field_str)),
      RANGE_HEADERS[header_index]);

  if (header)
    header_value = strstr (header, "=");
  if (header && !header_value)
    header_value = strstr (header, " ");
  if (header_value)
    header_value++;
  else {
    GST_WARNING_OBJECT (dlna_src,
        "Bytes not included in header from HEAD response field header value: %s",
        field_str);
    return FALSE;
  }

  /* Determine if byte string includes total which is not an * */
  if (strstr (header_value, "/") && !strstr (header_value, "*")) {
    /* Extract start and end and total BYTES */
    if ((ret_code =
            sscanf (header_value,
                "%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "/%"
                G_GUINT64_FORMAT, &ullong1, &ullong2, &ullong3)) != 3) {
      GST_WARNING_OBJECT (dlna_src,
          "Problems parsing BYTES from HEAD response field header %s, value: %s, retcode: %d, ullong: %"
          G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT
          ", %" G_GUINT64_FORMAT,
          field_str, header_value, ret_code, ullong1, ullong2, ullong3);
      return FALSE;
    }
  } else {
    /* Extract start and end (there is no total) BYTES */
    if ((ret_code =
            sscanf (header_value,
                "%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT, &ullong1,
                &ullong2)) != 2) {
      GST_WARNING_OBJECT (dlna_src,
          "Problems parsing BYTES from HEAD response field header %s, value: %s, retcode: %d, ullong: %"
          G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT, field_str, header_value,
          ret_code, ullong1, ullong2);
      return FALSE;
    }
  }

  if (start_byte)
    *start_byte = ullong1;
  if (end_byte)
    *end_byte = ullong2;
  if (total_bytes)
    *total_bytes = ullong3;

  return TRUE;
}

/**
 * Parse the npt (normal play time) range which may be contained in the following headers:
 *
 * TimeSeekRange.dlna.org : npt=335.1-336.1/40445.4 bytes=1539686400-1540210688/304857907200
 *
 * availableSeekRange.dlna.org: 0 npt=0:00:00.000-0:00:48.716 bytes=0-5219255 cleartextbytes=0-5219255
 *
 * @param   dlna_src    this element instance
 * @param   field_str   string containing HEAD response field header and value
 * @param   start_str   starting time in string form read from header response field
 * @param   stop_str    end time in string form read from header response field
 * @param   total_str   total time in string form read from header response field
 * @param   start       starting time in nanoseconds converted from string representation
 * @param   stop        end time in nanoseconds converted from string representation
 * @param   total       total time in nanoseconds converted from string representation
 *
 * @return  returns TRUE
 */
static gboolean
dlna_src_parse_npt_range (GstDlnaSrc * dlna_src, const gchar * field_str,
    gchar ** start_str, gchar ** stop_str, gchar ** total_str,
    guint64 * start, guint64 * stop, guint64 * total)
{
  gchar *header = NULL;
  gchar *header_value = NULL;

  gint ret_code = 0;
  gchar tmp1[32] = { 0 };
  gchar tmp2[32] = { 0 };
  gchar tmp3[32] = { 0 };

  /* Extract NPT portion of header value */
  header =
      strstr (g_ascii_strup (field_str, strlen (field_str)),
      RANGE_HEADERS[HEADER_INDEX_NPT]);
  if (header)
    header_value = strstr (header, "=");
  if (header_value)
    header_value++;
  else {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing npt from HEAD response field header value: %s",
        field_str);
    return FALSE;
  }

  /* Determine if npt string includes total */
  if (strstr (header_value, "/")) {
    /* Extract start and end and total NPT */
    if ((ret_code =
            sscanf (header_value, "%31[^-]-%31[^/]/%31s %*s", tmp1, tmp2,
                tmp3)) != 3) {
      GST_WARNING_OBJECT (dlna_src,
          "Problems parsing NPT from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s, %s",
          field_str, header_value, ret_code, tmp1, tmp2, tmp3);
      return FALSE;
    }

    *total_str = g_strdup (tmp3);
    if (strcmp (*total_str, "*") != 0)
      if (!dlna_src_npt_to_nanos (dlna_src, *total_str, total))
        return FALSE;
  } else {
    /* Extract start and end (there is no total) NPT */
    if ((ret_code = sscanf (header_value, "%31[^-]-%31s %*s", tmp1, tmp2)) != 2) {
      GST_WARNING_OBJECT (dlna_src,
          "Problems parsing NPT from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
          field_str, header_value, ret_code, tmp1, tmp2);
      return FALSE;
    }
  }
  *start_str = g_strdup (tmp1);
  if (!dlna_src_npt_to_nanos (dlna_src, *start_str, start))
    return FALSE;

  *stop_str = g_strdup (tmp2);
  if (!dlna_src_npt_to_nanos (dlna_src, *stop_str, stop))
    return FALSE;

  return TRUE;
}

/**
 * Extract values from content features header in HEAD Response
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	content feature header field extracted from HEAD response
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_content_features (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_value)
{
  GST_LOG_OBJECT (dlna_src, "Called with field str: %s", field_value);

  /* Split CONTENTFEATURES.DLNA.ORG into following sub-fields using ";" as deliminator
     "DLNA.ORG_PN"
     "DLNA.ORG_OP"
     "DLNA.ORG_PS"
     "DLNA.ORG_FLAGS"
     "DLNA.ORG_CI"
   */
  gchar *pn_str = NULL;
  gchar *op_str = NULL;
  gchar *ps_str = NULL;
  gchar *flags_str = NULL;
  gchar *ci_str = NULL;
  gchar **tokens = NULL;
  gchar *tmp_str = NULL;

  tokens = g_strsplit (field_value, ";", 0);
  gchar **ptr;
  for (ptr = tokens; *ptr; ptr++) {
    if (strlen (*ptr) > 0) {

      /* DLNA.ORG_PN */
      if ((tmp_str =
              strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                  CONTENT_FEATURES_HEADERS[HEADER_INDEX_PN])) != NULL) {
        GST_LOG_OBJECT (dlna_src, "Found field: %s",
            CONTENT_FEATURES_HEADERS[HEADER_INDEX_PN]);
        pn_str = *ptr;
      }
      /* DLNA.ORG_OP */
      else if ((tmp_str =
              strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                  CONTENT_FEATURES_HEADERS[HEADER_INDEX_OP])) != NULL) {
        GST_LOG_OBJECT (dlna_src, "Found field: %s",
            CONTENT_FEATURES_HEADERS[HEADER_INDEX_OP]);
        op_str = *ptr;
      }
      /* DLNA.ORG_PS */
      else if ((tmp_str =
              strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                  CONTENT_FEATURES_HEADERS[HEADER_INDEX_PS])) != NULL) {
        GST_LOG_OBJECT (dlna_src, "Found field: %s",
            CONTENT_FEATURES_HEADERS[HEADER_INDEX_PS]);
        ps_str = *ptr;
      }
      /* DLNA.ORG_FLAGS */
      else if ((tmp_str =
              strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                  CONTENT_FEATURES_HEADERS[HEADER_INDEX_FLAGS])) != NULL) {
        GST_LOG_OBJECT (dlna_src, "Found field: %s",
            CONTENT_FEATURES_HEADERS[HEADER_INDEX_FLAGS]);
        flags_str = *ptr;
      }
      /* DLNA.ORG_CI */
      else if ((tmp_str =
              strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                  CONTENT_FEATURES_HEADERS[HEADER_INDEX_CI])) != NULL) {
        GST_LOG_OBJECT (dlna_src, "Found field: %s",
            CONTENT_FEATURES_HEADERS[HEADER_INDEX_CI]);
        ci_str = *ptr;
      } else {
        GST_WARNING_OBJECT (dlna_src, "Unrecognized sub field:%s", *ptr);
      }
    }
  }

  if (pn_str != NULL) {
    if (!dlna_src_head_response_parse_profile (dlna_src, head_response, idx,
            pn_str)) {
      GST_WARNING_OBJECT (dlna_src, "Problems parsing profile sub field: %s",
          pn_str);
    }
  }
  if (op_str != NULL) {
    if (!dlna_src_head_response_parse_operations (dlna_src, head_response, idx,
            op_str)) {
      GST_WARNING_OBJECT (dlna_src, "Problems parsing operations sub field: %s",
          op_str);
    }
  }
  if (ps_str != NULL) {
    if (!dlna_src_head_response_parse_playspeeds (dlna_src, head_response, idx,
            ps_str)) {
      GST_WARNING_OBJECT (dlna_src, "Problems parsing playspeeds sub field: %s",
          ps_str);
    }
  }
  if (flags_str != NULL) {
    if (!dlna_src_head_response_parse_flags (dlna_src, head_response, idx,
            flags_str)) {
      GST_WARNING_OBJECT (dlna_src, "Problems parsing flags sub field: %s",
          flags_str);
    }
  }
  if (ci_str != NULL) {
    if (!dlna_src_head_response_parse_conversion_indicator (dlna_src,
            head_response, idx, ci_str)) {
      GST_WARNING_OBJECT (dlna_src,
          "Problems parsing conversion indicator sub field: %s", ci_str);
    }
  }
  g_strfreev (tokens);
  return TRUE;
}

/**
 * Parse DLNA profile identified by DLNA.ORG_PN header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_profile (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  gint ret_code = 0;
  gchar tmp1[256] = { 0 };
  gchar tmp2[256] = { 0 };

  GST_LOG_OBJECT (dlna_src, "Found PN Field: %s", field_str);

  if ((ret_code = sscanf (field_str, "%255[^=]=%255s", tmp1, tmp2)) != 2) {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing DLNA.ORG_PN from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
        HEAD_RESPONSE_HEADERS[idx], field_str, ret_code, tmp1, tmp2);
  } else {
    head_response->content_features->profile = g_strdup (tmp2);
  }
  return TRUE;
}

/**
 * Parse DLNA supported operations sub field identified by DLNA.ORG_OP header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_OP field
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_operations (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  gint ret_code = 0;
  gchar tmp1[256] = { 0 };
  gchar tmp2[256] = { 0 };

  GST_LOG_OBJECT (dlna_src, "Found OP Field: %s", field_str);

  if ((ret_code = sscanf (field_str, "%255[^=]=%255s", tmp1, tmp2)) != 2) {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing DLNA.ORG_OP from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
        HEAD_RESPONSE_HEADERS[idx], field_str, ret_code, tmp1, tmp2);
  } else {
    GST_LOG_OBJECT (dlna_src, "OP Field value: %s", tmp2);

    if (strlen (tmp2) != 2) {
      GST_WARNING_OBJECT (dlna_src,
          "DLNA.ORG_OP from HEAD response sub field %s value: %s, is not at expected len of 2",
          field_str, tmp2);
    } else {
      /* First char represents time seek support */
      if ((tmp2[0] == '0') || (tmp2[0] == '1')) {
        if (tmp2[0] == '0') {
          head_response->content_features->op_time_seek_supported = FALSE;
        } else {
          head_response->content_features->op_time_seek_supported = TRUE;
        }
      } else {
        GST_WARNING_OBJECT (dlna_src,
            "DLNA.ORG_OP Time Seek Flag from HEAD response sub field %s value: %s, is not 0 or 1",
            field_str, tmp2);
      }

      /* Second char represents byte range support */
      if ((tmp2[1] == '0') || (tmp2[1] == '1')) {
        if (tmp2[1] == '0') {
          head_response->content_features->op_range_supported = FALSE;
        } else {
          head_response->content_features->op_range_supported = TRUE;
        }
      } else {
        GST_WARNING_OBJECT (dlna_src,
            "DLNA.ORG_OP Range Flag from HEAD response sub field %s value: %s, is not 0 or 1",
            field_str, tmp2);
      }
    }
  }
  return TRUE;
}

/**
 * Parse DLNA playspeeds sub field identified by DLNA.ORG_PS header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PS field
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_playspeeds (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  gint ret_code = 0;
  gchar tmp1[256] = { 0 };
  gchar tmp2[256] = { 0 };
  gfloat rate = 0;
  int d;
  int n;
  gchar **tokens;
  gchar **ptr;

  GST_LOG_OBJECT (dlna_src, "Found PS Field: %s", field_str);

  if ((ret_code = sscanf (field_str, "%255[^=]=%255s", tmp1, tmp2)) != 2) {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing DLNA.ORG_PS from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
        HEAD_RESPONSE_HEADERS[idx], field_str, ret_code, tmp1, tmp2);
    return FALSE;
  } else {
    GST_LOG_OBJECT (dlna_src, "PS Field value: %s", tmp2);

    /* Tokenize list of comma separated playspeeds */
    tokens = g_strsplit (tmp2, ",", PLAYSPEEDS_MAX_CNT);
    for (ptr = tokens; *ptr; ptr++) {
      if (strlen (*ptr) > 0) {
        GST_LOG_OBJECT (dlna_src, "Found PS: %s", *ptr);

        /* Store string representation to facilitate fractional string conversion */
        head_response->content_features->playspeed_strs
            [head_response->content_features->playspeeds_cnt]
            = g_strdup (*ptr);

        /* Check if this is a non-fractional value */
        if (strstr (*ptr, "/") == NULL) {
          /* Convert str to numeric value */
          if ((ret_code = sscanf (*ptr, "%f", &rate)) != 1) {
            GST_WARNING_OBJECT (dlna_src,
                "Problems converting playspeed %s into numeric value", *ptr);
            return FALSE;
          } else {
            head_response->content_features->
                playspeeds[head_response->content_features->playspeeds_cnt] =
                rate;
          }
        } else {
          /* Handle conversion of fractional string into float, needed when specifying rate */
          if ((ret_code = sscanf (*ptr, "%d/%d", &n, &d)) != 2) {
            GST_WARNING_OBJECT (dlna_src,
                "Problems converting fractional playspeed %s into numeric value",
                *ptr);
            return FALSE;
          } else {
            rate = (gfloat) n / (gfloat) d;

            head_response->content_features->
                playspeeds[head_response->content_features->playspeeds_cnt] =
                rate;
          }
        }
        head_response->content_features->playspeeds_cnt++;
      }
    }
    g_strfreev (tokens);
  }

  return TRUE;
}

/**
 * Parse DLNA flags sub field identified by DLNA.ORG_FLAGS header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_FLAGS field
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_flags (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  gint ret_code = 0;
  gchar tmp1[256] = { 0 };
  gchar tmp2[256] = { 0 };

  GST_LOG_OBJECT (dlna_src, "Found Flags Field: %s", field_str);

  if ((ret_code = sscanf (field_str, "%255[^=]=%255s", tmp1, tmp2)) != 2) {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing DLNA.ORG_FLAGS from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
        HEAD_RESPONSE_HEADERS[idx], field_str, ret_code, tmp1, tmp2);
  } else {
    GST_LOG_OBJECT (dlna_src, "FLAGS Field value: %s", tmp2);

    head_response->content_features->flag_sender_paced_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, SP_FLAG);
    head_response->content_features->flag_limited_time_seek_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, LOP_NPT);
    head_response->content_features->flag_limited_byte_seek_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, LOP_BYTES);
    head_response->content_features->flag_play_container_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2,
        PLAYCONTAINER_PARAM);
    head_response->content_features->flag_so_increasing_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, S0_INCREASING);
    head_response->content_features->flag_sn_increasing_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, SN_INCREASING);
    head_response->content_features->flag_rtsp_pause_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, RTSP_PAUSE);
    head_response->content_features->flag_streaming_mode_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, TM_S);
    head_response->content_features->flag_interactive_mode_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, TM_I);
    head_response->content_features->flag_background_mode_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, TM_B);
    head_response->content_features->flag_stalling_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, HTTP_STALLING);
    head_response->content_features->flag_dlna_v15_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, DLNA_V15_FLAG);
    head_response->content_features->flag_link_protected_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, LP_FLAG);
    head_response->content_features->flag_full_clear_text_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2,
        CLEARTEXTBYTESEEK_FULL_FLAG);
    head_response->content_features->flag_limited_clear_text_set =
        dlna_src_head_response_is_flag_set (dlna_src, tmp2, LOP_CLEARTEXTBYTES);
  }

  return TRUE;
}

/**
 * Parse DLNA conversion indicator sub field identified by DLNA.ORG_CI header.
 *
 * @param   dlna_src    this element
 * @param   idx         index into array of header strings
 * @param   field_str   sub field string containing DLNA.ORG_CI field
 *
 * @return  TRUE
 */
static gboolean
dlna_src_head_response_parse_conversion_indicator (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_str)
{
  gint ret_code = 0;
  gchar header[256] = { 0 };
  gchar value[256] = { 0 };

  GST_LOG_OBJECT (dlna_src, "Found CI Field: %s", field_str);

  if ((ret_code = sscanf (field_str, "%255[^=]=%s", header, value)) != 2) {
    GST_WARNING_OBJECT (dlna_src,
        "Problems parsing DLNA.ORG_CI from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s",
        HEAD_RESPONSE_HEADERS[idx], field_str, ret_code, header, value);
  } else {
    if (value[0] == '1') {
      head_response->content_features->is_converted = TRUE;
    } else {
      head_response->content_features->is_converted = FALSE;
    }
  }

  return TRUE;
}

/**
 * Parse content type identified by CONTENT-TYPE header.  Includes additional
 * subfields when content is DTCP encrypted.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_content_type (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, gint idx, const gchar * field_value)
{
  gint ret_code = 0;
  gchar tmp1[32] = { 0 };
  gchar tmp2[32] = { 0 };
  gchar tmp3[32] = { 0 };
  gchar **tokens = NULL;
  gchar *tmp_str;
  gchar **ptr;

  GST_LOG_OBJECT (dlna_src, "Found Content Type Field: %s", field_value);

  /* If not DTCP content, this field is mime-type */
  if (strstr (g_ascii_strup (field_value, strlen (field_value)),
          "DTCP") == NULL) {
    head_response->content_type = g_strdup (field_value);
  } else {
    /* DTCP related info in subfields
       Split CONTENT-TYPE into following sub-fields using ";" as deliminator

       DTCP1HOST
       DTCP1PORT
       CONTENTFORMAT
     */
    tokens = g_strsplit (field_value, ";", 0);
    for (ptr = tokens; *ptr; ptr++) {
      if (strlen (*ptr) > 0) {
        /* DTCP1HOST */
        if ((tmp_str =
                strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                    CONTENT_TYPE_HEADERS[HEADER_INDEX_DTCP_HOST])) != NULL) {
          GST_LOG_OBJECT (dlna_src, "Found field: %s",
              CONTENT_TYPE_HEADERS[HEADER_INDEX_DTCP_HOST]);
          head_response->dtcp_host = g_strdup ((strstr (tmp_str, "=") + 1));
        }
        /* DTCP1PORT */
        else if ((tmp_str =
                strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                    CONTENT_TYPE_HEADERS[HEADER_INDEX_DTCP_PORT])) != NULL) {
          if ((ret_code = sscanf (tmp_str, "%31[^=]=%d", tmp1,
                      &head_response->dtcp_port)) != 2) {
            GST_WARNING_OBJECT (dlna_src,
                "Problems parsing DTCP PORT from HEAD response field header %s, value: %s, retcode: %d, tmp: %s",
                HEAD_RESPONSE_HEADERS[idx], tmp_str, ret_code, tmp1);
          } else {
            GST_LOG_OBJECT (dlna_src, "Found field: %s",
                CONTENT_TYPE_HEADERS[HEADER_INDEX_DTCP_PORT]);
          }
        }
        /* CONTENTFORMAT */
        else if ((tmp_str =
                strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                    CONTENT_TYPE_HEADERS[HEADER_INDEX_CONTENT_FORMAT])) !=
            NULL) {

          if ((ret_code = sscanf (tmp_str, "%31[^=]=%31s", tmp1, tmp2)) != 2) {
            GST_WARNING_OBJECT (dlna_src,
                "Problems parsing DTCP CONTENT FORMAT from HEAD response field header %s, value: %s, retcode: %d, tmp: %s, %s, %s",
                HEAD_RESPONSE_HEADERS[idx], tmp_str, ret_code, tmp1, tmp2,
                tmp3);
          } else {
            GST_LOG_OBJECT (dlna_src, "Found field: %s",
                CONTENT_TYPE_HEADERS[HEADER_INDEX_CONTENT_FORMAT]);
            head_response->content_type = g_strdup (tmp2);
          }
        }
        /*  APPLICATION/X-DTCP1a */
        else if ((tmp_str =
                strstr (g_ascii_strup (*ptr, strlen (*ptr)),
                    CONTENT_TYPE_HEADERS[HEADER_INDEX_APP_DTCP])) != NULL) {
          /* Ignoring this field */
        } else {
          GST_WARNING_OBJECT (dlna_src, "Unrecognized sub field:%s", *ptr);
        }
      }
    }
    g_strfreev (tokens);
  }

  return TRUE;
}

/**
 * Utility method which determines if a given flag is set in the flags string.
 *
 * @param dlna_src  this element
 * @param flagsStr  the fourth field of a protocolInfo string
 * @param flag      determines if this flag is set in supplied string
 *
 * @return TRUE if flag is set, FALSE otherwise
 */
static gboolean
dlna_src_head_response_is_flag_set (GstDlnaSrc * dlna_src,
    const gchar * flags_str, gint flag)
{
  gint64 value;
  gchar *tmp_str;
  gsize len;

  if ((flags_str == NULL) || (strlen (flags_str) <= RESERVED_FLAGS_LENGTH)) {
    GST_WARNING_OBJECT (dlna_src,
        "FLAGS Field value null or too short : %s", flags_str);
    return FALSE;
  }
  /* Drop reserved flags off of value (prepended zeros will be ignored) */
  tmp_str = g_strdup (flags_str);
  len = strlen (tmp_str);
  tmp_str[len - RESERVED_FLAGS_LENGTH] = '\0';

  /* Convert into long using hexidecimal format */
  value = strtoll (tmp_str, NULL, 16);

  g_free (tmp_str);

  return (value & flag) == flag;
}

/**
 * Format HEAD response structure into string representation.
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static void
dlna_src_head_response_struct_to_str (GstDlnaSrc * dlna_src,
    GstDlnaSrcHeadResponse * head_response, GString * struct_str)
{
  gint i;

  GST_DEBUG_OBJECT (dlna_src, "Formatting HEAD Response struct");

  dlna_src_struct_append_header_value_bool (struct_str,
      "\nByte Seek Supported?: ", dlna_src->byte_seek_supported);

  dlna_src_struct_append_header_value_guint64 (struct_str,
      "Byte Start: ", dlna_src->byte_start);

  dlna_src_struct_append_header_value_guint64 (struct_str,
      "Byte End: ", dlna_src->byte_end);

  dlna_src_struct_append_header_value_guint64 (struct_str,
      "Byte Total: ", dlna_src->byte_total);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Time Seek Supported?: ", dlna_src->time_seek_supported);

  if (dlna_src->time_seek_supported) {
    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "NPT Start: ", dlna_src->npt_start_str, dlna_src->npt_start_nanos);
    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "NPT End: ", dlna_src->npt_end_str, dlna_src->npt_end_nanos);
    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "NPT Total: ", dlna_src->npt_duration_str,
        dlna_src->npt_duration_nanos);
  }

  dlna_src_struct_append_header_value_str (struct_str,
      "\nHTTP Version: ", head_response->http_rev);

  dlna_src_struct_append_header_value_guint (struct_str,
      "HEAD Ret Code: ", head_response->ret_code);

  dlna_src_struct_append_header_value_str (struct_str,
      "HEAD Ret Msg: ", head_response->ret_msg);

  dlna_src_struct_append_header_value_str (struct_str,
      "Server: ", head_response->server);

  dlna_src_struct_append_header_value_str (struct_str,
      "Date: ", head_response->date);

  dlna_src_struct_append_header_value_guint64 (struct_str,
      "Content Length: ", head_response->content_length);

  dlna_src_struct_append_header_value_str (struct_str,
      "Accept Ranges: ", head_response->accept_ranges);

  dlna_src_struct_append_header_value_str (struct_str,
      "Content Type: ", head_response->content_type);

  if (head_response->dtcp_host != NULL) {

    dlna_src_struct_append_header_value_str (struct_str,
        "DTCP Host: ", head_response->dtcp_host);

    dlna_src_struct_append_header_value_guint (struct_str,
        "DTCP Port: ", head_response->dtcp_port);
  }

  dlna_src_struct_append_header_value_str (struct_str,
      "HTTP Transfer Encoding: ", head_response->transfer_encoding);

  dlna_src_struct_append_header_value_str (struct_str,
      "DLNA Transfer Mode: ", head_response->transfer_mode);

  if (head_response->time_seek_npt_start_str != NULL) {
    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "Time Seek NPT Start: ",
        head_response->time_seek_npt_start_str,
        head_response->time_seek_npt_start);

    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "Time Seek NPT End: ",
        head_response->time_seek_npt_end_str, head_response->time_seek_npt_end);

    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "Time Seek NPT Duration: ",
        head_response->time_seek_npt_duration_str,
        head_response->time_seek_npt_duration);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Byte Seek Start: ", head_response->time_byte_seek_start);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Byte Seek End: ", head_response->time_byte_seek_end);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Byte Seek Total: ", head_response->time_byte_seek_total);
  }
  if (head_response->dtcp_range_total != 0) {
    dlna_src_struct_append_header_value_guint64 (struct_str,
        "DTCP Range Start: ", head_response->dtcp_range_start);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "DTCP Range End: ", head_response->dtcp_range_end);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "DTCP Range Total: ", head_response->dtcp_range_total);
  }

  if (head_response->content_range_total != 0) {
    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Content Range Start: ", head_response->content_range_start);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Content Range End: ", head_response->content_range_end);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Content Range Total: ", head_response->content_range_total);
  }

  if (head_response->available_seek_npt_start_str != NULL) {
    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "Available Seek NPT Start: ",
        head_response->available_seek_npt_start_str,
        head_response->available_seek_npt_start);

    dlna_src_struct_append_header_value_str_guint64 (struct_str,
        "Available Seek NPT End: ",
        head_response->available_seek_npt_end_str,
        head_response->available_seek_npt_end);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Available byte Seek Start: ", head_response->available_seek_start);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Available byte Seek End: ", head_response->available_seek_end);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Available clear text Seek Start: ",
        head_response->available_seek_cleartext_start);

    dlna_src_struct_append_header_value_guint64 (struct_str,
        "Available clear text Seek End: ",
        head_response->available_seek_cleartext_end);
  }

  dlna_src_struct_append_header_value_str (struct_str,
      "DLNA Profile: ", head_response->content_features->profile);

  dlna_src_struct_append_header_value_guint (struct_str,
      "Supported Playspeed Cnt: ",
      head_response->content_features->playspeeds_cnt);

  struct_str = g_string_append (struct_str, "Playspeeds: ");
  for (i = 0; i < head_response->content_features->playspeeds_cnt; i++) {
    if (i > 0)
      struct_str = g_string_append (struct_str, ", ");
    struct_str =
        g_string_append (struct_str,
        head_response->content_features->playspeed_strs[i]);
  }
  struct_str = g_string_append (struct_str, "\n");

  dlna_src_struct_append_header_value_bool (struct_str,
      "Conversion Indicator?: ", head_response->content_features->is_converted);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Time Seek Supported Flag?: ",
      head_response->content_features->op_time_seek_supported);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Byte Seek Supported Flag?: ",
      head_response->content_features->op_range_supported);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Sender Paced?: ",
      head_response->content_features->flag_sender_paced_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Limited Time Seek?: ",
      head_response->content_features->flag_limited_time_seek_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Limited Byte Seek?: ",
      head_response->content_features->flag_limited_byte_seek_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Play Container?: ",
      head_response->content_features->flag_play_container_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "S0 Increasing?: ",
      head_response->content_features->flag_so_increasing_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Sn Increasing?: ",
      head_response->content_features->flag_sn_increasing_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "RTSP Pause?: ", head_response->content_features->flag_rtsp_pause_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Streaming Mode Supported?: ",
      head_response->content_features->flag_streaming_mode_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Interactive Mode Supported?: ",
      head_response->content_features->flag_interactive_mode_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Background Mode Supported?: ",
      head_response->content_features->flag_background_mode_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Connection Stalling Supported?: ",
      head_response->content_features->flag_stalling_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "DLNA Ver. 1.5?: ", head_response->content_features->flag_dlna_v15_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Link Protected?: ",
      head_response->content_features->flag_link_protected_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Full Clear Text?: ",
      head_response->content_features->flag_full_clear_text_set);

  dlna_src_struct_append_header_value_bool (struct_str,
      "Limited Clear Text?: ",
      head_response->content_features->flag_limited_clear_text_set);
}

/**
 * Utility function which formats a string and appends to the supplied string
 * using the title and either TRUE or FALSE depending on value.
 *
 * @param   struct_str          append values to this string buffer
 * @param   struct_str_max_size max size in bytes of the string buffer
 * @param   title               append string starting with this as title
 * @param   value               append this value
 *
 * @return  TRUE if appending string was successful, false otherwise
 */
static void
dlna_src_struct_append_header_value_bool (GString * struct_str,
    gchar * title, gboolean value)
{
  struct_str = g_string_append (struct_str, title);
  struct_str = g_string_append (struct_str, (value) ? "TRUE\n" : "FALSE\n");
}

/**
 * Utility function which formats a string and appends to the supplied string
 * using the title and the value itself.
 *
 * @param   struct_str          append values to this string buffer
 * @param   struct_str_max_size max size in bytes of the string buffer
 * @param   title               append string starting with this as title
 * @param   value               append this value
 *
 * @return  TRUE if appending string was successful, false otherwise
 */
static void
dlna_src_struct_append_header_value_str (GString * struct_str,
    gchar * title, gchar * value)
{
  if (value != NULL) {
    struct_str = g_string_append (struct_str, title);
    struct_str = g_string_append (struct_str, value);
    struct_str = g_string_append (struct_str, "\n");
  }
}

/**
 * Utility function which formats a string and appends to the supplied string
 * using the title and the value itself.
 *
 * @param   struct_str          append values to this string buffer
 * @param   struct_str_max_size max size in bytes of the string buffer
 * @param   title               append string starting with this as title
 * @param   value               append this value
 *
 * @return  TRUE if appending string was successful, false otherwise
 */
static void
dlna_src_struct_append_header_value_guint64 (GString * struct_str,
    gchar * title, guint64 value)
{
  gchar tmp_str[32] = { 0 };
  gsize tmp_str_max_size = 32;

  struct_str = g_string_append (struct_str, title);
  g_snprintf (tmp_str, tmp_str_max_size, "%" G_GUINT64_FORMAT, value);
  struct_str = g_string_append (struct_str, tmp_str);
  struct_str = g_string_append (struct_str, "\n");
}

/**
 * Utility function which formats a string and appends to the supplied string
 * using the title and the value itself.
 *
 * @param   struct_str          append values to this string buffer
 * @param   struct_str_max_size max size in bytes of the string buffer
 * @param   title               append string starting with this as title
 * @param   value               append this value
 *
 * @return  TRUE if appending string was successful, false otherwise
 */
static void
dlna_src_struct_append_header_value_guint (GString * struct_str,
    gchar * title, guint value)
{
  gchar tmp_str[32] = { 0 };
  gsize tmp_str_max_size = 32;

  struct_str = g_string_append (struct_str, title);
  g_snprintf (tmp_str, tmp_str_max_size, "%d", value);
  struct_str = g_string_append (struct_str, tmp_str);
  struct_str = g_string_append (struct_str, "\n");
}

/**
 * Utility function which formats a string and appends to the supplied string
 * using the title, the value as a string and the value itself.
 *
 * @param   struct_str          append values to this string buffer
 * @param   struct_str_max_size max size in bytes of the string buffer
 * @param   title               append string starting with this as title
 * @param   value_str           append this string representing value as formatted string
 * @param   value               append this value
 *
 * @return  TRUE if appending string was successful, false otherwise
 */
static void
dlna_src_struct_append_header_value_str_guint64 (GString * struct_str,
    gchar * title, gchar * value_str, guint64 value)
{
  gchar tmp_str[32] = { 0 };
  gsize tmp_str_max_size = 32;

  if (value_str) {
    struct_str = g_string_append (struct_str, title);
    struct_str = g_string_append (struct_str, value_str);
    g_snprintf (tmp_str, tmp_str_max_size, " - %" G_GUINT64_FORMAT, value);
    struct_str = g_string_append (struct_str, tmp_str);
    struct_str = g_string_append (struct_str, "\n");
  }
}

/**
 * Utility function which uses server info from time seek range in order
 * to convert supplied byte position to corresponding normal play time (npt) in nanoseconds.
 *
 * @param   dlna_src    this element instance
 * @param   bytes       byte position to convert to npt in nanoseconds
 * @param   npt_nanos   npt which represents supplied byte position
 *
 * @return  TRUE if conversion was successful, false otherwise
 */
static gboolean
dlna_src_convert_bytes_to_npt_nanos (GstDlnaSrc * dlna_src, guint64 bytes,
    guint64 * npt_nanos)
{
  /* Unable to issue time seek range header with bytes and get back npt
     so just using the time seek range results from previous head to estimate
     what npt would be for supplied byte
   */
  *npt_nanos = (bytes * dlna_src->npt_duration_nanos) / dlna_src->byte_total;

  GST_INFO_OBJECT (dlna_src,
      "Converted %" G_GUINT64_FORMAT " bytes to %" GST_TIME_FORMAT
      " npt using total bytes=%" G_GUINT64_FORMAT " total npt=%"
      GST_TIME_FORMAT, bytes, GST_TIME_ARGS (*npt_nanos),
      dlna_src->byte_total, GST_TIME_ARGS (dlna_src->npt_duration_nanos));

  return TRUE;
}

/**
 * Utility function which uses server info from time seek range in order
 * to convert supplied normal play time (npt) in nanoseconds to corresponding byte position.
 *
 * @param   dlna_src    this element instance
 * @param   npt_nanos   npt in nanoseconds to convert to byte position
 * @param   bytes       byte position which represents supplied npt nanos
 *
 * @return  TRUE if conversion was successful, false otherwise
 */
static gboolean
dlna_src_convert_npt_nanos_to_bytes (GstDlnaSrc * dlna_src, guint64 npt_nanos,
    guint64 * bytes)
{
  /* Issue head to get conversion info */
  GstDlnaSrcHeadResponse *head_response = NULL;
  gchar head_request_str[MAX_HTTP_BUF_SIZE] = { 0 };
  gint head_request_max_size = MAX_HTTP_BUF_SIZE;
  gchar tmpStr[32] = { 0 };
  gsize tmp_str_max_size = 32;
  gchar *time_seek_head_request_headers[][2] =
      { {HEADER_TIME_SEEK_RANGE_TITLE, HEADER_TIME_SEEK_RANGE_VALUE} };
  gsize time_seek_head_request_headers_array_size = 1;

  /* Convert start time from nanos into secs string */
  guint64 npt_secs = npt_nanos / GST_SECOND;

  if (!dlna_src_head_response_init_struct (dlna_src, &head_response)) {
    GST_ERROR_OBJECT (dlna_src,
        "Problems initializing struct to store HEAD response");
    return FALSE;
  }

  /* Include time seek range to get conversion values */
  if (g_strlcat (head_request_str, "TimeSeekRange.dlna.org: npt=",
          head_request_max_size) >= head_request_max_size)
    goto overflow;

  /* Include starting npt (since bytes are only included in response) */
  g_snprintf (tmpStr, tmp_str_max_size, "%" G_GUINT64_FORMAT, npt_secs);
  if (g_strlcat (head_request_str, tmpStr,
          head_request_max_size) >= head_request_max_size)
    goto overflow;

  if (g_strlcat (head_request_str, "-",
          head_request_max_size) >= head_request_max_size)
    goto overflow;
  if (g_strlcat (head_request_str, CRLF,
          head_request_max_size) >= head_request_max_size)
    goto overflow;

  if (!dlna_src_soup_issue_head (dlna_src,
          time_seek_head_request_headers_array_size,
          time_seek_head_request_headers, head_response, FALSE)) {
    GST_WARNING_OBJECT (dlna_src, "Problems with HEAD request");
    return FALSE;
  }

  *bytes = head_response->time_byte_seek_start;
  GST_INFO_OBJECT (dlna_src,
      "Converted %" GST_TIME_FORMAT " npt to %" G_GUINT64_FORMAT " bytes",
      GST_TIME_ARGS (npt_nanos), *bytes);

  dlna_src_head_response_free (dlna_src, head_response);

  return TRUE;

overflow:
  GST_ERROR_OBJECT (dlna_src,
      "Overflow - exceeded head request string size of: %d",
      head_request_max_size);
  return FALSE;
}

/**
 * Convert supplied string which represents normal play time (npt) into
 * nanoseconds.  The format of NPT is as follows:
 *
 * npt time  = npt sec | npt hhmmss
 *
 * npt sec   = 1*DIGIT [ "." 1*3DIGIT ]
 * npthhmmss = npthh":"nptmm":"nptss["."1*3DIGIT]
 * npthh     = 1*DIGIT     ; any positive number
 * nptmm     = 1*2DIGIT    ; 0-59
 * nptss     = 1*2DIGIT    ; 0-59
 *
 * @param	dlna_src			this element, needed for logging
 * @param	string				normal play time string to convert
 * @param	media_time_nanos	npt string value converted into nanoseconds
 *
 * @return	true if no problems encountered, false otherwise
 */
static gboolean
dlna_src_npt_to_nanos (GstDlnaSrc * dlna_src, gchar * string,
    guint64 * media_time_nanos)
{
  gboolean ret = FALSE;

  guint hours = 0;
  guint mins = 0;
  float secs = 0.;

  if (sscanf (string, "%u:%u:%f", &hours, &mins, &secs) == 3) {
    /* Long form */
    *media_time_nanos =
        ((hours * 60 * 60 * 1000) + (mins * 60 * 1000) +
        (secs * 1000)) * 1000000L;
    ret = TRUE;

    GST_LOG_OBJECT (dlna_src,
        "Convert npt str %s hr=%d:mn=%d:s=%f into nanosecs: %"
        G_GUINT64_FORMAT, string, hours, mins, secs, *media_time_nanos);
  } else if (sscanf (string, "%f", &secs) == 1) {
    /* Short form */
    *media_time_nanos = (secs * 1000) * 1000000L;
    ret = TRUE;
    GST_LOG_OBJECT (dlna_src,
        "Convert npt str %s secs=%f into nanosecs: %"
        G_GUINT64_FORMAT, string, secs, *media_time_nanos);
  } else {
    GST_ERROR_OBJECT (dlna_src,
        "Problems converting npt str into nanosecs: %s", string);
  }

  return ret;
}

/**
 * Formats given nanoseconds into string which represents normal play time (npt).
 * The format of NPT is as follows:
 *
 * npt time  = npt hhmmss
 *
 * npthhmmss = npthh":"nptmm":"nptss["."1*3DIGIT]
 * npthh     = 1*DIGIT     ; any positive number
 * nptmm     = 1*2DIGIT    ; 0-59
 * nptss     = 1*2DIGIT    ; 0-59
 *
 * @param   dlna_src            this element, needed for logging
 * @param   media_time_nanos    nanoseconds to be formatted into string
 * @param   string              returning normal play time string
 */
static void
dlna_src_nanos_to_npt (GstDlnaSrc * dlna_src, guint64 media_time_nanos,
    GString * npt_str)
{
  guint64 media_time_ms = media_time_nanos / GST_MSECOND;
  gint hours = media_time_ms / (60 * 60 * 1000);
  gint remainder = media_time_ms % (60 * 60 * 1000);
  gint minutes = remainder / (60 * 1000);
  float seconds = roundf ((remainder % (60 * 1000))) / 1000.0;

  g_string_append_printf (npt_str, "%d:%02d:%02.3f", hours, minutes, seconds);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dlna_src_init (GstPlugin * dlna_src)
{
  GST_DEBUG_CATEGORY_INIT (gst_dlna_src_debug, "dlnasrc", 0,
      "MPEG+DLNA Player");

  /* *TODO* - setting  + 1 forces this element to get selected as src by playsrc2 */
  return gst_element_register ((GstPlugin *) dlna_src, "dlnasrc",
      GST_RANK_PRIMARY + 101,
      /*                  GST_RANK_PRIMARY-1, */
      GST_TYPE_DLNA_SRC);
}

/* gstreamer looks for this structure to register eisss
 *
 * exchange the string 'Template eiss' with your eiss description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlnasrc,
    "DLNA HTTP Source",
    (GstPluginInitFunc) dlna_src_init,
    VERSION, "BSD", "gst-cablelabs_ri", "http://gstreamer.net/")
