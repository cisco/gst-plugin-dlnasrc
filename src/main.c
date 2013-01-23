#include <gst/gst.h>
#include <string.h>

// Global vars for cmd line args
//
static int waitSecs = 0;
static gfloat requested_rate = 0;
static int rrid = 2;
static char host[256];
static char uri[256];
static gboolean usePlaybin = FALSE;

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
	GstElement *pipeline;  /* Our pipeline element */
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean seek_done;    /* Have we performed the seek already? */
	guint64 duration;      /* How long does this media last, in nanoseconds */
	gdouble rate;		   /* current playspeed */
} CustomData;

// Local method declarations
//
static gboolean process_cmd_line_args(int argc, char*argv[]);
static GstElement* create_playbin2_pipeline();
static GstElement* create_pipeline();
static void on_source_changed(GstElement* element, GParamSpec* param, gpointer data);
static void handle_message (CustomData *data, GstMessage *msg);


/**
 * Test program for playspeed testing
 */
int main(int argc, char *argv[]) 
{
	GstBus *bus;
	GstMessage *msg;
	GstEvent* event;
	GstStateChangeReturn ret;
	GstQuery* query;
	GstEvent* seek_event;
	gint64 start;
	gint64 stop;

	CustomData data;
	data.playing = FALSE;
	data.terminate = FALSE;
	data.seek_enabled = FALSE;
	data.seek_done = FALSE;
	data.duration = GST_CLOCK_TIME_NONE;

	// Assign default values
	strcpy(host, "192.168.2.2");
	uri[0] = '\0';

	if (!process_cmd_line_args(argc, argv))
	{
		g_printerr("Exit due to problems with cmd line args\n");
		return -1;
	}

	// Build default URI if one was not specified
	if (uri[0] == '\0')
	{
		char* line2 = "http://";
		char* line3 = ":8008/ocaphn/recording?rrid=";
		char* line4 = "&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg";
		sprintf(uri, "%s%s%s%d%s", line2, host, line3, rrid, line4);
	}

	// Initialize GStreamer
	gst_init (&argc, &argv);

	// Build the pipeline
	if (usePlaybin)
	{
		g_print("Creating pipeline using playbin2\n");
		data.pipeline = create_playbin2_pipeline();
	}
	else
	{
		g_print("Creating pipeline by assembling elements\n");
		data.pipeline = create_pipeline();
	}

	// Check that pipeline was properly created
	if (data.pipeline == NULL)
	{
		g_printerr("Problems creating pipeline\n");
		return -1;
	}

	// Start playing
	ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		return -1;
	}
	else
	{
		g_print("Pipeline in playing state\n");
	}

	// Wait until error or EOS
	bus = gst_element_get_bus (data.pipeline);
	do {
		msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
				GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);

		// Parse message
		if (msg != NULL)
		{
			handle_message(&data, msg);
		}
		else
		{
			// We got no message, this means the timeout expired
			if (data.playing)
			{
				GstFormat fmt = GST_FORMAT_TIME;
				gint64 current = -1;

				// Query the current position of the stream
				if (!gst_element_query_position (data.pipeline, &fmt, &current))
				{
					g_printerr ("Could not query current position.\n");
				}

				// If we didn't know it yet, query the stream duration
				if (!GST_CLOCK_TIME_IS_VALID (data.duration))
				{
					//g_print ("Current duration is invalid, query for duration\n");
					if (!gst_element_query_duration (data.pipeline, &fmt, &data.duration))
					{
						g_printerr ("Could not query current duration.\n");
					}
					else
					{
						g_print ("Current duration is: %llu\n", data.duration);
					}
				}

				// Get the current playback rate
				query = gst_query_new_segment(fmt);
				if (query != NULL)
				{
					gst_element_query(data.pipeline, query);
					gst_query_parse_segment(query, &data.rate, &fmt, &start, &stop);
					gst_query_unref(query);
					//g_print ("\nCurrent playspeed: %3.1f\n\n", data.rate);
				}
				else
				{
					g_printerr ("Could not query segment to get playback rate\n");
				}

				// Print current position and total duration
				g_print ("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
						GST_TIME_ARGS (current), GST_TIME_ARGS (data.duration));

				// If seeking is enabled, we have not done it yet, and the time is right, seek
				if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND)
				{
					g_print ("\nReached 10s, performing seek with rate %3.1f...\n", data.rate);

					// If not changing rate, use seek simple
					if (requested_rate == 0.0)
					{
						// Seek simple will send rate of 0 which will be ignored
						gst_element_seek_simple (data.pipeline, GST_FORMAT_TIME,
								GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
					}
					else
					{
						gst_element_seek(data.pipeline, data.rate, GST_FORMAT_TIME,
							GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
							GST_SEEK_TYPE_END, 30 * GST_SECOND, GST_SEEK_TYPE_NONE, 0);
					}
					data.seek_done = TRUE;
				}
			}
		}
	} while (!data.terminate);

	// Close down
	gst_object_unref (bus);
	gst_element_set_state (data.pipeline, GST_STATE_NULL);
	gst_object_unref (data.pipeline);
	return 0;
}

