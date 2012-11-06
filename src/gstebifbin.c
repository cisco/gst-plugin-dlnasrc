/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
/*
  This file was derived from the gstplaybin.c file, and has been adapted
  to play MPEG files containing EBIF / EISS streams
 */

/**
 * SECTION:element-ebifbin
 *
 * MPEG+EBIF Stream player
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch ...
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <glib-object.h>

#include "gstebifbin.h"

#define QUEUE_BUFF_SIZE 2000

//GST_DEBUG_CATEGORY_STATIC (gst_ebif_bin_debug);
//#define GST_CAT_DEFAULT gst_ebif_bin_debug

/* props */
enum
{
  ARG_0,
  ARG_VIDEO_PID,
  ARG_AUDIO_PID,
  ARG_EISS_CALLBACK,
  ARG_EISS_PID,
  ARG_EBIF_CALLBACK,
  ARG_EBIF_PID
  //ARG_AUDIO_SINK,
  //ARG_VIDEO_SINK,
  //ARG_VOLUME
};

/* signals */
enum
{
  SIGNAL_NEW_DECODED_PAD,
  SIGNAL_UNKNOWN_TYPE,
  LAST_SIGNAL
};
static guint gst_ebif_bin_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstEbifBin, gst_ebif_bin, GstElement, GST_TYPE_BIN);

static void gst_ebif_bin_dispose (GObject * object);

static GstEbifBin* gst_ebif_build_bin (GstEbifBin *ebif_bin);

GstEbifBin* gst_ebif_rebuild_bin (GstEbifBin *ebif_bin);

static void gst_ebif_empty_bin (GstBin *bin);

static GstElement* gst_ebif_build_eiss_pipeline (GstEbifBin *ebif_bin);

static GstElement* gst_ebif_build_ebif_pipeline (GstEbifBin *ebif_bin);

static void gst_ebif_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);

static void gst_ebif_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

void
gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure *closure,
				       GValue *return_value G_GNUC_UNUSED,
				       guint n_param_values,
				       const GValue *param_values,
				       gpointer invocation_hint G_GNUC_UNUSED,
				       gpointer marshal_data);


const GstElementDetails gst_ebif_bin_details
= GST_ELEMENT_DETAILS("EBIF Bin",
		      "EBIF/Bin/Decoder",
		      "Decode MPEG/EBIF/EISS media from a uri",
		      "Eric Winkelman <e.winkelman@cablelabs.com>");

/*
GType
gst_ebif_bin_get_type (void)
{
  static GType gst_ebif_bin_type = 0;

  if (!gst_ebif_bin_type) {
    static const GTypeInfo gst_ebif_bin_info = {
      sizeof (GstEbifBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ebif_bin_class_init,
      NULL,
      NULL,
      sizeof (GstEbifBin),
      0,
      (GInstanceInitFunc) gst_ebif_bin_init,
      NULL
    };

    gst_ebif_bin_type = g_type_register_static (GST_TYPE_PIPELINE,
						"GstEbifBin",
						&gst_ebif_bin_info, 0);
  }

  return gst_ebif_bin_type;
}
*/

static void
gst_ebif_bin_base_init (gpointer gclass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

    gst_element_class_set_details_simple
      (element_class,
       "EBIF aware media decoder",
       "Generic/Bin/Decoder",
       "Decode MPEG+EBIF transport streams",
       "Eric Winkelman <e.winkelman@cablelabs.com>");
}

