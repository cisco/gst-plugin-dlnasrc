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

#include "gstebifbin.h"

//GST_DEBUG_CATEGORY_STATIC (gst_ebif_bin_debug);
//#define GST_CAT_DEFAULT gst_ebif_bin_debug

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VOLUME
};

/* signals */
enum
{
  LAST_SIGNAL
};

GST_BOILERPLATE (GstEbifBin, gst_ebif_bin, GstElement, GST_TYPE_PIPELINE);

//static void gst_ebif_bin_class_init (GstEbifBinClass * klass);

//static void gst_ebif_bin_init (GstEbifBin * ebif_bin);

static void gst_ebif_bin_dispose (GObject * object);

// TODO - Need functions to create / adjust the pipeline

static GstEbifBin* gst_ebif_build_pipeline (GstEbifBin *ebif_bin);

GstEbifBin* gst_ebif_rebuild_pipeline (GstEbifBin *ebif_bin);

static void gst_ebif_empty_pipeline (GstPipeline *pipeline);

//static void
//ebifbin_set_audio_mute (GstEbifBaseBin * ebif_base_bin,
//			gboolean mute);

static void gst_ebif_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);

static void gst_ebif_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

//static GstStateChangeReturn
//gst_ebif_bin_change_state (GstElement * element, GstStateChange transition);

//static GstPipelineClass *parent_class;

const GstElementDetails gst_ebif_bin_details
= GST_ELEMENT_DETAILS("EBIF Bin",
		      "EBIF/Bin/Player",
		      "Play MPEG/EBIF/EISS media from an uri",
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
       "EBIF aware media player",
       "Generic/Bin/Player",
       "Play MPEG+EBIF transport streams",
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

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, ARG_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  // GstEbifBin:volume:
  // Get or set the current audio stream volume.
  //   1.0 means 100%,
  //   0.0 means mute.
  // This uses a linear volume scale.
  g_object_class_install_property (gobject_klass, ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_ebif_bin_dispose);

  gst_element_class_set_details (gstelement_klass, &gst_ebif_bin_details);

  //gstelement_klass->change_state =
  //  GST_DEBUG_FUNCPTR (gst_ebif_bin_change_state);
}

static void
gst_ebif_bin_init (GstEbifBin * ebif_bin,
		   GstEbifBinClass * gclass)
{
  /*
  // Create the empty pipeline
  ebif_bin->pipeline = (GstPipeline *) gst_pipeline_new("ebif-player");
  if (!ebif_bin->pipeline) {
    g_printerr ("The main pipeline could not be created. Exiting.\n");
    exit(1);
  }
  */

  // Initialize the configurable elements
  ebif_bin->video_sink = NULL;
  ebif_bin->audio_sink = NULL;
  ebif_bin->volume_element = NULL;
  ebif_bin->volume = 1.0;

  // Set some defaults for testing
  ebif_bin->uri = NULL;
  ebif_bin->video_pid = "0x104";
  ebif_bin->audio_pid = "0x103";
  ebif_bin->ebif_pid = "0x102";
  ebif_bin->eiss_pid = "0x101";

  // Now, create the pipeline
  gst_ebif_build_pipeline(ebif_bin);
}

