/* GStreamer dlna src
 * Copyright (C) 2013 CableLabs, Louisville, CO 80027
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DLNA_SRC_H__
#define __GST_DLNA_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_DLNA_SRC \
        (gst_dlna_src_get_type())
#define GST_DLNA_SRC(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DLNA_SRC,GstDlnaSrc))
#define GST_DLNA_SRC_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DLNA_SRC,GstDlnaSrcClass))
#define GST_IS_DLNA_SRC(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DLNA_SRC))
#define GST_IS_DLNA_SRC_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DLNA_SRC))

#define PLAYSPEEDS_MAX_CNT 64

typedef struct _GstDlnaSrc GstDlnaSrc;
typedef struct _GstDlnaSrcClass GstDlnaSrcClass;

typedef struct _GstDlnaSrcHeadResponse GstDlnaSrcHeadResponse;
typedef struct _GstDlnaSrcHeadResponseContentFeatures GstDlnaSrcHeadResponseContentFeatures;

/**
 * GstDlnaSrc:
 *
 * High-level dlna element
 */
struct _GstDlnaSrc
{
    GstBin bin;
    GstElement* http_src;
    GstElement* dtcp_decrypter;

    GstPad* src_pad;

    GstElement* pipeline;
    GstBus* bus;

    // Name used to identify this element as source in playsrc2 logging
    gchar* cl_name;

    // DTCP Key Storage
    gchar* dtcp_key_storage;

    // Stream info
    gchar *uri;

    // Socket params used to issue HEAD request
    gchar *uri_addr;
    guint uri_port;
    gint sock;

    gchar *head_request_str;
    gchar *head_response_str;
    GstDlnaSrcHeadResponse* head_response;

    // Current playback rate
    gfloat rate;

    // *TODO* - need to support this eventually,
    // indication if the pipeline is live
    //gboolean is_live;

    // Requested change info
    gboolean requested_change;

    gfloat requested_rate;
    GstFormat requested_format;
    guint64 requested_start;
    guint64 requested_stop;

    gboolean event_received;
    GMutex event_mutex;
    GCond event_cond;
    gulong event_probe;
};

struct _GstDlnaSrcHeadResponse
{
    gchar *head_response_uppercase_str;
    gchar* struct_str;

    gchar* http_rev;
    gint http_rev_idx;

    gint ret_code;
    gint ret_code_idx;

    gchar* ret_msg;
    gint ret_msg_idx;

    const gchar* time_seek_hdr;
    gint time_seek_idx;

    gchar* time_seek_npt_start_str;
    gchar* time_seek_npt_end_str;
    gchar* time_seek_npt_duration_str;
    guint64 time_seek_npt_start;
    guint64 time_seek_npt_end;
    guint64 time_seek_npt_duration;
    gint npt_seek_idx;

    guint64 byte_seek_start;
    guint64 byte_seek_end;
    guint64 byte_seek_total;
    gint byte_seek_idx;

    guint64 dtcp_range_start;
    guint64 dtcp_range_end;
    guint64 dtcp_range_total;
    gint dtcp_range_idx;

    gchar* transfer_mode;
    gint transfer_mode_idx;

    gchar* transfer_encoding;
    gint transfer_encoding_idx;

    gchar* date;
    gint date_idx;

    gchar* server;
    gint server_idx;

    gchar* content_type;
    gint content_type_idx;

    gchar* dtcp_host;
    gint dtcp_host_idx;
    guint dtcp_port;
    gint dtcp_port_idx;
    gint content_format_idx;

    GstDlnaSrcHeadResponseContentFeatures* content_features;
    gint  content_features_idx;
};

struct _GstDlnaSrcHeadResponseContentFeatures
{
    gint  profile_idx;
    gchar* profile;

    gint  operations_idx;
    gboolean op_time_seek_supported;
    gboolean op_range_supported;

    gint playspeeds_idx;
    guint playspeeds_cnt;
    gchar* playspeed_strs[PLAYSPEEDS_MAX_CNT];
    gfloat playspeeds[PLAYSPEEDS_MAX_CNT];

    gint  flags_idx;
    gboolean flag_sender_paced_set;
    gboolean flag_limited_time_seek_set;
    gboolean flag_limited_byte_seek_set;
    gboolean flag_play_container_set;
    gboolean flag_so_increasing_set;
    gboolean flag_sn_increasing_set;
    gboolean flag_rtsp_pause_set;
    gboolean flag_streaming_mode_set;
    gboolean flag_interactive_mode_set;
    gboolean flag_background_mode_set;
    gboolean flag_stalling_set;
    gboolean flag_dlna_v15_set;
    gboolean flag_link_protected_set;
    gboolean flag_full_clear_text_set;
    gboolean flag_limited_clear_text_set;
};

struct _GstDlnaSrcClass
{
    GstBinClass parent_class;
};

GType gst_dlna_src_get_type (void);

G_END_DECLS

#endif /* __GST_DLNA_SRC_H__ */
