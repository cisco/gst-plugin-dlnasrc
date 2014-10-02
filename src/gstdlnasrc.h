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

struct _GstDlnaSrc
{
    GstBin bin;
    GstElement* http_src;
    GstElement* dtcp_decrypter;

    GstPad* src_pad;

    guint dtcp_blocksize;
    gchar* dtcp_key_storage;

    gchar *dlna_uri;
    gchar *http_uri;

    SoupSession *soup_session;
    SoupMessage *soup_msg;

    GstDlnaSrcHeadResponse* server_info;

    gfloat rate;
    gfloat requested_rate;
    GstFormat requested_format;
    guint64 requested_start;
    guint64 requested_stop;

    guint32 time_seek_seqnum;
    guint64 time_seek_event_start;
    gboolean handled_time_seek_seqnum;

    gboolean is_uri_initialized;
    gboolean is_live;
    gboolean is_encrypted;

    gboolean byte_seek_supported;
    guint64 byte_start;
    guint64 byte_end;
    guint64 byte_total;

    gboolean time_seek_supported;
    gchar*  npt_start_str;
    gchar*  npt_end_str;
    gchar* npt_duration_str;
    guint64 npt_start_nanos;
    guint64 npt_end_nanos;
    guint64 npt_duration_nanos;

    gboolean forward_event;

    gint64   pause_pos;
    gboolean in_tsb;
    gboolean seek_to_play;

};

struct _GstDlnaSrcHeadResponse
{
    gchar* http_rev;
    gint http_rev_idx;

    guint ret_code;
    gint ret_code_idx;

    gchar* ret_msg;
    gint ret_msg_idx;

    guint64 content_length;
    gint content_length_idx;

    gchar* accept_ranges;
    gint accept_ranges_idx;
    gboolean accept_byte_ranges;

    guint64 content_range_start;
    guint64 content_range_end;
    guint64 content_range_total;
    gint content_range_idx;

    gint time_seek_idx;

    gchar* time_seek_npt_start_str;
    gchar* time_seek_npt_end_str;
    gchar* time_seek_npt_duration_str;
    guint64 time_seek_npt_start;
    guint64 time_seek_npt_end;
    guint64 time_seek_npt_duration;
    gint npt_seek_idx;

    guint64 time_byte_seek_start;
    guint64 time_byte_seek_end;
    guint64 time_byte_seek_total;
    gint byte_seek_idx;

    gint clear_text_idx;

    guint64 dtcp_range_start;
    guint64 dtcp_range_end;
    guint64 dtcp_range_total;
    gint dtcp_range_idx;

    gint available_range_idx;
    gchar* available_seek_npt_start_str;
    gchar* available_seek_npt_end_str;
    guint64 available_seek_npt_start;
    guint64 available_seek_npt_end;
    guint64 available_seek_start;
    guint64 available_seek_end;
    guint64 available_seek_cleartext_start;
    guint64 available_seek_cleartext_end;

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

    gint  conversion_idx;
    gboolean is_converted;
};

struct _GstDlnaSrcClass
{
    GstBinClass parent_class;
};

GType gst_dlna_src_get_type (void);

G_END_DECLS

#endif /* __GST_DLNA_SRC_H__ */