static void
gst_ebif_bin_dispose (GObject * object)
{
  GstEbifBin *ebif_bin = GST_EBIF_BIN (object);

  // Release the elements in the pipeline
  gst_element_set_state ((GstElement *)ebif_bin, GST_STATE_NULL);

  // Release the user configured elements
  if (ebif_bin->video_sink != NULL) {
    gst_element_set_state (ebif_bin->video_sink, GST_STATE_NULL);
    gst_object_unref (ebif_bin->video_sink);
    ebif_bin->video_sink = NULL;
  }
  if (ebif_bin->audio_sink != NULL) {
    gst_element_set_state (ebif_bin->audio_sink, GST_STATE_NULL);
    gst_object_unref (ebif_bin->audio_sink);
    ebif_bin->audio_sink = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void 
gst_ebif_bin_set_property (GObject * object, guint prop_id,
			   const GValue * value, GParamSpec * pspec)
{
  GstEbifBin *ebif_bin;

  ebif_bin = GST_EBIF_BIN (object);

  switch (prop_id) {
    case ARG_URI:
    {
      const gchar *uri = g_value_get_string (value);

      if (uri == NULL) {
        g_warning ("cannot set NULL uri");
        return;
      }
      /* if we have no previous uri, or the new uri is different from the
       * old one, replug */
      if (ebif_bin->uri == NULL || strcmp (ebif_bin->uri, uri) != 0) {
        g_free (ebif_bin->uri);
        ebif_bin->uri = g_strdup (uri);
      }

      // Rebuild a new pipeline
      gst_ebif_rebuild_pipeline(ebif_bin);
      break;
    }

    case ARG_VIDEO_SINK:
      if (ebif_bin->video_sink != NULL) {
        gst_object_unref (ebif_bin->video_sink);
      }
      ebif_bin->video_sink = g_value_get_object (value);
      if (ebif_bin->video_sink != NULL) {
        gst_object_ref (ebif_bin->video_sink);
        gst_object_sink (GST_OBJECT_CAST (ebif_bin->video_sink));
      }

      // Rebuild a new pipeline
      gst_ebif_rebuild_pipeline(ebif_bin);
      break;

    case ARG_AUDIO_SINK:
      if (ebif_bin->audio_sink != NULL) {
        gst_object_unref (ebif_bin->audio_sink);
      }
      ebif_bin->audio_sink = g_value_get_object (value);
      if (ebif_bin->audio_sink != NULL) {
        gst_object_ref (ebif_bin->audio_sink);
        gst_object_sink (GST_OBJECT_CAST (ebif_bin->audio_sink));
      }
      
      // Rebuild a new pipeline
      gst_ebif_rebuild_pipeline(ebif_bin);
      break;

    case ARG_VOLUME:
      ebif_bin->volume = g_value_get_double (value);
      if (ebif_bin->volume_element) {
        g_object_set (G_OBJECT (ebif_bin->volume_element), "volume",
            ebif_bin->volume, NULL);
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
    case ARG_VIDEO_SINK:
      g_value_set_object (value, ebif_bin->video_sink);
      break;
    case ARG_AUDIO_SINK:
      g_value_set_object (value, ebif_bin->audio_sink);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, ebif_bin->volume);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*
  gst_ebif_build_source_tee

  Create the source for the pipeline, with a tee to connect to
 */
static GstElement*
gst_ebif_build_source_tee (GstEbifBin *ebif_bin)
{
  GstElement *source = NULL;
  GstElement *tee;

  // Create the source
  if (ebif_bin->uri != NULL) {
    // Use the URI to automagically select a source
    source = gst_element_make_from_uri(GST_URI_SRC, ebif_bin->uri,
				       "file-source");
  }
  if (source == NULL) {
    // Create a filesrc as the default
    source = gst_element_factory_make ("filesrc", "file-source");

    // Set the input filename for the filesrc element
    if (source) {
      g_object_set (G_OBJECT (source), "location", ebif_bin->uri, NULL);
    }
  }

  // Add a splitter for multiple streams
  tee = gst_element_factory_make ("tee", "stream-splitter");

  if (!source || !tee) {
    g_printerr ("The source tee could not be created. Exiting.\n");
    exit(1);
  }

  // Add the element into the pipeline
  gst_bin_add_many(GST_BIN(&ebif_bin->pipeline), source, tee, NULL);

  // Link them together
  gst_element_link(source, tee);

  return tee;
}


/*
  gst_ebif_build_video_pipeline

  queue -> pidfilter ->esassembler -> mpegdecoder -> videoscale -> xvimagesink
 */
static GstElement*
gst_ebif_build_video_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *videoQueue, *videoPF, *videoAssem,
             *videoParse, *videoScale, *videoSink;

  // Create the standard video stream elements
  videoQueue = gst_element_factory_make ("queue",       "video-queue");
  videoPF    = gst_element_factory_make ("pidfilter",   "video-pidfilter");
  videoAssem = gst_element_factory_make ("esassembler", "video-assembler");
  videoParse = gst_element_factory_make ("mpegdecoder", "video-parser");
  videoScale = gst_element_factory_make ("videoscale", "video-scaler");

  // Create the video sink
  if (! ebif_bin->video_sink) {
    ebif_bin->video_sink = gst_element_factory_make ("xvimagesink",
						     "videosink");
    gst_object_ref (ebif_bin->video_sink);
  }
  videoSink = ebif_bin->video_sink;

  // Make sure everything is good
  if (!videoQueue || !videoPF || !videoAssem || !videoParse
      || !videoScale || !videoSink) {
    g_printerr ("One video element could not be created. Exiting.\n");
    exit(1);
  }

  // Set up the video filter
  g_object_set (G_OBJECT (videoPF), "pidlist", ebif_bin->video_pid, NULL);

  // Add the elements to the pipeline 
  gst_bin_add_many(GST_BIN(&ebif_bin->pipeline), videoQueue, videoPF,
		   videoAssem, videoParse, videoScale, videoSink, NULL);

  // Video pipeline
  gst_element_link_many(videoQueue, videoPF, videoAssem, videoParse,
			videoScale, videoSink, NULL);

  return videoQueue;
}

/*
  gst_ebif_build_audio_pipeline

  queue -> pidfilter -> esassembler -> audioparse -> volume -> pulsesink
 */
static GstElement*
gst_ebif_build_audio_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *audioQueue;
  GstElement *audioPF, *audioAssem, *audioParse, *audioVolume, *audioSink;

  // Create the standard audio elements
  audioQueue  = gst_element_factory_make ("queue",       "audio-queue");
  audioPF     = gst_element_factory_make ("pidfilter",   "audio-pidfilter");
  audioAssem  = gst_element_factory_make ("esassembler", "audio-assembler");
  audioParse  = gst_element_factory_make ("audioparse",  "audio-parser");
  audioVolume = gst_element_factory_make ("volume",      "audio-volume");

  // Create the audio sink
  if (! ebif_bin->audio_sink) {
    ebif_bin->audio_sink = gst_element_factory_make ("pulsesink",
						     "audio-sink");
    gst_object_ref (ebif_bin->audio_sink);
  }
  audioSink = ebif_bin->audio_sink;

  // Make sure everything is good
  if (!audioQueue || !audioPF || !audioAssem
      || !audioParse || !audioVolume || !audioSink) {
    g_printerr ("One audio element could not be created. Exiting.\n");
    exit(1);
  }

  // Set up the audio filter
  g_object_set (G_OBJECT (audioPF), "pidlist", ebif_bin->audio_pid, NULL);

  // Add the elements to the pipeline 
  gst_bin_add_many(GST_BIN(&ebif_bin->pipeline), audioQueue, audioPF, audioAssem,
		   audioParse, audioVolume, audioSink, NULL);

  // Audio pipeline
  gst_element_link_many(audioQueue, audioPF, audioAssem, audioParse,
			audioVolume, audioSink, NULL);

  return audioQueue;
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

  // The ebif stream
  ebifQueue = gst_element_factory_make ("queue",            "ebif-queue");
  ebifPF    = gst_element_factory_make ("pidfilter",        "ebif-pidfilter");
  ebifAssem = gst_element_factory_make ("sectionassembler", "ebif-assembler");
  //ebifSink  = gst_element_factory_make ("sectionsink",      "ebif-sink");
  ebifSink  = gst_element_factory_make ("fakesink",      "ebif-sink");

  if (!ebifQueue   || !ebifPF     || !ebifAssem   || !ebifSink) {
    g_printerr ("One EBIF element could not be created. Exiting.\n");
    exit(1);
  }

  // Set up the EBIF filter
  g_object_set (G_OBJECT (ebifPF), "pidlist", ebif_bin->ebif_pid, NULL);
  g_object_set (G_OBJECT (ebifAssem), "assemble-all", 1, NULL);

  // Add the elements to the pipeline 
  gst_bin_add_many(GST_BIN(&ebif_bin->pipeline), ebifQueue,  ebifPF,
		   ebifAssem, ebifSink, NULL);

  // EBIF pipeline
  gst_element_link_many(ebifQueue, ebifPF, ebifAssem, ebifSink, NULL);

  return ebifQueue;
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

  // The eiss pipeline
  eissQueue = gst_element_factory_make ("queue",            "eiss-queue");
  eissPF    = gst_element_factory_make ("pidfilter",        "eiss-pidfilter");
  eissAssem = gst_element_factory_make ("sectionassembler", "eiss-assembler");
  //eissSink  = gst_element_factory_make ("sectionsink",      "eiss-sink");
  eissSink  = gst_element_factory_make ("fakesink",      "eiss-sink");

  if (!eissQueue || !eissPF || !eissAssem || !eissSink) {
    g_printerr ("One EISS element could not be created. Exiting.\n");
    exit(1);
  }

  // Set up the EISS filter
  g_object_set (G_OBJECT (eissPF), "pidlist", ebif_bin->eiss_pid, NULL);

  // Set up the EISS assembler
  g_object_set (G_OBJECT (eissAssem), "assemble-all", 1, NULL);

  g_object_set (G_OBJECT (eissSink), "dump", FALSE, NULL);

  // Add the elements to the pipeline 
  gst_bin_add_many(GST_BIN(&ebif_bin->pipeline), eissQueue, eissPF,
		   eissAssem, eissSink, NULL);

  // EISS pipeline
  gst_element_link_many(eissQueue, eissPF, eissAssem, eissSink, NULL);

  return eissQueue;
}


/*
  gst_ebif_build_pipeline

    tee -> videoQueue
        -> audioQueue
	-> ebifQueue
	-> eissQueue
 */
static GstEbifBin*
gst_ebif_build_pipeline (GstEbifBin *ebif_bin)
{
  GstElement *tee, *videoQueue, *audioQueue, *ebifQueue, *eissQueue;

  // Create the source
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
  
  return ebif_bin;
}


GstEbifBin*
gst_ebif_rebuild_pipeline (GstEbifBin *ebif_bin)
{
  // Release the elements in the old pipeline
  gst_ebif_empty_pipeline(&ebif_bin->pipeline);

  // Rebuild a new pipeline
  gst_ebif_build_pipeline(ebif_bin);
  
  return ebif_bin;
}

/*
  gst_ebif_empty_pipeline

  Remove, and release the elements in the pipeline
 */
static void
gst_ebif_empty_pipeline (GstPipeline *pipeline)
{
  GstIterator *it;
  GstElement *item;
  int done = FALSE;

  it = gst_bin_iterate_elements(GST_BIN(pipeline));
  while (!done) {
    switch (gst_iterator_next (it, (gpointer *)&item)) {
    case GST_ITERATOR_OK:
      gst_bin_remove(GST_BIN (pipeline), item);
      gst_object_unref (item);
      break;
    case GST_ITERATOR_RESYNC:
      gst_iterator_resync (it);
      break;
    case GST_ITERATOR_ERROR:
      g_printerr
	("gst_ebif_empty_pipeline - error in the iterator. Exiting.\n");
      exit(1);
      break;
    case GST_ITERATOR_DONE:
      done = TRUE;
      break;
    }
  }
  gst_iterator_free (it);

}

/*
static GstStateChangeReturn
gst_ebif_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstEbifBin *ebif_bin;

  ebif_bin = GST_EBIF_BIN (element);


  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      // this really is the easiest way to make the state change return
      // ASYNC until we added the sinks 
      if (!ebif_bin->fakesink) {
        ebif_bin->fakesink = gst_element_factory_make ("fakesink", "test");
        gst_bin_add (GST_BIN_CAST (ebif_bin), ebif_bin->fakesink);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      // remember us being a live pipeline 
      ebif_bin->is_live = (ret == GST_STATE_CHANGE_NO_PREROLL);
      GST_DEBUG_OBJECT (ebif_bin, "is live: %d", ebif_bin->is_live);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      // FIXME Release audio device when we implement that
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      // remove sinks we added 
      //remove_sinks (ebif_bin);
      // and there might be a fakesink we need to clean up now 
      if (ebif_bin->fakesink) {
        gst_element_set_state (ebif_bin->fakesink, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (ebif_bin), ebif_bin->fakesink);
        ebif_bin->fakesink = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}
*/

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
    "MPEG+EBIF Player",
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
