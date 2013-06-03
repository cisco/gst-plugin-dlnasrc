/* Test program for CableLabs GStreamer plugins
 * Copyright (C) 2013 CableLabs, Louisville, CO 80027
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include <gst/gst.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Global vars for cmd line args
//
static int g_wait_secs = 0;
static int g_state_change_timeout_secs = 45;
static gfloat g_requested_rate = 1;
static int g_rrid = 2;
static int g_rate_change_cnt = 1;
static char g_host[256];
static char g_uri[256];

static gboolean g_use_manual_bin_pipeline = FALSE;
static gboolean g_use_manual_elements_pipeline = FALSE;
static gboolean g_use_simple_pipeline = FALSE;

static gboolean g_use_dtcp = FALSE;
static gboolean g_format_bytes = FALSE;
static gboolean g_do_query = FALSE;
static gboolean g_do_seek = FALSE;

static gboolean g_zero_position = FALSE;
static gboolean g_positions = FALSE;
static gboolean g_test_uri_switch = FALSE;
static int g_eos_max_cnt = 1;
static int g_eos_cnt = 0;
//static const guint64 NANOS_PER_SECOND = 1000000000L;
static const guint64 NANOS_PER_MINUTE = 60000000000L;

static gboolean g_use_file = FALSE;
static char g_file_name[256];
static gchar* g_file_path = NULL;
static gchar* TEST_FILE_URL_PREFIX_ENV = "TEST_FILE_URL_PREFIX";
static gboolean g_create_dot = FALSE;

static GstElement* g_sink = NULL;
static GstElement* g_pipeline = NULL;
static GstElement* g_passthru = NULL;
static GstElement* g_tee = NULL;
static GstElement* g_queue = NULL;
static GstElement* g_mpeg2dec = NULL;
static GstElement* g_aparse = NULL;
static GstElement* g_avdec = NULL;


/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
	GstElement *pipeline;  /* Our pipeline element */
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean seek_done;    /* Have we performed the seek already? */
	gint64 duration;       /* How long does this media last, in nanoseconds */
	gdouble rate;		   /* current playspeed */
} CustomData;

// Local method declarations
//
static gboolean process_cmd_line_args(int argc, char*argv[]);

static GstElement* create_playbin_pipeline();
static GstElement* create_manual_pipeline(gchar* pipeline_name, gboolean use_bin);
static gboolean create_manual_bin_pipeline(GstElement* pipeline);
static gboolean create_manual_elements_pipeline(GstElement* pipeline);
static void bin_cb_pad_added (GstElement *dec, GstPad *pad, gpointer data);
static void tsdemux_cb_pad_added (GstElement *tsdemux, GstPad *pad, gpointer data);
static gboolean link_video_elements(gchar* name, GstPad* tsdemux_src_pad, GstCaps* caps);
static gboolean link_audio_elements(gchar* name, GstPad* tsdemux_src_pad, GstCaps* caps);
static GstElement* create_simple_pipeline();

static void on_source_changed(GstElement* element, GParamSpec* param, gpointer data);

static void perform_test(CustomData* data);
static gboolean perform_test_rate_change(CustomData* data);
static gboolean perform_test_position(CustomData* data);
static gboolean perform_test_query(CustomData* data, gint64* position, GstFormat format);
static gboolean perform_test_seek(CustomData* data, gint64 position, GstFormat format, gfloat rate);
static gboolean set_pipeline_state(CustomData* data, GstState desired_state, gint timeoutSecs);
static gboolean set_new_uri(CustomData* data);
static void handle_message (CustomData *data, GstMessage *msg);

static void log_bin_elements(GstBin* bin);
static GstElement* log_element_links(GstElement* elem);


/**
 * Test program for playspeed testing
 */
int main(int argc, char *argv[]) 
{
	CustomData data;
	data.playing = FALSE;
	data.terminate = FALSE;
	data.seek_enabled = FALSE;
	data.seek_done = FALSE;
	data.duration = GST_CLOCK_TIME_NONE;

	// Assign default values
	strcpy(g_host, "192.168.2.2");
	g_uri[0] = '\0';
	g_file_name[0] = '\0';
	if (!process_cmd_line_args(argc, argv))
	{
		g_printerr("Exit due to problems with cmd line args\n");
		return -1;
	}

	// Build default URI if one was not specified
	if (g_uri[0] == '\0')
	{
		char* line2 = "http://";
		char* line3 = ":8008/ocaphn/recording?rrid=";
		char* line4 = "&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg";
		if (g_use_dtcp)
		{
			line4 = "&profile=DTCP_MPEG_TS_SD_NA_ISO&mime=video/mpeg";
		}
		sprintf(g_uri, "%s%s%s%d%s", line2, g_host, line3, g_rrid, line4);
	}

	// Initialize GStreamer
	gst_init (&argc, &argv);

	// Build the pipeline
	if (g_use_manual_bin_pipeline)
	{
		g_print("Creating pipeline by manually linking uri decode bin\n");
		data.pipeline = create_manual_pipeline("manual-bin-pipeline", TRUE);
	}
	else if (g_use_manual_elements_pipeline)
	{
		g_print("Creating pipeline by manually linking decode elements\n");
		data.pipeline = create_manual_pipeline("manual-elements-pipeline", FALSE);
	}
	else if (g_use_simple_pipeline)
	{
		g_print("Creating simple pipeline\n");
		data.pipeline = create_simple_pipeline();
	}
	else
	{
		g_print("Creating pipeline using playbin\n");
		data.pipeline = create_playbin_pipeline();
	}

	// Check that pipeline was properly created
	if (data.pipeline == NULL)
	{
		g_printerr("Problems creating pipeline\n");
		return -1;
	}
	g_pipeline = data.pipeline;

	// Start playing
	g_print("Pipeline created, start playing\n");
	if (!set_pipeline_state(&data, GST_STATE_PLAYING, g_state_change_timeout_secs))
	{
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		return -1;
	}
	else
	{
		g_print("Pipeline in playing state\n");
	}

	// Print out elements in playbin
	log_bin_elements(GST_BIN(data.pipeline));

	// Create pipeline diagram
	if (g_create_dot)
	{
		g_print("Creating pipeline.dot file\n");
		GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(data.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
	}

	// Perform requested testing
	g_print("Begin pipeline test\n");
	perform_test(&data);

	return 0;
}

/**
 *
 */
static void handle_message (CustomData *data, GstMessage *msg)
{
	GError *err;
	gchar *debug_info;
	GstState old_state, new_state, pending_state;
	g_print("Got message type: %s\n", GST_MESSAGE_TYPE_NAME (msg));

	switch (GST_MESSAGE_TYPE (msg))
	{
	case GST_MESSAGE_ERROR:
		err = NULL;
		debug_info = NULL;
		gst_message_parse_error (msg, &err, &debug_info);
		g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
		g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
		g_clear_error (&err);
		g_free (debug_info);
		data->terminate = TRUE;
		break;

	case GST_MESSAGE_EOS:
		g_print ("End-Of-Stream reached.\n");
		g_eos_cnt++;
		if (g_eos_cnt >= g_eos_max_cnt)
		{
			data->terminate = TRUE;
		}
		break;

	case GST_MESSAGE_DURATION:
		// The duration has changed, mark thdata.pipeline,e current one as invalid
		data->duration = GST_CLOCK_TIME_NONE;
		break;

	case GST_MESSAGE_STATE_CHANGED:
		gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
		if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline))
		{
			g_print ("Pipeline state changed from %s to %s:\n",
					gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

			// Remember whether we are in the PLAYING state or not
			data->playing = (new_state == GST_STATE_PLAYING);

		}
	    break;

	default:
		// We should not reach here
		g_printerr ("Unexpected message received.\n");
		break;
	}
	gst_message_unref (msg);
}

