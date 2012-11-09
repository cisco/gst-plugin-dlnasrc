
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

typedef struct _GstDlnaBin GstDlnaBin;
typedef struct _GstDlnaBinClass GstDlnaBinClass;

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
	char *uri;

	// Socket params used to issue HEAD request
	gchar *uri_addr;
	guint uri_port;
	int sock;

	gchar *head_request_str;
	gchar *head_response_str;

	// Requested URI content info
	gboolean is_dtcp_encrypted;

	// indication if the pipeline is live
	//gboolean is_live;
};


struct _GstDlnaBinClass
{
	GstBinClass parent_class;

};

GType gst_dlna_bin_get_type (void);

G_END_DECLS

#endif /* __GST_DLNA_BIN_H__ */
