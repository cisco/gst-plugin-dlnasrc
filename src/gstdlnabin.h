
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
#define PLAYSPEEDS_MAX 64

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

	// Stream info
	gchar *uri;

	// Socket params used to issue HEAD request
	gchar *uri_addr;
	guint uri_port;
	gint sock;

	gchar *head_request_str;
	gchar *head_response_str;
	GstDlnaBinHeadResponse* head_response;

	// Requested URI content info
	gboolean is_dtcp_encrypted;

	// indication if the pipeline is live
	//gboolean is_live;
};

struct _GstDlnaBinHeadResponse
{
	gchar* http_rev;
	gint ret_code;
	gchar* ret_msg;
	gchar* time_seek_npt_start;
	gchar* time_seek_npt_end;
	gint64 byte_seek_start;
	gint64 byte_seek_end;
	gchar* transfer_mode;
	gchar* transfer_encoding;
	gchar* date;
	gchar* server;
	gchar* content_type;
	gchar* struct_str;
	GstDlnaBinHeadResponseContentFeatures* content_features;
};

struct _GstDlnaBinHeadResponseContentFeatures
{
	gchar* profile;
	gboolean op_time_seek_supported;
	gboolean op_range_supported;

	guint playspeeds_cnt;
	gfloat playspeeds[PLAYSPEEDS_MAX];

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
