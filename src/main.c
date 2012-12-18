#include <gst/gst.h>
#include <string.h>

int main(int argc, char *argv[]) 
{
	GstElement *pipeline;
	GstBus *bus;
	GstMessage *msg;
	GstEvent* event;

	int waitSecs = 0;
	int rate = 0;

	// Set default recording id for URI
	int rrid = 2;

	// Setup a default for host ip addr
	char host[256];
	strcpy(host, "192.168.2.2");

	// Set uri to NULL as default
	char uri[256];
	uri[0] = '\0';

	int i = 0;
	for (i = 1; i < argc; i++)
	{
		if (strstr(argv[i], "rate=") != NULL)
		{
			if (sscanf(argv[i], "rate=%d", &rate) != 1)
			{
				printf("Invalid rate arg specified: %s\n", argv[i]);
			}
			else
			{
				printf("Set requested rate change to %d\n", rate);
			}
		}
		else if (strstr(argv[i], "wait=") != NULL)
		{
			if (sscanf(argv[i], "wait=%d", &waitSecs) != 1)
			{
				printf("Invalid wait arg specified: %s\n", argv[i]);
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
			}
			else
			{
				printf("Set requested host ip to %s\n", host);
			}
		}
		else
		{
			printf("Invalid option: %s\n", argv[i]);
			printf("Usage:\t wait=x where x is secs, rate=y where y is desired rate\n");
			printf("\t\t rrid=i where i is cds recording id, host=ip addr of server\n");
			printf("\t\t uri=l where l is uri of desired content\n");
			return -1;
		}
	}

	// Initialize GStreamer
	gst_init (&argc, &argv);

	// Build the pipeline
	char* line1 = "playbin2 uri=";
	char* line2 = "http://";
	char* line3 = ":8008/ocaphn/recording?rrid=";
	char* line4 = "&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg";

	char launchLine[256];
	if (uri[0] == '\0')
	{
		sprintf(launchLine, "%s%s%s%s%d%s", line1, line2, host, line3, rrid, line4);
	}
	else
	{
		sprintf(launchLine, "%s%s", line1, uri);
	}
	printf("Starting up playbin2 using line: %s\n", launchLine);
	pipeline = gst_parse_launch(launchLine, NULL);

	// Start playing
	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	// Wait for 10 seconds for playback to start up
	long secs = waitSecs;
	printf("Waiting %ld secs for startup prior to rate change\n", secs);
	g_usleep(secs * 1000000L);

	// If requested, send rate change
	if (rate != 0)
	{
		printf("Requesting rate change to %d\n", rate);

		gint64 position;
		GstFormat format = GST_FORMAT_TIME;

		// Obtain the current position, needed for the seek event
		if (!gst_element_query_position(pipeline, &format, &position)) {
			printf("Unable to retrieve current position.\n");
			return -1;
		}

		// Create the seek event
		gdouble requested_rate = rate;
		event = gst_event_new_seek(requested_rate, GST_FORMAT_TIME,
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
