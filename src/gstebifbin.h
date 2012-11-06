
#ifndef __GST_EBIF_BIN_H__
#define __GST_EBIF_BIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EBIF_BIN \
  (gst_ebif_bin_get_type())
#define GST_EBIF_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EBIF_BIN,GstEbifBin))
#define GST_EBIF_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EBIF_BIN,GstEbifBinClass))
#define GST_IS_EBIF_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EBIF_BIN))
#define GST_IS_EBIF_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EBIF_BIN))

#define VOLUME_MAX_DOUBLE 10.0

typedef struct _GstEbifBin GstEbifBin;
typedef struct _GstEbifBinClass GstEbifBinClass;

/**
 * GstEbifBin:
 *
 * High-level ebif element
 */
struct _GstEbifBin
{
  GstBin bin;

  // the configurable elements
  //GstElement *fakesink;
  //GstElement *audio_sink;
  //GstElement *video_sink;

  // Volume
  //GstElement *volume_element;
  //gfloat volume;

  // Stream info
  //char *uri;
  char *video_pid;
  char *audio_pid;
  char *ebif_pid;
  char *eiss_pid;

  // Callbacks for the section sinks
  gpointer eiss_callback;
  gpointer ebif_callback;

  // indication if the pipeline is live
  //gboolean is_live;
};


struct _GstEbifBinClass
{
  GstBinClass parent_class;

  // signal we fire when a new pad has been decoded into raw audio/video
  void (*new_decoded_pad) (GstElement * element, GstPad * pad, gboolean last);

  // signal we fire when a pad has been removed
  void (*removed_decoded_pad) (GstElement * element, GstPad * pad);

  // signal fired when we found a pad that we cannot decode
  void (*unknown_type) (GstElement * element, GstPad * pad, GstCaps * caps);
};

GType gst_ebif_bin_get_type (void);

/*
typedef void (*gst_ebif_bin_eiss_callback_f) (char *, int);

void gst_ebif_bin_set_eiss_callback (gst_ebif_bin_eiss_callback_f func);
*/

typedef void (*gst_ebif_bin_callback_f) (char *, int);

G_END_DECLS

#endif /* __GST_EBIF_BIN_H__ */
