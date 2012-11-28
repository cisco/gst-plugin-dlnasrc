
#ifndef __GST_DLNA_BIN_H__
#define __GST_DLNA_BIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DLNA_BIN \
		(gst_dlna_bin_get_type())
#define GST_DLNA_BIN(obj) \
		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DLNA_BIN,GstDlnaBin))
#define GST_DLNA_BIN_CLASS(klass) \
		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DLNA_BIN,GstDlnaBinClass))
#define GST_IS_DLNA_BIN(obj) \
		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DLNA_BIN))
#define GST_IS_DLNA_BIN_CLASS(klass) \
		(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DLNA_BIN))

#define VOLUME_MAX_DOUBLE 10.0
#define PLAYSPEEDS_MAX_CNT 64

typedef struct _GstDlnaBin GstDlnaBin;
typedef struct _GstDlnaBinClass GstDlnaBinClass;

typedef struct _GstDlnaBinHeadResponse GstDlnaBinHeadResponse;
typedef struct _GstDlnaBinHeadResponseContentFeatures GstDlnaBinHeadResponseContentFeatures;

/**
 * GstDlnaBin:
 *
 * High-level dlna element
 */
struct _GstDlnaBin
{
	GstBin bin;
	GstElement* http_src;
	GstElement* dtcp_decrypter;

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
	GstDlnaBinHeadResponse* head_response;

	// indication if the pipeline is live
	//gboolean is_live;
};

struct _GstDlnaBinHeadResponse
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

	gchar* time_seek_npt_start;
	gchar* time_seek_npt_end;
	gchar* time_seek_npt_duration;
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

	GstDlnaBinHeadResponseContentFeatures* content_features;
	gint  content_features_idx;
};

struct _GstDlnaBinHeadResponseContentFeatures
{
	gint  profile_idx;
	gchar* profile;

	gint  operations_idx;
	gboolean op_time_seek_supported;
	gboolean op_range_supported;

	gint playspeeds_idx;
	guint playspeeds_cnt;
	gchar* playspeeds[PLAYSPEEDS_MAX_CNT];

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

struct _GstDlnaBinClass
{
	GstBinClass parent_class;

};

GType gst_dlna_bin_get_type (void);

G_END_DECLS

#endif /* __GST_DLNA_BIN_H__ */