/**
 *
 */
static void handle_message (CustomData *data, GstMessage *msg)
{
	GError *err;
	gchar *debug_info;
	//g_print("Got message type: %s\n", GST_MESSAGE_TYPE_NAME (msg));

	switch (GST_MESSAGE_TYPE (msg))
	{
	case GST_MESSAGE_ERROR:
		gst_message_parse_error (msg, &err, &debug_info);
		g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
		g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
		g_clear_error (&err);
		g_free (debug_info);
		data->terminate = TRUE;
		break;

	case GST_MESSAGE_EOS:
		g_print ("End-Of-Stream reached.\n");
		data->terminate = TRUE;
		break;

	case GST_MESSAGE_DURATION:
		// The duration has changed, mark thdata.pipeline,e current one as invalid
		data->duration = GST_CLOCK_TIME_NONE;
		break;

	case GST_MESSAGE_STATE_CHANGED:
	{
		GstState old_state, new_state, pending_state;
		gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
		if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline))
		{
			g_print ("Pipeline state changed from %s to %s:\n",
					gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

			// Remember whether we are in the PLAYING state or not
			data->playing = (new_state == GST_STATE_PLAYING);

			if (data->playing)
			{
				// We just moved to PLAYING. Check if seeking is possible
				GstQuery *query;
				gint64 start, end;
				query = gst_query_new_seeking (GST_FORMAT_TIME);
				if (gst_element_query (data->pipeline, query))
				{
					gst_query_parse_seeking (query, NULL, &data->seek_enabled, &start, &end);
					if (data->seek_enabled)
					{
						g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
								GST_TIME_ARGS (start), GST_TIME_ARGS (end));
					}
					else
					{
						g_print ("Seeking is DISABLED for this stream.\n");
					}
				}
				else
				{
					g_printerr ("Seeking query failed.");
				}
				gst_query_unref (query);
			}
		}
	} break;
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
			if (sscanf(argv[i], "rate=%f", &requested_rate) != 1)
			{
				g_printerr("Invalid rate arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested rate change to %4.1f\n", requested_rate);
			}
		}
		else if (strstr(argv[i], "wait=") != NULL)
		{
			if (sscanf(argv[i], "wait=%d", &waitSecs) != 1)
			{
				g_printerr("Invalid wait arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested wait secs to %d\n", waitSecs);
			}
		}
		else if (strstr(argv[i], "uri=") != NULL)
		{
			if (sscanf(argv[i], "uri=%s\n", &uri[0]) != 1)
			{
				g_printerr("Invalid uri arg specified: %s\n", argv[i]);
				return FALSE;

			}
			else
			{
				g_print("Set requested URI to %s\n", uri);
			}
		}
		else if (strstr(argv[i], "rrid=") != NULL)
		{
			if (sscanf(argv[i], "rrid=%d", &rrid) != 1)
			{
				g_printerr("Invalid rrid specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested rrid to %d\n", rrid);
			}
		}
		else if (strstr(argv[i], "host=") != NULL)
		{
			if (sscanf(argv[i], "host=%s\n", &host[0]) != 1)
			{
				g_printerr("Invalid host arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				g_print("Set requested host ip to %s\n", host);
			}
		}
		else if (strstr(argv[i], "playbin") != NULL)
		{
			usePlaybin = TRUE;
			g_print("Set to use playbin2\n");
		}
		else
		{
			g_printerr("Invalid option: %s\n", argv[i]);
			g_printerr("Usage:\t wait=x where x is secs, rate=y where y is desired rate\n");
			g_printerr("\t\t rrid=i where i is cds recording id, host=ip addr of server\n");
			g_printerr("\t\t uri=l where l is uri of desired content\n");
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * Create playbin2 pipeline
 */
static GstElement* create_playbin2_pipeline()
{
	GstElement* pipeline = NULL;
	char launchLine[256];
	char* line1 = "playbin2 uri=";

	sprintf(launchLine, "%s%s", line1, uri);

	g_print("Starting up playbin2 using line: %s\n", launchLine);
	pipeline = gst_parse_launch(launchLine, NULL);

	// Register to receive playbin2 source signal & call get property on sourc
	// to get supported playspeeds just like webkit would do
    g_signal_connect(pipeline, "notify::source", G_CALLBACK(on_source_changed), pipeline);

	return pipeline;
}

/**
 * Create pipeline which is built from manual assembly of components
 */
static GstElement* create_pipeline()
{
	// Create gstreamer elements
	GstElement* pipeline = gst_pipeline_new("manual-playbin");
	if (!pipeline)
	{
		g_printerr ("Pipeline element could not be created.\n");
		return NULL;
	}
	GstElement* dlnasrc  = gst_element_factory_make ("dlnasrc", "http-source");
	if (!dlnasrc)
	{
		g_printerr ("Dlnasrc element could not be created.\n");
		return NULL;
	}
	GstElement* dlnabin  = gst_element_factory_make ("dlnabin", "cablelabs demuxer");
	if (!dlnabin)
	{
		g_printerr ("dlnabin element could not be created.\n");
		return NULL;
	}
	GstElement* mpegvideoparse = gst_element_factory_make ("mpegvideoparse", "mpeg parser");
	if (!mpegvideoparse)
	{
		g_printerr ("mpegvideoparse element could not be created.\n");
		return NULL;
	}
	GstElement* mpeg2dec = gst_element_factory_make ("mpeg2dec",  "mpeg decoder");
	if (!mpeg2dec)
	{
		g_printerr ("mpeg2dec element could not be created.\n");
		return NULL;
	}

	GstElement* ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace",  "RGB YUV converter");
	if (!ffmpegcolorspace)
	{
		g_printerr ("ffmpegcolorspace element could not be created.\n");
		return NULL;
	}
	GstElement* videoscale = gst_element_factory_make ("videoscale", "videoscale");
	if (!videoscale)
	{
		g_printerr ("videoscale element could not be created.\n");
		return NULL;
	}
	GstElement* autovideosink = gst_element_factory_make ("autovideosink", "video-output");
	if (!autovideosink)
	{
		g_printerr ("autovideosink element could not be created.\n");
		return NULL;
	}
	GstElement* queue1 =  gst_element_factory_make ("queue", "queue1");
	GstElement* queue2 =  gst_element_factory_make ("queue", "queue2");
	if (!queue1 || !queue2)
	{
		g_printerr ("One of queue elements could not be created.\n");
		return NULL;
	}
	GstElement* textsink = gst_element_factory_make ("filesink", "text file");
	GstElement* captionsink = gst_element_factory_make ("filesink", "caption file");
	if (!textsink || !captionsink)
	{
		g_printerr ("One of filesink elements could not be created.\n");
		return NULL;
	}

	// Set properties as necessary on elements
	g_object_set(G_OBJECT(dlnasrc), "uri", uri, NULL);

	// Verify URI was properly set
	gchar* tmpUri = NULL;
	g_object_get(G_OBJECT(dlnasrc), "uri", &tmpUri, NULL);
	if ((tmpUri == NULL) || (strcmp(uri, tmpUri) != 0))
	{
		g_printerr ("Problems setting URI to: %s. Exiting.\n", uri);
		return NULL;
	}
	else
	{
		g_free(tmpUri);
	}

	g_object_set(G_OBJECT(textsink), "location", "demuxTextOutput", NULL);

	g_object_set(G_OBJECT(captionsink), "location", "captionsTextOutput", NULL);

	// Add all elements into the pipeline
	gst_bin_add_many (GST_BIN (pipeline),
			dlnasrc, dlnabin, queue1, mpegvideoparse, queue2, mpeg2dec, ffmpegcolorspace,
			videoscale, autovideosink, textsink, captionsink, NULL);

	// Link the elements together
	if (!gst_element_link_many (dlnasrc, dlnabin, queue1, mpegvideoparse, queue2, mpeg2dec, ffmpegcolorspace,
			videoscale, autovideosink, textsink, captionsink, NULL))
	{
		g_printerr ("Problems linking elements together\n");
		return NULL;
	}

	return pipeline;
}

/**
 * Callback when playbin2's source element changes
 */
static void on_source_changed(GstElement* element, GParamSpec* param, gpointer data)
{
	g_print("Notified of source change, gather supported rates\n");

	int i = 0;
	float rate = 0;
    GstElement* src = NULL;
    g_object_get(element, "source", &src, NULL);
    if (src != NULL)
    {
    	g_print("Got src from callback, getting supported rates property\n");

    	// Get supported rates property value which is a GArray
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
    	g_printerr("Unable to get src from callback\n");
    }
}
/*
static void old_main()
{
	// Wait for 10 seconds for playback to start up
	long secs = waitSecs;
	g_print("Waiting %ld secs for startup prior to rate change\n", secs);
	g_usleep(secs * 1000000L);

	// If requested, send rate change
	if (rate != 0)
	{
		g_print("Requesting rate change to %4.1f\n", rate);

		gint64 position;
		GstFormat format = GST_FORMAT_TIME;

		// Obtain the current position, needed for the seek event
		if (!gst_element_query_position(pipeline, &format, &position)) {
			g_printerr("Unable to retrieve current position.\n");
			return -1;
		}

		// Create the seek event
		event = gst_event_new_seek(rate, GST_FORMAT_TIME,
				GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
				GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, 0);

		// Send the event
		gst_element_send_event(pipeline, event);
	}
	else
	{
		g_print("Not requesting rate change\n");
	}

	// Initiate pause if rate was set to zero but wait time was not zero
	if ((rate == 0) && (waitSecs != 0))
	{
		g_print("Pausing pipeline for %d secs due to rate=0 & wait!=0\n", waitSecs);
		gst_element_set_state (pipeline, GST_STATE_PAUSED);

		g_usleep(secs * 1000000L);

		g_print("Resuming pipeline after %d sec pause\n", waitSecs);
		gst_element_set_state (pipeline, GST_STATE_PLAYING);
	}
	gboolean done = FALSE;
	g_print("Waiting for EOS or ERROR\n");
	while (!done)
	{
		msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
		if (msg != NULL)
		{
			g_print("Received msg type: %s\n", GST_MESSAGE_TYPE_NAME(msg));

			if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
			{
				GError *err = NULL;
				gchar *dbg_info = NULL;
				gst_message_parse_error (msg, &err, &dbg_info);
				g_print("ERROR from element %s: %s\n",
						GST_OBJECT_NAME (msg->src), err->message);
				g_print("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
				g_error_free(err);
				g_free(dbg_info);
			}
			gst_message_unref (msg);
			done = TRUE;
		}
	}
}
*/
