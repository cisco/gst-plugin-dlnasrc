#include <gst/gst.h>
#include <string.h>

static gboolean process_cmd_line_args(int argc, char*argv[]);
static GstElement* create_playbin2_pipeline();
static GstElement* create_pipeline();

// Global vars for cmd line args
static int waitSecs = 0;
static gdouble rate = 0;
static int rrid = 2;
static char host[256];
static char uri[256];
static gboolean usePlaybin = FALSE;

int main(int argc, char *argv[]) 
{
	GstElement *pipeline;
	GstBus *bus;
	GstMessage *msg;
	GstEvent* event;

	// Assign default values
	strcpy(host, "192.168.2.2");
	uri[0] = '\0';

	if (!process_cmd_line_args(argc, argv))
	{
		printf("Exit due to problems with cmd line args\n");
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
		printf("Creating pipeline using playbin2\n");
		pipeline = create_playbin2_pipeline();
	}
	else
	{
		printf("Creating pipeline by assembling elements\n");
		pipeline = create_pipeline();
	}

	// Check that pipeline was properly created
	if (pipeline == NULL)
	{
		printf("Problems creating pipeline\n");
		return -1;
	}
	// Start playing
	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	// Wait for 10 seconds for playback to start up
	long secs = waitSecs;
	printf("Waiting %ld secs for startup prior to rate change\n", secs);
	g_usleep(secs * 1000000L);

	// If requested, send rate change
	if (rate != 0)
	{
		printf("Requesting rate change to %4.1f\n", rate);

		gint64 position;
		GstFormat format = GST_FORMAT_TIME;

		// Obtain the current position, needed for the seek event
		if (!gst_element_query_position(pipeline, &format, &position)) {
			printf("Unable to retrieve current position.\n");
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
		printf("Not requesting rate change\n");
	}

	// Initiate pause if rate was set to zero but wait time was not zero
	if ((rate == 0) && (waitSecs != 0))
	{
		printf("Pausing pipeline for %d secs due to rate=0 & wait!=0\n", waitSecs);
		gst_element_set_state (pipeline, GST_STATE_PAUSED);

		g_usleep(secs * 1000000L);

		printf("Resuming pipeline after %d sec pause\n", waitSecs);
		gst_element_set_state (pipeline, GST_STATE_PLAYING);
	}

	// Wait until error or EOS
	gboolean done = FALSE;
	printf("Waiting for EOS or ERROR\n");
	bus = gst_element_get_bus (pipeline);
	while (!done)
	{
		msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
		if (msg != NULL)
		{
			printf("Received msg type: %s\n", GST_MESSAGE_TYPE_NAME(msg));

			if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
			{
				GError *err = NULL;
				gchar *dbg_info = NULL;
				gst_message_parse_error (msg, &err, &dbg_info);
				printf("ERROR from element %s: %s\n",
						GST_OBJECT_NAME (msg->src), err->message);
				printf("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
				g_error_free(err);
				g_free(dbg_info);
			}
			gst_message_unref (msg);
			done = TRUE;
		}
	}

	// Close down
	gst_object_unref (bus);
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (pipeline);
	return 0;
}

static gboolean process_cmd_line_args(int argc, char *argv[])
{
	int i = 0;
	for (i = 1; i < argc; i++)
	{
		if (strstr(argv[i], "rate=") != NULL)
		{
			if (sscanf(argv[i], "rate=%lg", &rate) != 1)
			{
				printf("Invalid rate arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				printf("Set requested rate change to %4.1f\n", rate);
			}
		}
		else if (strstr(argv[i], "wait=") != NULL)
		{
			if (sscanf(argv[i], "wait=%d", &waitSecs) != 1)
			{
				printf("Invalid wait arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				printf("Set requested wait secs to %d\n", waitSecs);
			}
		}
		else if (strstr(argv[i], "uri=") != NULL)
		{
			if (sscanf(argv[i], "uri=%s\n", &uri[0]) != 1)
			{
				printf("Invalid uri arg specified: %s\n", argv[i]);
				return FALSE;

			}
			else
			{
				printf("Set requested URI to %s\n", uri);
			}
		}
		else if (strstr(argv[i], "rrid=") != NULL)
		{
			if (sscanf(argv[i], "rrid=%d", &rrid) != 1)
			{
				printf("Invalid rrid specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				printf("Set requested rrid to %d\n", rrid);
			}
		}
		else if (strstr(argv[i], "host=") != NULL)
		{
			if (sscanf(argv[i], "host=%s\n", &host[0]) != 1)
			{
				printf("Invalid host arg specified: %s\n", argv[i]);
				return FALSE;
			}
			else
			{
				printf("Set requested host ip to %s\n", host);
			}
		}
		else if (strstr(argv[i], "playbin") != NULL)
		{
			usePlaybin = TRUE;
			printf("Set to use playbin2\n");
		}
		else
		{
			printf("Invalid option: %s\n", argv[i]);
			printf("Usage:\t wait=x where x is secs, rate=y where y is desired rate\n");
			printf("\t\t rrid=i where i is cds recording id, host=ip addr of server\n");
			printf("\t\t uri=l where l is uri of desired content\n");
			return FALSE;
		}
	}
	return TRUE;
}

static GstElement* create_playbin2_pipeline()
{
	GstElement* pipeline = NULL;

	char launchLine[256];
	char* line1 = "playbin2 uri=";

	sprintf(launchLine, "%s%s", line1, uri);

	printf("Starting up playbin2 using line: %s\n", launchLine);
	pipeline = gst_parse_launch(launchLine, NULL);

	return pipeline;
}

static GstElement* create_pipeline()
{
	// Create gstreamer elements
	GstElement* pipeline = gst_pipeline_new("manual-playbin");
	GstElement* dlnasrc  = gst_element_factory_make ("dlnasrc", "http-source");
	GstElement* dlnabin  = gst_element_factory_make ("dlnabin", "cablelabs demuxer");
	GstElement* queue1 =  gst_element_factory_make ("queue", "queue1");
	GstElement* mpegvideoparse = gst_element_factory_make ("mpegvideoparse", "mpeg parser");
	GstElement* queue2 =  gst_element_factory_make ("queue", "queue2");
	GstElement* mpeg2dec = gst_element_factory_make ("mpeg2dec",  "mpeg decoder");
	GstElement* ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace",  "RGB YUV converter");
	GstElement* videoscale = gst_element_factory_make ("videoscale", "videoscale");
	GstElement* autovideosink = gst_element_factory_make ("autovideosink", "video-output");
	GstElement* textsink = gst_element_factory_make ("filesink", "text file");
	GstElement* captionsink = gst_element_factory_make ("filesink", "caption file");

	if (!pipeline || !dlnasrc || !dlnabin || !queue1 || !mpegvideoparse || !queue2 || !mpeg2dec ||
			!ffmpegcolorspace || !videoscale || !autovideosink || !textsink || !captionsink)
	{
		g_printerr ("One element could not be created. Exiting.\n");
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
	gst_element_link_many (dlnasrc, dlnabin, queue1, mpegvideoparse, queue2, mpeg2dec, ffmpegcolorspace,
			videoscale, autovideosink, textsink, captionsink, NULL);

	return pipeline;
}