static void
gst_ebif_bin_class_init (GstEbifBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_ebif_bin_set_property;
  gobject_klass->get_property = gst_ebif_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_VIDEO_PID,
      g_param_spec_string ("video-pid", "Video PID",
			    "Sets the program Id for the video stream",
			   NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_AUDIO_PID,
      g_param_spec_string ("audio-pid", "Audio PID",
			    "Sets the program Id for the audio stream",
			   NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_EISS_CALLBACK,
      g_param_spec_pointer ("eiss-callback", "EISS Callback",
			    "Callback function for EISS sections",
			    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_EISS_PID,
      g_param_spec_string ("eiss-pid", "EISS Program Id",
			    "Sets the program Id for the EISS stream",
			   NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_EBIF_CALLBACK,
      g_param_spec_pointer ("ebif-callback", "EBIF Callback",
			    "Callback function for EBIF sections",
			    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_EBIF_PID,
      g_param_spec_string ("ebif-pid", "EBIF PID",
			    "Sets the program Id for the EBIF stream",
			   NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_ebif_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_ebif_bin_details);

  /**
   * GstEbifBin::new-decoded-pad:
   * @bin: The ebifbin
   * @pad: The available pad
   * @islast: #TRUE if this is the last pad to be added. Deprecated.
   *
   * This signal gets emitted as soon as a new pad of the same type as one of
   * the valid 'raw' types is added.
   */
  gst_ebif_bin_signals[SIGNAL_NEW_DECODED_PAD] =
      g_signal_new ("new-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstEbifBinClass, new_decoded_pad), NULL, NULL,
      gst_play_marshal_VOID__OBJECT_BOOLEAN, G_TYPE_NONE, 2, GST_TYPE_PAD,
      G_TYPE_BOOLEAN);

  // Unused, but here to mimic decodebin
  gst_ebif_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstEbifBinClass, unknown_type), NULL, NULL,
      gst_marshal_VOID__OBJECT_BOXED, G_TYPE_NONE, 2, GST_TYPE_PAD,
      GST_TYPE_CAPS);
}

static void
gst_ebif_bin_init (GstEbifBin * ebif_bin,
		   GstEbifBinClass * gclass)
{
  printf("\n>>>Setting the pidfilter defaults\n");

  // Set some defaults for testing
  ebif_bin->video_pid = "0x0011";
  ebif_bin->audio_pid = "0x0014";
  ebif_bin->ebif_pid  = "0x06ea";
  ebif_bin->eiss_pid  = "0x06e8";

  // Now, create the bin
  gst_ebif_build_bin(ebif_bin);
}

static void
gst_ebif_bin_dispose (GObject * object)
{
  //GstEbifBin *ebif_bin = GST_EBIF_BIN (object);

  // Release the elements in the bin
  //gst_element_set_state ((GstElement *)ebif_bin, GST_STATE_NULL);

  // Release the user configured elements
  /*
  if (ebif_bin->video_sink != NULL) {
    gst_element_set_state (ebif_bin->video_sink, GST_STATE_NULL);
    gst_object_unref (ebif_bin->video_sink);
    ebif_bin->video_sink = NULL;
  }
  */

  /*
  if (ebif_bin->audio_sink != NULL) {
    gst_element_set_state (ebif_bin->audio_sink, GST_STATE_NULL);
    gst_object_unref (ebif_bin->audio_sink);
    ebif_bin->audio_sink = NULL;
  }
  */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void 
gst_ebif_bin_set_property (GObject * object, guint prop_id,
			   const GValue * value, GParamSpec * pspec)
{
  GstEbifBin *ebif_bin;

  ebif_bin = GST_EBIF_BIN (object);

  switch (prop_id) {


  case ARG_VIDEO_PID:
    {
      GstElement *elem;

      printf(">>>Setting the VIDEO_PID property\n");
      
      // Get the video pidfilter
      elem = gst_bin_get_by_name(&ebif_bin->bin, "video-pidfilter");

      printf(">>>Setting the pid to %s\n", g_value_get_string(value));

      // Set the pidlist
      g_object_set(G_OBJECT(elem), "pidlist", g_value_get_string(value), NULL);
    }
    break;

  case ARG_AUDIO_PID:
    {
      GstElement *elem;

      printf(">>>Setting the AUDIO_PID property\n");
      
      // Get the audio pidfilter
      elem = gst_bin_get_by_name(&ebif_bin->bin, "audio-pidfilter");

      printf(">>>Setting the pid to %s\n", g_value_get_string(value));

      // Set the pidlist
      g_object_set(G_OBJECT(elem), "pidlist", g_value_get_string(value), NULL);
    }
    break;

  case ARG_EISS_CALLBACK:
    ebif_bin->eiss_callback = g_value_get_pointer(value);
    break;

  case ARG_EISS_PID:
    {
      GstElement *elem;

      printf(">>>Setting the EISS_PID property\n");
      
      // Get the pidfilter
      elem = gst_bin_get_by_name(&ebif_bin->bin, "eiss-pidfilter");

      // Create the pipeline if
      if (elem == NULL) {
	GstElement *eissQueue;
	GstElement *tee;

	printf(">>>Building the new pipeline\n");

	// Build the EISS pipeline
	eissQueue = gst_ebif_build_eiss_pipeline(ebif_bin);

	// Get the tee element
	tee = gst_bin_get_by_name(&ebif_bin->bin, "stream-splitter");

	// Make sure everything worked...
	if (!eissQueue || !tee) {
	  g_warning("Unable to create EISS pipeline\n");
	}

	// Link them together
	gst_element_link(tee, eissQueue);
      
	// Get the pidfilter
	elem = gst_bin_get_by_name(&ebif_bin->bin, "eiss-pidfilter");
      }

      printf(">>>Setting the pid to %s\n", g_value_get_string(value));

      // Set the pidlist
      g_object_set(G_OBJECT(elem), "pidlist",
		   g_value_get_string(value), NULL);
    }
    break;

  case ARG_EBIF_CALLBACK:
    ebif_bin->ebif_callback = g_value_get_pointer(value);
    break;

  case ARG_EBIF_PID:
    {
      GstElement *elem;

      printf(">>>Setting the EBIF_PID property\n");
      
      // Get the pidfilter
      elem = gst_bin_get_by_name(&ebif_bin->bin, "ebif-pidfilter");

      // Create the pipeline if
      if (elem == NULL) {
	GstElement *ebifQueue;
	GstElement *tee;

	printf(">>>Building the new pipeline\n");

	// Build the EBIF pipeline
	ebifQueue = gst_ebif_build_ebif_pipeline(ebif_bin);

	// Get the tee element
	tee = gst_bin_get_by_name(&ebif_bin->bin, "stream-splitter");

	// Make sure everything worked...
	if (!ebifQueue || !tee) {
	  g_warning("Unable to create EBIF pipeline\n");
	}

	// Link them together
	gst_element_link(tee, ebifQueue);
      
	// Get the pidfilter
	elem = gst_bin_get_by_name(&ebif_bin->bin, "ebif-pidfilter");
      }

      printf(">>>Setting the pid to %s\n", g_value_get_string(value));

      // Set the pidlist
      g_object_set(G_OBJECT(elem), "pidlist", g_value_get_string(value), NULL);
    }
    break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ebif_bin_get_property (GObject * object, guint prop_id, GValue * value,
			   GParamSpec * pspec)
{
  GstEbifBin *ebif_bin;

  ebif_bin = GST_EBIF_BIN (object);

  switch (prop_id) {

  case ARG_EISS_CALLBACK:
    g_value_set_pointer(value, ebif_bin->eiss_callback);
    break;

  case ARG_EBIF_CALLBACK:
    g_value_set_pointer(value, ebif_bin->ebif_callback);
    break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*
  gst_ebif_build_source_tee

  Create the source tee for the streams
 */
static GstElement*
gst_ebif_build_source_tee (GstEbifBin *ebif_bin)
{
  //GstElement *source = NULL;
  GstElement *tee;

  // Create a splitter for multiple streams
  tee = gst_element_factory_make ("tee", "stream-splitter");

  if (!tee) {
    g_printerr ("The source tee could not be created. Exiting.\n");
    exit(1);
  }

  // Add the element into the bin
  gst_bin_add_many(GST_BIN(&ebif_bin->bin), tee, NULL);

  return tee;
}


/*
  gst_ebif_build_video_pipeline

  queue -> pidfilter -> esassembler -> mpegdecoder
 */
static GstElement*
gst_ebif_build_video_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *videoQueue, *videoPF, *videoAssem, *videoParse;

  // Create the standard video stream elements
  videoQueue = gst_element_factory_make ("queue",       "video-queue");
  videoPF    = gst_element_factory_make ("pidfilter",   "video-pidfilter");
  videoAssem = gst_element_factory_make ("esassembler", "video-assembler");
  //videoParse = gst_element_factory_make ("ffdec_h264", "video-parser");
  videoParse = gst_element_factory_make ("mpeg2dec", "video-parser");

  // Make sure everything is good
  if (!videoQueue || !videoPF || !videoAssem || !videoParse) {
    g_printerr ("A video element could not be created. Exiting.\n");
    exit(1);
  }

  // Up the buffer size so the pipeline doesn't block
  g_object_set (G_OBJECT (videoQueue), "max-size-buffers", QUEUE_BUFF_SIZE,
		NULL);  
  //g_object_set (G_OBJECT (videoQueue), "leaky", 2, NULL);  

  // Set up the video filter
  g_object_set (G_OBJECT (videoPF), "pidlist", ebif_bin->video_pid, NULL);

  // Add the elements to the bin 
  gst_bin_add_many(GST_BIN(&ebif_bin->bin), videoQueue, videoPF,
		   videoAssem, videoParse, NULL);

  // Video bin
  gst_element_link_many(videoQueue, videoPF, videoAssem, videoParse, NULL);

  return videoQueue;
}

/*
  gst_ebif_build_audio_pipeline

  queue -> pidfilter -> esassembler -> audioparse
 */
static GstElement*
gst_ebif_build_audio_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *audioQueue;
  GstElement *audioPF, *audioAssem, *audioParse;

  // Create the standard audio elements
  audioQueue  = gst_element_factory_make ("queue",       "audio-queue");
  audioPF     = gst_element_factory_make ("pidfilter",   "audio-pidfilter");
  audioAssem  = gst_element_factory_make ("esassembler", "audio-assembler");
  //audioParse  = gst_element_factory_make ("faad",        "audio-parser");
  //audioParse  = gst_element_factory_make ("audioparse",  "audio-parser");
  //audioParse  = gst_element_factory_make ("mad",  "audio-parser");
  audioParse  = gst_element_factory_make ("a52dec",  "audio-parser");

  // Make sure everything is good
  if (!audioQueue || !audioPF || !audioAssem || !audioParse) {
    g_printerr ("An audio element could not be created. Exiting.\n");
    exit(1);
  }

  // Up the buffer size so the pipeline doesn't block
  g_object_set (G_OBJECT (audioQueue), "max-size-buffers", QUEUE_BUFF_SIZE,
		NULL);  
  //g_object_set (G_OBJECT (audioQueue), "leaky", 2, NULL);  

  // Set up the audio filter
  g_object_set (G_OBJECT (audioPF), "pidlist", ebif_bin->audio_pid, NULL);

  // Add the elements to the bin 
  gst_bin_add_many(GST_BIN(&ebif_bin->bin), audioQueue, audioPF,
		   audioAssem, audioParse, NULL);

  // Audio pipeline
  gst_element_link_many(audioQueue, audioPF, audioAssem, audioParse, NULL);

  return audioQueue;
}


/*
  cb_ebif

  This function gets called by the sectionsink when there is a new section.
 */
void cb_ebif (GstElement *eissSink, guint arg0, gpointer section,
	      gpointer ebifBin)
{
  // int n;
  char *data = (char *)GST_BUFFER_DATA((GstBuffer *)section);
  int size = GST_BUFFER_SIZE((GstBuffer *)section);

  /*
  printf("\ncb_eiss called!\n");
  printf("arg0: %i\n", arg0);
  printf("section: %p\n", section);
  printf("ebif_bin: %p\n", ebifBin);
  printf("eissSink: %p\n", eissSink);
  printf("data address: %p\n", data);
  printf("data size: %i\n", size);
  printf("section:\n");
  int n;
  for(n = 1; n <= size; n++) {
    printf("%02x ", ((char *)data)[n-1] & 0xff);
    if (n % 16 == 0) printf("\n");
  }
  printf("\n");
  */

  if (GST_EBIF_BIN(ebifBin)->ebif_callback != NULL)
    ((gst_ebif_bin_callback_f)
     (GST_EBIF_BIN(ebifBin)->ebif_callback))(data, size);

  //eiss_callback(data, size);
}


/*
  gst_ebif_build_ebif_pipeline

  queue -> pidfilter -> sectionassembler -> sectionsink
 */
static GstElement*
gst_ebif_build_ebif_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *ebifQueue;
  GstElement *ebifPF, *ebifAssem, *ebifSink;
  GstElement *ebifDebug;

  // The ebif stream
  ebifQueue = gst_element_factory_make ("queue",            "ebif-queue");
  ebifPF    = gst_element_factory_make ("pidfilter",        "ebif-pidfilter");
  ebifAssem = gst_element_factory_make ("sectionassembler", "ebif-assembler");
  ebifDebug = gst_element_factory_make ("identity",         "ebif-debug");
  ebifSink  = gst_element_factory_make ("sectionsink",      "ebif-sink");

  if (!ebifQueue   || !ebifPF     || !ebifAssem   || !ebifSink) {
    g_printerr ("One EBIF element could not be created. Exiting.\n");
    exit(1);
  }

  // Up the buffer size so the pipeline doesn't block
  g_object_set (G_OBJECT (ebifQueue), "max-size-buffers", QUEUE_BUFF_SIZE,
		NULL);  
  //g_object_set (G_OBJECT (ebifQueue), "leaky", 2, NULL);  

  // Set up the EBIF filter
  g_object_set (G_OBJECT (ebifPF), "pidlist", ebif_bin->ebif_pid, NULL);
  g_object_set (G_OBJECT (ebifAssem), "assemble-all", 1, NULL);

  // Dump debugging info
  g_object_set (G_OBJECT (ebifDebug), "dump", FALSE, NULL);

  // Add the elements to the bin
  gst_bin_add_many(GST_BIN(&ebif_bin->bin), ebifQueue, ebifPF,
		   ebifDebug,
		   ebifAssem, ebifSink, NULL);

  // EBIF pipeline
  gst_element_link_many(ebifQueue, ebifPF, ebifAssem,
			ebifDebug,
			ebifSink, NULL);

  // Connect the eiss signal handler
  g_signal_connect(G_OBJECT(ebifSink), "section-available",
		   G_CALLBACK(cb_ebif), ebif_bin);

  return ebifQueue;
}


/*
  gst_ebif_bin_eiss_callback_f

  The callback function for eiss sections
 */
//gst_ebif_bin_eiss_callback_f eiss_callback = NULL; //print_eiss;


/*
  gst_ebif_bin_set_eiss_callback

  Set the callback function for eiss sections
 */
/*
void gst_ebif_bin_set_eiss_callback (gst_ebif_bin_eiss_callback_f func)
{
  eiss_callback = func;
}
*/

/*
  cb_eiss

  This function gets called by the sectionsink when there is a new section.
 */
void cb_eiss (GstElement *eissSink, guint arg0, gpointer section,
	      gpointer ebifBin)
{
  // int n;
  char *data = (char *)GST_BUFFER_DATA((GstBuffer *)section);
  int size = GST_BUFFER_SIZE((GstBuffer *)section);

  /*
  printf("\ncb_eiss called!\n");
  printf("arg0: %i\n", arg0);
  printf("section: %p\n", section);
  printf("ebif_bin: %p\n", ebifBin);
  printf("eissSink: %p\n", eissSink);
  printf("data address: %p\n", data);
  printf("data size: %i\n", size);
  printf("section:\n");
  int n;
  for(n = 1; n <= size; n++) {
    printf("%02x ", ((char *)data)[n-1] & 0xff);
    if (n % 16 == 0) printf("\n");
  }
  printf("\n");
  */

  if (GST_EBIF_BIN(ebifBin)->eiss_callback != NULL)
    ((gst_ebif_bin_callback_f)
     (GST_EBIF_BIN(ebifBin)->eiss_callback))(data, size);

  //eiss_callback(data, size);
}


/*
  gst_ebif_build_eiss_pipeline

  queue -> pidfilter -> sectionassembler -> sectionsink
 */
static GstElement*
gst_ebif_build_eiss_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *eissQueue;
  GstElement *eissPF, *eissAssem, *eissSink;
  GstElement *eissDebug;

  // The eiss pipeline
  eissQueue = gst_element_factory_make ("queue",            "eiss-queue");
  eissPF    = gst_element_factory_make ("pidfilter",        "eiss-pidfilter");
  eissAssem = gst_element_factory_make ("sectionassembler", "eiss-assembler");
  eissDebug = gst_element_factory_make ("identity",         "eiss-debug");
  eissSink  = gst_element_factory_make ("sectionsink",      "eiss-sink");

  if (!eissQueue || !eissPF || !eissAssem || !eissSink) {
    g_printerr ("One EISS element could not be created. Exiting.\n");
    exit(1);
  }

  // Up the buffer size so the pipeline doesn't block
  g_object_set (G_OBJECT (eissQueue), "max-size-buffers", QUEUE_BUFF_SIZE,
		NULL);  
  //g_object_set (G_OBJECT (eissQueue), "leaky", 2, NULL);  

  // Set up the EISS filter
  g_object_set (G_OBJECT (eissPF), "pidlist", ebif_bin->eiss_pid, NULL);

  // Set up the EISS assembler
  g_object_set (G_OBJECT (eissAssem), "assemble-all", 1, NULL);

  // Dump debugging info
  g_object_set (G_OBJECT (eissDebug), "dump", FALSE, NULL);

  // Add the elements to the bin 
  gst_bin_add_many(GST_BIN(&ebif_bin->bin), eissQueue, eissPF,
		   eissDebug,
		   eissAssem, eissSink, NULL);

  // EISS pipeline
  gst_element_link_many(eissQueue, eissPF, eissAssem,
			eissDebug,
			eissSink, NULL);

  printf("ebif_bin: %p\n", ebif_bin);
  printf("eissSink: %p\n", eissSink);

  // Connect the eiss signal handler
  g_signal_connect(G_OBJECT(eissSink), "section-available",
		   G_CALLBACK(cb_eiss), ebif_bin);

  return eissQueue;
}


/*
  cb_source_linked

  A call back function for when the source pad is connected.

  It's purpose is to advertise the availability of the sink pads in a way
  that is compatible with "decodebin"
 */
static void
cb_source_linked (GstPad * pad, GstPad * peerpad, GstElement * ebif_bin)
{
  GstPad *srcPad;

  g_print("gstebifbin - cb_source_linked: the source pad was just linked\n");

  // Check the video pad
  srcPad = gst_element_get_pad(ebif_bin, "src-video");
  if (! srcPad) {
    g_printerr ("cb_source_linked: Could not get ebifbin src-video.\n");
    exit(1);
  }
  if (! gst_pad_is_linked(srcPad)) {

    g_print("emitting the signal for the video\n");

    g_signal_emit (G_OBJECT (ebif_bin),
		   gst_ebif_bin_signals[SIGNAL_NEW_DECODED_PAD],
		   0, srcPad, FALSE);
  }
  gst_object_unref(srcPad);

  // Check the audio pad
  srcPad = gst_element_get_pad(ebif_bin, "src-audio");
  if (! srcPad) {
    g_printerr ("cb_source_linked: Could not get ebifbin src-audio.\n");
    exit(1);
  }
  if (! gst_pad_is_linked(srcPad)) {

    g_print("emitting the signal for the audio\n");

    g_signal_emit (G_OBJECT (ebif_bin),
		   gst_ebif_bin_signals[SIGNAL_NEW_DECODED_PAD],
		   0, srcPad, FALSE);
  }
  gst_object_unref(srcPad);
}


/*
  gst_ebif_build_bin

    tee -> videoQueue
        -> audioQueue
	-> ebifQueue
	-> eissQueue
 */
static GstEbifBin*
gst_ebif_build_bin (GstEbifBin *ebif_bin)
{
  GstElement *tee, *videoQueue, *audioQueue, *src;
  GstElement *ebifQueue;
  GstElement *eissQueue;
  GstPad *pad, *gpad;

  // Create the source tee
  tee = gst_ebif_build_source_tee(ebif_bin);

  // Create the video pipeline
  videoQueue = gst_ebif_build_video_pipeline(ebif_bin);

  // Create the audio pipeline
  audioQueue = gst_ebif_build_audio_pipeline(ebif_bin);
  
  // Create the ebif pipeline
  ebifQueue = gst_ebif_build_ebif_pipeline(ebif_bin);
  
  // Create the eiss pipeline
  eissQueue = gst_ebif_build_eiss_pipeline(ebif_bin);

  // Link the elements together
  gst_element_link(tee, videoQueue);
  gst_element_link(tee, audioQueue);
  gst_element_link(tee, ebifQueue);
  gst_element_link(tee, eissQueue);

  // Create the sink ghost pad
  pad = gst_element_get_static_pad(tee, "sink");
  if (!pad) {
    g_printerr ("Could not get pad for tee\n");
    exit(1);
  }
  gpad = gst_ghost_pad_new("sink", pad);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (&ebif_bin->bin), gpad);
  gst_object_unref (pad);

  // Catch the linking events
  g_signal_connect (G_OBJECT (gpad), "linked",
		    G_CALLBACK (cb_source_linked), ebif_bin);

  // Create the video src pad
  src = gst_bin_get_by_name(GST_BIN (&ebif_bin->bin), "video-parser");
  if (! src) {
    g_printerr ("Could not find the video src\n");
    exit(1);
  }
  pad = gst_element_get_static_pad(src, "src");
  if (!pad) {
    g_printerr ("Could not get pad for video\n");
    exit(1);
  }
  gpad = gst_ghost_pad_new("src-video", pad);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (&ebif_bin->bin), gpad);
  gst_object_unref (pad);

  // Create the audio src pad
  src = gst_bin_get_by_name(GST_BIN (&ebif_bin->bin), "audio-parser");
  if (! src) {
    g_printerr ("Could not find the audio src\n");
    exit(1);
  }
  pad = gst_element_get_static_pad(src, "src");
  if (!pad) {
    g_printerr ("Could not get pad for audio\n");
    exit(1);
  }
  gpad = gst_ghost_pad_new("src-audio", pad);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (&ebif_bin->bin), gpad);
  gst_object_unref (pad);

  return ebif_bin;
}


GstEbifBin*
gst_ebif_rebuild_bin (GstEbifBin *ebif_bin)
{
  // Release the elements in the old bin
  gst_ebif_empty_bin(&ebif_bin->bin);

  // Rebuild a new bin
  gst_ebif_build_bin(ebif_bin);
  
  return ebif_bin;
}

/*
  gst_ebif_empty_bin

  Remove, and release the elements in the bin
 */
static void
gst_ebif_empty_bin (GstBin *bin)
{
  GstIterator *it;
  GstElement *item;
  int done = FALSE;

  it = gst_bin_iterate_elements(GST_BIN(bin));
  while (!done) {
    switch (gst_iterator_next (it, (gpointer *)&item)) {
    case GST_ITERATOR_OK:
      gst_bin_remove(GST_BIN (bin), item);
      gst_object_unref (item);
      break;
    case GST_ITERATOR_RESYNC:
      gst_iterator_resync (it);
      break;
    case GST_ITERATOR_ERROR:
      g_printerr
	("gst_ebif_empty_bin - error in the iterator. Exiting.\n");
      exit(1);
      break;
    case GST_ITERATOR_DONE:
      done = TRUE;
      break;
    }
  }
  gst_iterator_free (it);

}


#if 0

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
ebif_bin_init (GstPlugin * ebif_bin)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template ' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_ebif_bin_debug, "ebifbin",
			   0, "MPEG+EBIF Player");

  return gst_element_register ((GstPlugin *)ebif_bin, "ebifbin",
			       GST_RANK_NONE, GST_TYPE_EBIF_BIN);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "ebifbin"