/**
 * Handle command line args
 */
static gboolean process_cmd_line_args(int argc, char *argv[])
{
	int i = 0;
	for (i = 1; i < argc; i++)
	{
		if (strstr(argv[i], "rate=") != NULL)
		{
			if (sscanf(argv[i], "rate=%f", &g_requested_rate) != 1)
			{
				g_printerr("Invalid rate arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested rate change to %4.1f\n", g_requested_rate);
				g_do_seek = TRUE;
				g_print("Setting do seek flag to TRUE\n");
				g_do_query = TRUE;
				g_print("Setting do query flag to TRUE\n");
			}
		}
		else if (strstr(argv[i], "wait=") != NULL)
		{
			if (sscanf(argv[i], "wait=%d", &g_wait_secs) != 1)
			{
				g_printerr("Invalid wait arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested wait secs to %d\n", g_wait_secs);
			}
		}
		else if (strstr(argv[i], "uri=") != NULL)
		{
			if (sscanf(argv[i], "uri=%s\n", &g_uri[0]) != 1)
			{
				g_printerr("Invalid uri arg specified: %s\n", argv[i]);
				return FALSE;

			}
			else
			{
				g_print("Set requested URI to %s\n", g_uri);
			}
		}
		else if (strstr(argv[i], "rrid=") != NULL)
		{
			if (sscanf(argv[i], "rrid=%d", &g_rrid) != 1)
			{
				g_printerr("Invalid rrid specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested rrid to %d\n", g_rrid);
			}
		}
		else if (strstr(argv[i], "host=") != NULL)
		{
			if (sscanf(argv[i], "host=%s\n", &g_host[0]) != 1)
			{
				g_printerr("Invalid host arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested host ip to %s\n", g_host);
			}
		}
		else if (strstr(argv[i], "manual_bin") != NULL)
		{
			g_use_manual_bin_pipeline = TRUE;
			g_print("Set to manually build pipeline using uridecode bin\n");
		}
		else if (strstr(argv[i], "manual_elements") != NULL)
		{
			g_use_manual_elements_pipeline = TRUE;
			g_print("Set to manually build pipeline using individual elements\n");
		}
		else if (strstr(argv[i], "simple") != NULL)
		{
			g_use_simple_pipeline = TRUE;
			g_print("Set to build simplest pipeline\n");
		}
		else if (strstr(argv[i], "switch") != NULL)
		{
			g_test_uri_switch = TRUE;
			g_print("Set to test uri switching\n");
		}
		else if (strstr(argv[i], "query") != NULL)
		{
			g_do_query = TRUE;
			g_print("Set to query position\n");
		}
		else if (strstr(argv[i], "zero") != NULL)
		{
			g_zero_position = TRUE;
			g_print("Set to position to zero\n");
		}
		else if (strstr(argv[i], "file=") != NULL)
		{
			if (sscanf(argv[i], "file=%s\n", &g_file_name[0]) != 1)
			{
				g_printerr("Invalid file name specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_use_file = TRUE;
				g_print("Test using local file %s rather than URI\n", g_file_name);
			}
		}
		else if (strstr(argv[i], "dtcp") != NULL)
		{
			g_use_dtcp = TRUE;
			g_print("Set to use dtcp URI\n");
		}
		else if (strstr(argv[i], "byte") != NULL)
		{
			g_format_bytes = TRUE;
			g_print("Set to use byte format for query and seek\n");
		}
		else if (strstr(argv[i], "dot") != NULL)
		{
			g_create_dot = TRUE;
			g_print("Set to generate dot file for pipeline diagram\n");
		}
        else if (strstr(argv[i], "changes=") != NULL)
        {
            if (sscanf(argv[i], "changes=%d", &g_rate_change_cnt) != 1)
            {
                g_printerr("Invalid rate change count arg specified: %s\n", argv[i]);
                return FALSE;
            }
            else
            {
                g_print("Set requested rate change count to %d\n", g_rate_change_cnt);
            }
        }
        else if (strstr(argv[i], "position") != NULL)
        {
            g_positions = TRUE;
            g_print("Set to test positioning\n");
        }
		else
		{
			g_printerr("Invalid option: %s\n", argv[i]);
			g_printerr("Usage:\n");
			g_printerr("\t byte \t\t use byte format for query and seek\n");
            g_printerr("\t changes=x \t\t where x is number of 2x increase rate changes\n");
			g_printerr("\t dot \t\t generate dot file of pipeline diagram\n");
			g_printerr("\t dtcp \t\t indicates content is dtcp/ip encrypted\n");
			g_printerr("\t file=name \t\twhere name indicates file name using path from env var\n");
			g_printerr("\t host=ip \t\t addr of server\n");
			g_printerr("\t manual_uri_bin \t\t build manual pipeline using uri decode bin\n");
			g_printerr("\t manual_decode_bin \t\t build manual pipeline using decode bin\n");
			g_printerr("\t manual_elements \t\t build manual pipeline using decode elements\n");
            g_printerr("\t position \t\t test positioning of seeks\n");
			g_printerr("\t query \t\t perform seek using current position + 10\n");
			g_printerr("\t rate=y \t\t where y is desired rate\n");
			g_printerr("\t rrid=i \t\t where i is cds recording id\n");
			g_printerr("\t simple \t\t create simple pipeline rather than playbin\n");
			g_printerr("\t switch \t\t play for wait secs then switch uri\n");
			g_printerr("\t uri=l \t\t where l is uri of desired content\n");
			g_printerr("\t wait=x \t\t where x is secs\n");
			g_printerr("\t zero \t\t set start position of seek to zero\n");
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * Create playbin pipeline
 */
static GstElement* create_playbin_pipeline()
{
	GstElement* pipeline = NULL;
	char launchLine[256];
	char* line1 = "playbin flags=0x3 uri=";
	if (!g_use_file)
	{
		sprintf(launchLine, "%s%s", line1, g_uri);
	}
	else
	{
		g_file_path = getenv(TEST_FILE_URL_PREFIX_ENV);
		if (g_file_path == NULL)
		{
			g_printerr ("Could not get env var %s value\n", TEST_FILE_URL_PREFIX_ENV);
			return NULL;
		}
		else
		{
			sprintf(launchLine, "%s%s%s", line1, g_file_path, g_file_name);
		}
	}

	g_print("Starting up playbin using line: %s\n", launchLine);
	pipeline = gst_parse_launch(launchLine, NULL);

	// Register to receive playbin source signal & call get property on sourc
	// to get supported playspeeds just like webkit would do
    g_signal_connect(pipeline, "notify::source", G_CALLBACK(on_source_changed), pipeline);

    // Uncomment this line to limit the amount of downloaded data
    // NOTE: Can't set ring buffer to anything besides zero otherwise queue will not forward events
    //g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000, NULL);

    // Tried to force audio to fake sink since it complained about audio decoder
    // when trying to change URIs
    //GstElement* fake_sink = gst_element_factory_make ("fakesink", "fakesink");
    //g_object_set (pipeline, "audio-sink", fake_sink, NULL);

    return pipeline;
}

/**
 * Create really simple pipeline with just src and sink
 */
static GstElement* create_simple_pipeline()
{
	GstElement* passthru;

	// Create gstreamer elements
	GstElement* pipeline = gst_pipeline_new("simple-pipeline");
	if (!pipeline)
	{
		g_printerr ("Pipeline element could not be created.\n");
		return NULL;
	}
	GstElement* src = NULL;
	if (g_use_file)
	{
		g_print("%s() - using file source\n", __FUNCTION__);
		src = gst_element_factory_make ("filesrc", "file-source");
		if (!src)
		{
			g_printerr ("Filesrc element could not be created.\n");
			return NULL;
		}
		g_object_set(G_OBJECT(src), "location", "clock.mpg", NULL);
	}
	else
	{
		g_print("%s() - using dlnasrc\n", __FUNCTION__);
		src = gst_element_factory_make ("dlnasrc", "dlna http source");
		if (!src)
		{
			g_printerr ("dlnasrc element could not be created.\n");
			return NULL;
		}
		g_object_set(G_OBJECT(src), "uri", g_uri, NULL);
	}

	// Create passthru element
	passthru  = gst_element_factory_make ("passthru", "passthru");
	if (!passthru)
	{
		g_printerr ("passthru element could not be created.\n");
		return NULL;
	}

	GstElement* sink = gst_element_factory_make ("filesink", "file-sink");
	if (!sink)
	{
		g_printerr ("Filesink element could not be created.\n");
		return NULL;
	}
	g_object_set(G_OBJECT(sink), "location", "simple.out", NULL);

	// Add all elements into the pipeline
	gst_bin_add_many (GST_BIN (pipeline),
			src, passthru, sink,
			NULL);

	// Link the elements together
	if (!gst_element_link_many (src, passthru, sink,
			NULL))
	{
		g_printerr ("Problems linking elements together\n");
		return NULL;
	}

	return pipeline;
}

/**
 * Create pipeline manually based on pipeline which
 * playbin creates.
 */
static GstElement* create_manual_pipeline(gchar* pipeline_name, gboolean use_bin)
{
	// Create gstreamer elements
	GstElement* pipeline = gst_pipeline_new(pipeline_name);
	if (!pipeline)
	{
		g_printerr ("Pipeline element could not be created.\n");
		return NULL;
	}

	// Check if local file uri should be used
	if (g_use_file)
	{
		g_file_path = getenv(TEST_FILE_URL_PREFIX_ENV);
		if (g_file_path == NULL)
		{
			g_printerr ("Could not get env var %s value\n", TEST_FILE_URL_PREFIX_ENV);
			return NULL;
		}
		else
		{
			sprintf(g_uri, "%s%s", g_file_path, g_file_name);
		}
	}

	// Create playsink
	g_sink  = gst_element_factory_make ("playsink", "playsink");
	if (!g_sink)
	{
		g_printerr ("playsink element could not be created.\n");
		return NULL;
	}
	gst_util_set_object_arg (G_OBJECT(g_sink), "flags",
	      "soft-colorbalance+soft-volume+vis+text+audio+video");

	if (use_bin)
	{
		if (!create_manual_bin_pipeline(pipeline))
		{
			g_printerr ("%s() - problems creating decode bin based pipeline.\n",
					__FUNCTION__);
			return NULL;
		}
	}
	else
	{
		if (!create_manual_elements_pipeline(pipeline))
		{
			g_printerr ("%s() - problems creating pipeline built from decode elements.\n",
					__FUNCTION__);
			return NULL;
		}
	}

	g_print("%s() - creation of manual pipeline complete\n", __FUNCTION__);
	return pipeline;
}

/**
 * Create pipeline manually by creating a decode bin and linking in callback function.
 */
static gboolean create_manual_bin_pipeline(GstElement* pipeline)
{
	// Create decode elements
	GstElement* dbin = NULL;

	// Create URI decode bin
	g_print("%s() - creating uridecodebin element\n", __FUNCTION__);

	dbin  = gst_element_factory_make ("uridecodebin", "uridecodebin");
	if (!dbin)
	{
		g_printerr ("%s() - uridecodebin element could not be created.\n", __FUNCTION__);
		return FALSE;
	}
	g_object_set(G_OBJECT(dbin), "uri", g_uri, NULL);
	g_print("%s() - set uri decode bin uri: %s\n", __FUNCTION__, g_uri);

	// Create diagnostic element
	g_passthru  = gst_element_factory_make ("passthru", "diagnostic-pt");
	if (!g_passthru)
	{
		g_printerr ("%s() - passthru element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	gst_bin_add_many (GST_BIN (pipeline),
			dbin, g_passthru, g_sink,
			NULL);

	// Add callback to link after source has been selected
	g_print("%s() - add callback on decode bin\n", __FUNCTION__);
	g_signal_connect (dbin, "pad-added", G_CALLBACK (bin_cb_pad_added), NULL);
	g_print("%s() - setup for decode bin callback\n", __FUNCTION__);

	g_print("%s() - creation of manual uri decode bin pipeline complete\n", __FUNCTION__);
	return TRUE;
}

/**
 * Callback function to link decode bin to playsink when building a manual pipeline
 * using a uri decode bin.
 */
static void
bin_cb_pad_added (GstElement *dec,
		GstPad     *srcpad,
		gpointer    data)
{
	g_print("%s() - called\n", __FUNCTION__);

	GstCaps *caps;
	GstStructure *str;
	const gchar *name;
	GstPadTemplate *templ;
	GstElementClass *klass;
	GstPad* d_src_pad;
	GstPad* d_sink_pad;

	d_src_pad = gst_element_get_static_pad(g_passthru, "src");
	d_sink_pad = gst_element_get_static_pad(g_passthru, "sink");
	if (!d_src_pad || !d_sink_pad)
	{
		g_printerr ("passthru element pads could not be retrieved.\n");
		return;
	}
	if ((gst_pad_is_linked(d_sink_pad)) || (gst_pad_is_linked(d_src_pad)))
	{
		g_printerr ("passthru element pads already linked\n");
		return;
	}

	/* check media type */
	caps = gst_pad_query_caps (srcpad, NULL);
	str = gst_caps_get_structure (caps, 0);
	name = gst_structure_get_name (str);

	klass = GST_ELEMENT_GET_CLASS (g_sink);

	if (g_str_has_prefix (name, "audio")) {
		templ = gst_element_class_get_pad_template (klass, "audio_sink");
	} else if (g_str_has_prefix (name, "video")) {
		templ = gst_element_class_get_pad_template (klass, "video_sink");
	} else if (g_str_has_prefix (name, "text")) {
		templ = gst_element_class_get_pad_template (klass, "text_sink");
	} else {
		templ = NULL;
	}

	if (templ)
	{
		g_print("%s() - got sink pad template\n", __FUNCTION__);
		GstPad *sinkpad;

		sinkpad = gst_element_request_pad (g_sink, templ, NULL, NULL);
		g_print("%s() - got sink pad from sink element\n", __FUNCTION__);

		if (!gst_pad_is_linked (sinkpad))
		{
			gst_pad_link (srcpad, d_sink_pad);
			gst_pad_link (d_src_pad, sinkpad);
			g_print("%s() - sink pad of sink element is now linked\n", __FUNCTION__);
		}
		else
		{
			g_print("%s() - sink pad of sink element was already linked\n", __FUNCTION__);
		}
		gst_object_unref (sinkpad);
	}
}

/**
 * Create pipeline manually by creating individual elements based on pipeline which
 * playbin creates.
 */
static gboolean create_manual_elements_pipeline(GstElement* pipeline)
{
	GstElement* tsdemux = NULL;
	GstElement* src = NULL;
	GstElement* mqueue = NULL;

	g_print("%s() - creating elements manually\n", __FUNCTION__);

	// Create either a file or dlna source
	if (g_use_file)
	{
		src = gst_element_factory_make ("filesrc", "file-source");
		g_object_set(G_OBJECT(src), "location", g_file_name, NULL);
	}
	else
	{
		src = gst_element_factory_make ("dlnasrc", "dlna-source");
		g_object_set(G_OBJECT(src), "uri", g_uri, NULL);
	}
	if (!src)
	{
		g_printerr ("%s() - src element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	g_tee = gst_element_factory_make ("tee", "tee");
	if (!g_tee)
	{
		g_printerr ("%s() - tee element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	mqueue  = gst_element_factory_make ("queue2", "mqueue");
	if (!mqueue)
	{
		g_printerr ("%s() - mpeg queue element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	tsdemux = gst_element_factory_make ("tsdemux", "tsdemux");
	if (!tsdemux)
	{
		g_printerr ("%s() - tsdemux element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	g_queue  = gst_element_factory_make ("multiqueue", "multiqueue");
	if (!g_queue)
	{
		g_printerr ("%s() - queue element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	g_mpeg2dec  = gst_element_factory_make ("mpeg2dec", "mpeg2dec");
	if (!g_mpeg2dec)
	{
		g_printerr ("%s() - mpeg2dec element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	g_aparse  = gst_element_factory_make ("ac3parse", "ac3parse");
	if (!g_aparse)
	{
		g_printerr ("%s() - ac3parse element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	g_avdec  = gst_element_factory_make ("avdec_ac3", "avdec_ac3");
	if (!g_avdec)
	{
		g_printerr ("%s() - avdec_ac3 element could not be created.\n", __FUNCTION__);
		return FALSE;
	}

	gst_bin_add_many (GST_BIN (pipeline),
			src, g_tee, mqueue, tsdemux, g_queue, g_mpeg2dec,
			g_aparse, g_avdec,
			g_sink,
			NULL);

	// Can only link source to demux for now, rest is done in callback
	if (!gst_element_link_many (
			src, g_tee, mqueue, tsdemux,
			NULL))
	{
		g_printerr ("%s() - Problems linking filesrc to tsdemux\n", __FUNCTION__);
		return FALSE;
	}
	g_print("%s() - linked filesrc to tsdemux complete\n", __FUNCTION__);

	// Add callback to link after source has been selected
	g_print("%s() - registering tsdemux for callback\n", __FUNCTION__);
	g_signal_connect (tsdemux, "pad-added", G_CALLBACK (tsdemux_cb_pad_added), NULL);
	g_print("%s() - setup for tsdemux callback\n", __FUNCTION__);

	g_print("%s() - creation of manual pipeline complete\n", __FUNCTION__);
	return TRUE;
}

/**
 * Callback function to manually link up decode elements
 */
static void
tsdemux_cb_pad_added (GstElement *tsdemux,
					  GstPad     *tsdemux_src_pad,
					  gpointer    data)
{
	g_print("%s() - called\n", __FUNCTION__);

	gchar *name;
	GstCaps *caps;
	gchar *caps_string;

	name = gst_pad_get_name (tsdemux_src_pad);
	g_print ("%s() - new pad %s was created\n", __FUNCTION__, name);

	caps = gst_pad_query_caps (tsdemux_src_pad, NULL);
	caps_string = gst_caps_to_string (caps);
	gst_caps_unref (caps);
	g_print ("%s() - Pad Capability:  %s\n", __FUNCTION__, caps_string);

	if (strncmp (caps_string, "video/mpeg", 10) == 0)
	{
		if (link_video_elements(name, tsdemux_src_pad, caps))
		{
			g_print("%s() - setup of video elements complete\n", __FUNCTION__);
		}
		else
		{
			g_printerr("%s() - problems in setup of video elements\n", __FUNCTION__);
		}
	}
	else if (strncmp (caps_string, "audio/x-ac3", 11) == 0)
	{
		if (link_audio_elements(name, tsdemux_src_pad, caps))
		{
			g_print("%s() - setup of audio elements complete\n", __FUNCTION__);
		}
		else
		{
			g_printerr("%s() - problems in setup of audio elements\n", __FUNCTION__);
		}
	}
	else
	{
		g_print ("%s() - Pad was not an audio or video pad\n", __FUNCTION__);
	}
	g_free (name);
	g_free (caps_string);
}

static gboolean
link_video_elements(gchar* name, GstPad* tsdemux_src_pad, GstCaps* caps)
{
	GstPad* queue_in_pad = NULL;
	GstPad* mpeg2dec_src_pad = NULL;
	GstPad* sink_in_pad = NULL;
	GstPadTemplate *templ;
	GstElementClass *klass;
	gboolean is_linked = FALSE;

	g_print("%s() - dynamic Video-pad created, linking %s to queue/Mpegparser\n", __FUNCTION__, name);

	// Link tsdemux src pad to queue
	queue_in_pad = gst_element_get_compatible_pad(g_queue, tsdemux_src_pad, caps);
	if (queue_in_pad != NULL)
	{
		g_print("%s() - got sink pad: %s ", __FUNCTION__, gst_pad_get_name (queue_in_pad));

		if (gst_pad_link (tsdemux_src_pad, queue_in_pad) == GST_PAD_LINK_OK)
		{
			g_print ("%s() - linked tdemux src pad to queue sink pad\n", __FUNCTION__);
		}
		else
		{
			g_print ("%s() - unable to link src pad to sink\n", __FUNCTION__);
			return FALSE;
		}
		gst_object_unref (queue_in_pad);
	}
	else
	{
		g_print ("%s() - Unable to get sink pad\n", __FUNCTION__);
		return FALSE;
	}

	// Link queue to mpegvparse just using default element links
	if (!gst_element_link_many (
			g_queue, g_mpeg2dec,
			NULL))
	{
		g_printerr ("%s() - problems linking video decode elements\n", __FUNCTION__);
		return FALSE;
	}

	// Link mpeg2dec video src pad to playsink
	// Get static video src pad of mpeg2dec to hook up to sink pad of playsink
	mpeg2dec_src_pad = gst_element_get_static_pad(g_mpeg2dec, "src");
	if (mpeg2dec_src_pad == NULL)
	{
		g_printerr ("%s() - unable to get video src output pad from mpeg2dec\n", __FUNCTION__);
		return FALSE;
	}

	// Get playsink's video sink pad template
	klass = GST_ELEMENT_GET_CLASS (g_sink);
	templ = gst_element_class_get_pad_template (klass, "video_sink");
	if (templ)
	{
		g_print("%s() - got sink video pad template\n", __FUNCTION__);

		sink_in_pad = gst_element_request_pad (g_sink, templ, NULL, NULL);
		if (sink_in_pad != NULL)
		{
			g_print("%s() - got sink pad from sink element\n", __FUNCTION__);

			if (gst_pad_link (mpeg2dec_src_pad, sink_in_pad) != GST_PAD_LINK_OK)
			{
				g_printerr ("%s() - unable to link mpeg2dec src pad to playsink sink pad\n", __FUNCTION__);
			}
			else
			{
				g_print("%s() - mpeg2dec and playsink are now linked\n", __FUNCTION__);
				is_linked = TRUE;
			}
			gst_object_unref (sink_in_pad);
		}
		else
		{
			g_printerr("%s() - Unable to get sink pad of playsink\n", __FUNCTION__);
		}
	}
	else
	{
		g_printerr("%s() - Unable to get template of video sink pad of playsink\n", __FUNCTION__);
	}

	return is_linked;
}

static gboolean
link_audio_elements(gchar* name, GstPad* tsdemux_src_pad, GstCaps* caps)
{
	GstPad* queue_in_pad = NULL;
	GstPad* avdec_src_pad = NULL;
	GstPad* sink_in_pad = NULL;
	GstPadTemplate *templ;
	GstElementClass *klass;
	gboolean is_linked = FALSE;

	g_print("%s() - dynamic Audio-pad created, linking %s to queue/ac3parser\n", __FUNCTION__, name);

	// Link tsdemux src pad to queue
	queue_in_pad = gst_element_get_compatible_pad(g_queue, tsdemux_src_pad, caps);
	if (queue_in_pad != NULL)
	{
		g_print("%s() - got sink pad: %s ", __FUNCTION__, gst_pad_get_name (queue_in_pad));

		if (gst_pad_link (tsdemux_src_pad, queue_in_pad) == GST_PAD_LINK_OK)
		{
			g_print ("%s() - linked tdemux src pad to queue sink pad\n", __FUNCTION__);
		}
		else
		{
			g_printerr ("%s() - unable to link src pad to sink\n", __FUNCTION__);
			return FALSE;
		}
		gst_object_unref (queue_in_pad);
	}
	else
	{
		g_printerr ("%s() - Unable to get sink pad\n", __FUNCTION__);
		return FALSE;
	}

	// Link queue to ac3parse just using default element links
	if (!gst_element_link_many (
			g_queue, g_aparse, g_avdec,
			NULL))
	{
		g_printerr ("%s() - problems linking audio decode elements\n", __FUNCTION__);
		return FALSE;
	}

	// Get static audio src pad of avdec to hook up to sink pad of playsink
	avdec_src_pad = gst_element_get_static_pad(g_avdec, "src");
	if (avdec_src_pad == NULL)
	{
		g_printerr ("%s() - unable to get audio src output pad from avdec\n", __FUNCTION__);
		return FALSE;
	}

	// Get playsink's audio sink pad template
	klass = GST_ELEMENT_GET_CLASS (g_sink);
	templ = gst_element_class_get_pad_template (klass, "audio_sink");
	if (templ)
	{
		g_print("%s() - got sink audio pad template\n", __FUNCTION__);

		sink_in_pad = gst_element_request_pad (g_sink, templ, NULL, NULL);
		if (sink_in_pad != NULL)
		{
			g_print("%s() - got sink pad from sink element\n", __FUNCTION__);

			if (gst_pad_link (avdec_src_pad, sink_in_pad) != GST_PAD_LINK_OK)
			{
				g_printerr ("%s() - unable to link avdec src pad to playsink sink pad\n", __FUNCTION__);
			}
			else
			{
				is_linked = TRUE;
				g_print("%s() - avdec and playsink are now linked\n", __FUNCTION__);
			}
			gst_object_unref (sink_in_pad);
		}
		else
		{
			g_printerr("%s() - Unable to get sink pad of playsink\n", __FUNCTION__);
		}
	}
	else
	{
		g_printerr("%s() - Unable to get template of audio sink pad of playsink\n", __FUNCTION__);
	}
	return is_linked;
}

/**
 * Callback when playbin's source element changes
 */
static void on_source_changed(GstElement* element, GParamSpec* param, gpointer data)
{
	g_print("Notified of source change, gather supported rates\n");

	int i = 0;
    GstElement* src = NULL;
    gchar *strVal = NULL;

    g_object_get(element, "source", &src, NULL);
    if (src != NULL)
    {
    	g_print("Got src from callback, determine if dlnasrc\n");

    	g_object_get(src, "cl_name", &strVal, NULL);
    	if (strVal != NULL)
    	{
    		// Get supported rates property value which is a GArray
    		g_print("Getting supported rates\n");
    		GArray* arrayVal = NULL;
    		g_object_get(src, "supported_rates", &arrayVal, NULL);
    		if (arrayVal != NULL)
    		{
    			g_print("Supported rates cnt: %d\n", arrayVal->len);
    			for (i = 0; i < arrayVal->len; i++)
    			{
    				g_print("Retrieved rate %d: %f\n", (i+1), g_array_index(arrayVal, gfloat, i));
    			}
    		}
    		else
    		{
    			g_printerr("Got null value for supported rates property\n");
    		}
    	}
    	else
    	{
       		g_print("dlnasrc is NOT source for pipeline\n");
    	}
    }
    else
    {
    	g_printerr("Unable to get src from callback\n");
    }
}

static void log_bin_elements(GstBin* bin)
{
	GstIterator* it = gst_bin_iterate_elements(bin);
	gboolean done = FALSE;
	GstElement *elem = NULL;
	GValue value = { 0 };

	while (!done)
	{
		switch (gst_iterator_next (it, &value))
		{
		case GST_ITERATOR_OK:
			elem = (GstElement *) g_value_get_object (&value);
			g_print("Bin %s has element: %s\n",
				GST_ELEMENT_NAME(GST_ELEMENT(bin)),
				GST_ELEMENT_NAME(elem));

			// If this is a bin, log its elements
			if (GST_IS_BIN(elem))
			{
				log_bin_elements(GST_BIN(elem));
			}
			else
			{
				g_print("Element: %s is linked to: ", GST_ELEMENT_NAME(elem));
				elem = log_element_links(elem);
			}
			g_value_unset (&value);
			break;
		case GST_ITERATOR_RESYNC:
			gst_iterator_resync (it);
			break;
		case GST_ITERATOR_ERROR:
			g_printerr("Unable to iterate through elements\n");
			done = TRUE;
			break;
		case GST_ITERATOR_DONE:
			done = TRUE;
			break;
		}
	}
	gst_iterator_free (it);
}

static GstElement* log_element_links(GstElement* elem)
{
	// Get sink pads of this element
	GstIterator* it = gst_element_iterate_src_pads(elem);
	gboolean done = FALSE;
	GstPad *pad = NULL;
	GstPad* peer = NULL;
	GstElement* ds_elem = NULL;
	int cnt = 0;
	GValue value = { 0 };
	while (!done)
	{
		switch (gst_iterator_next (it, &value))
		{
		case GST_ITERATOR_OK:
			pad = (GstPad *) g_value_get_object (&value);
			// Got a pad, check if its linked
			if (gst_pad_is_linked(pad))
			{
				cnt++;

				// Get peer of linkage
				peer = gst_pad_get_peer(pad);
				if (peer)
				{
					// Get parent element
					ds_elem = gst_pad_get_parent_element(peer);
					if (ds_elem)
					{
						g_print(" %s\n", GST_ELEMENT_NAME(ds_elem));
					}
					else
					{
						g_print(" NULL Parent\n");
					}
				}
			}
			g_value_unset (&value);
			break;
		case GST_ITERATOR_RESYNC:
			gst_iterator_resync (it);
			break;
		case GST_ITERATOR_ERROR:
			g_printerr("Unable to iterate through elements\n");
			done = TRUE;
			break;
		case GST_ITERATOR_DONE:
			done = TRUE;
			break;
		default:
			done = TRUE;
			break;
		}
	}
	gst_iterator_free (it);
	if (cnt == 0)
	{
		g_print(" Nothing\n");
	}

	return ds_elem;
}

static void perform_test(CustomData* data)
{
	GstBus *bus;
	GstMessage *msg;

    if (g_requested_rate >= 2)
    {
        perform_test_rate_change(data);
    }
    else if (g_requested_rate == 0)
	{
		g_print("%s - Pausing pipeline for %d secs\n", __FUNCTION__, g_wait_secs);
		set_pipeline_state (data, GST_STATE_PAUSED, g_state_change_timeout_secs);

		g_usleep(g_wait_secs * 1000000L);

		g_print("%s - Resuming pipeline after %d sec pause\n", __FUNCTION__, g_wait_secs);
		set_pipeline_state (data, GST_STATE_PLAYING, g_state_change_timeout_secs);
	}
    else if (g_test_uri_switch)
	{
		// Set eos count to two since one will be received for each EOS
		g_eos_max_cnt = 2;
		if (!set_new_uri(data))
		{
			g_print("Problems setting new URI\n");
			return;
		}
	}
    else if (g_positions)
    {
        perform_test_position(data);
    }

	// Wait until error or EOS
	bus = gst_element_get_bus (data->pipeline);
	g_print("%s - Waiting for EOS or ERROR\n", __FUNCTION__);
	gboolean done = FALSE;
	while (!done)
	{
		msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
											GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
		if (msg != NULL)
		{
			g_print("%s - Received msg type: %s\n", __FUNCTION__, GST_MESSAGE_TYPE_NAME(msg));
			handle_message(data, msg);
			gst_message_unref (msg);

			if (data->terminate)
			{
				done = TRUE;
			}
		}
	}
}

static gboolean perform_test_rate_change(CustomData* data)
{
    int i = 0;
    gfloat rate = 0;
    long secs = g_wait_secs;
    gint64 position = -1;

    for (i = 0; i < g_rate_change_cnt; i++)
    {
        rate = pow(g_requested_rate, i+1);

        g_print("%s - Waiting %ld secs prior to change %d of %d using rate %0.2f\n",
                __FUNCTION__, secs, (i+1), g_rate_change_cnt, rate);
        g_usleep(secs * 1000000L);

        // Determine what format to use for query and seek
        GstFormat format = GST_FORMAT_TIME;
        if (g_format_bytes)
        {
            format = GST_FORMAT_BYTES;
        }
        g_print("%s - Test pipeline using format: %s\n",
                __FUNCTION__, gst_format_get_name(format));

        // Query current position, duration and rate
        if (g_do_query)
        {
            if (!perform_test_query(data, &position, format))
            {
                g_printerr("%s - Problems with query associated with test.\n",
                        __FUNCTION__);
                return FALSE;
            }
        }
        else
        {
            g_print("%s - Not performing query\n", __FUNCTION__);
        }

        // Initiate seek to perform test
        if (g_do_seek)
        {
            if (!perform_test_seek(data, position, format, rate))
            {
                g_printerr("%s - Problems with seek associated with test.\n",
                        __FUNCTION__);
                return FALSE;
            }
        }
        else
        {
            g_print("%s - Not performing seek\n", __FUNCTION__);
        }
    }

    return TRUE;
}

static gboolean perform_test_position(CustomData* data)
{
    int i = 0;
    gfloat rate = 1.0;
    long secs = g_wait_secs;
    guint64 position = 0L;

    g_print("%s - Performing position changes at 4, 1, 3, and 2 mins\n",  __FUNCTION__);

    guint64 positions[] = {4L,1L,3L,2L};
    for (i = 0; i < 4; i++)
    {
        g_print("%s - Waiting %ld secs prior to seeking to %llu minutes\n",
                __FUNCTION__, secs, positions[i]);
        g_usleep(secs * 1000000L);

        position = positions[i] * NANOS_PER_MINUTE;
        g_print("%s - Seeking to time position: %" GST_TIME_FORMAT "\n",
                __FUNCTION__, GST_TIME_ARGS (position));

        // Initiate seek to perform test
        if (!perform_test_seek(data, position, GST_FORMAT_TIME, rate))
        {
            g_printerr("%s - Problems with seek associated with test.\n",
                    __FUNCTION__);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean perform_test_query(CustomData* data, gint64* position, GstFormat format)
{
	g_print("%s - Query position in format: %s\n",
			__FUNCTION__, gst_format_get_name(format));

	// Query current time
	if (!gst_element_query_position(data->pipeline, format, position))
	{
		g_printerr("%s - Unable to retrieve current position.\n",
				__FUNCTION__);
		return FALSE;
	}
	if (format == GST_FORMAT_TIME)
	{
	    g_print("%s - Got current time position: %" GST_TIME_FORMAT "\n",
            __FUNCTION__, GST_TIME_ARGS (*position));
	}

	// Query duration
	if (!GST_CLOCK_TIME_IS_VALID (data->duration))
	{
		if (!gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &data->duration))
		{
			g_printerr ("Could not query current duration.\n");
			return FALSE;
		}
		else
		{
		    if (format == GST_FORMAT_TIME)
		    {
		        g_print("%s - Got time duration: %" GST_TIME_FORMAT "\n",
		            __FUNCTION__, GST_TIME_ARGS (data->duration));
		    }
		}
	}

	// Get the current playback rate
	GstQuery* query = NULL;
	GstFormat fmt;
	gint64 start;
	gint64 stop;
	query = gst_query_new_segment(GST_FORMAT_TIME);
	if (query != NULL)
	{
		gst_element_query(data->pipeline, query);
		gst_query_parse_segment(query, &data->rate, &fmt, &start, &stop);
		gst_query_unref(query);
		//g_print ("\nCurrent playspeed: %3.1f\n\n", data.rate);
	}
	else
	{
		g_printerr ("Could not query segment to get playback rate\n");
		return FALSE;
	}

	// Print current position and total duration
	if (!g_format_bytes)
	{
		g_print ("Current Time Position %" GST_TIME_FORMAT " / Total Duration %" GST_TIME_FORMAT "\r",
			GST_TIME_ARGS (*position), GST_TIME_ARGS (data->duration));
	}

	return TRUE;
}

static gboolean perform_test_seek(CustomData* data, gint64 start_position, GstFormat format, gfloat rate)
{
    gint64 stop_position = GST_CLOCK_TIME_NONE;

	// Determine if seek is necessary for requested test
	if ((rate == 0) || (g_test_uri_switch))
	{
		g_print("%s - No seeking necessary for requested test\n", __FUNCTION__);
		return TRUE;
	}

	// If requested, set position to zero
	if (g_zero_position)
	{
		start_position = 0;
	}

	// If reverse playspeed, set stop to zero
	if (rate < 0)
	{
	    stop_position = 0;
	}

	if (format == GST_FORMAT_TIME)
	{
        g_print("%s - Seeking to time position: %" GST_TIME_FORMAT " at rate %3.1f\n",
                __FUNCTION__, GST_TIME_ARGS (start_position), rate);
	}
	else
	{
	    g_print("%s - Requesting rate of %4.1f at position %lld using format %s\n",
	        __FUNCTION__, rate, start_position, gst_format_get_name(format));
	}

	// Initiate seek
	if (!gst_element_seek (data->pipeline, rate,
			format, GST_SEEK_FLAG_FLUSH,
			GST_SEEK_TYPE_SET, start_position,
			GST_SEEK_TYPE_NONE, stop_position))
	{
		g_printerr("%s - Problems seeking.\n", __FUNCTION__);
		return FALSE;
	}
	g_print("%s - Seeking on pipeline element complete\n", __FUNCTION__);

	return TRUE;
}

static gboolean set_pipeline_state(CustomData* data, GstState desired_state, gint timeoutSecs)
{
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(data->pipeline), desired_state);
    printf("Set state returned: %d\n", ret);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
    	g_printerr ("Unable to set playbin to desired state: %s\n",
    			gst_element_state_get_name(desired_state));
    	return FALSE;
    }
    if (ret == GST_STATE_CHANGE_ASYNC)
    {
        printf("State change is async, calling get state to wait %d secs for state\n",
        		timeoutSecs);
        int maxCnt = timeoutSecs;
        int curCnt = 0;
        GstState state = GST_STATE_NULL;
        do
        {
             ret = gst_element_get_state(GST_ELEMENT(data->pipeline), // element
                    &state, // state
                    NULL, // pending
                    100000000LL); // timeout(1 second = 10^9 nanoseconds)

             if (ret == GST_STATE_CHANGE_SUCCESS)
             {
             	printf("State change succeeded, now in desired state: %s\n",
             			gst_element_state_get_name(desired_state));
             	return TRUE;
             }
             else if (ret == GST_STATE_CHANGE_FAILURE)
             {
                 g_printerr ("State change failed\n");
                 return FALSE;
             }
             else if (ret == GST_STATE_CHANGE_ASYNC)
             {
                 g_printerr ("State change time out: %d secs\n", curCnt);
             }
             else
             {
                 g_printerr ("Unknown state change return value: %d\n", ret);
                 return FALSE;
             }
             curCnt++;

             // Sleep for a short time
             g_usleep(1000000L);
        }
        while ((desired_state != state) && (curCnt < maxCnt));
     }
 	g_printerr("Timed out waiting %d secs for desired state: %s\n",
 			timeoutSecs, gst_element_state_get_name(desired_state));
    return FALSE;
}

static gboolean set_new_uri(CustomData* data)
{
	// Formulate a new URI
	gchar new_uri[256];
	new_uri[0] = '\0';
	char* line2 = "http://";
	char* line3 = ":8008/ocaphn/recording?rrid=";
	char* line4 = "&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg";
	sprintf(new_uri, "%s%s%s%d%s", line2, g_host, line3, 7, line4);

	// Get source of pipeline which is a playbin
	g_print("Getting source of playbin\n");
	GstElement* dlna_src = NULL;
	if (data->pipeline != NULL)
	{
	    g_object_get(data->pipeline, "source", &dlna_src, NULL);
		if (dlna_src != NULL)
		{
			g_print("Setting new uri: %s\n", new_uri);
			g_object_set(G_OBJECT(dlna_src), "uri", &new_uri, NULL);
			g_print("Done setting new uri: %s\n", new_uri);
		}
		else
		{
			g_printerr("Unable to get source of pipeline to change URI\n");
			return FALSE;
		}
	}
	else
	{
		g_printerr("Unable to change URI due to NULL pipeline\n");
		return FALSE;
	}
	return TRUE;
}