#endif

/* gstreamer looks for this structure to register eisss
 *
 * exchange the string 'Template eiss' with your eiss description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ebifbin",
    "MPEG+EBIF Decoder",
    (GstPluginInitFunc)ebif_bin_init,
    VERSION,
    "LGPL",
    "gst-cablelabs_ri",
    "http://gstreamer.net/"
)

#endif

/*
static gboolean
gst_ebif_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ebif_bin_debug, "ebifbin", 0, "ebif bin");

  return gst_element_register (plugin, "ebifbin", GST_RANK_NONE,
			       GST_TYPE_EBIF_BIN);
}
*/


/*
  Function for marshaling the callback arguments into a function closure.

  Taken from the decodebin code, so we can replicate the interface.
 */

#define g_marshal_value_peek_boolean(v) (v)->data[0].v_int
#define g_marshal_value_peek_object(v) (v)->data[0].v_pointer
#define g_marshal_value_peek_boxed(v) g_value_get_boxed(v)

void
gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure *closure,
				       GValue *return_value G_GNUC_UNUSED,
				       guint n_param_values,
				       const GValue *param_values,
				       gpointer invocation_hint G_GNUC_UNUSED,
				       gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_BOOLEAN) (gpointer data1,
						     gpointer arg_1,
						     gboolean arg_2,
						     gpointer data2);
  register GMarshalFunc_VOID__OBJECT_BOOLEAN callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  
  g_return_if_fail (n_param_values == 3);
  
  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback = (GMarshalFunc_VOID__OBJECT_BOOLEAN)
    (marshal_data ? marshal_data : cc->callback);
  
  callback (data1,
	    g_marshal_value_peek_object (param_values + 1),
	    g_marshal_value_peek_boolean (param_values + 2),
	    data2);
}

void
gst_play_marshal_BOXED__OBJECT_BOXED (GClosure *closure,
				      GValue *return_value G_GNUC_UNUSED,
				      guint n_param_values,
				      const GValue *param_values,
				      gpointer invocation_hint G_GNUC_UNUSED,
				      gpointer marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__OBJECT_BOXED) (gpointer data1,
							gpointer arg_1,
							gpointer arg_2,
							gpointer data2);
  register GMarshalFunc_BOXED__OBJECT_BOXED callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  gpointer v_return;
			
  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);
  
  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback = (GMarshalFunc_BOXED__OBJECT_BOXED)
    (marshal_data ? marshal_data : cc->callback);
			
  v_return = callback (data1,
		       g_marshal_value_peek_object (param_values + 1),
		       g_marshal_value_peek_boxed (param_values + 2),
		       data2);
			
  g_value_take_boxed (return_value, v_return);
}
