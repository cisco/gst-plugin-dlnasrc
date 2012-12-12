#include <gst/gst.h>

int main(int argc, char *argv[]) 
{
	GstElement *pipeline;
	GstBus *bus;
	GstMessage *msg;
	GstEvent* event;

	gboolean changeRate = FALSE;
	if (argc > 1)
	{
		changeRate = TRUE;
	}

	// Initialize GStreamer
	gst_init (&argc, &argv);
	printf("Starting up playbin2\n");

	// Build the pipeline
	pipeline = gst_parse_launch("playbin2 uri=http://192.168.0.106:8008/ocaphn/recording?rrid=12&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg", NULL);

	// Start playing
	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	// Wait for 10 seconds for playback to start up
	printf("Waiting 10 secs for playback to start\n");
	g_usleep(50000000L);

	// If requested, send rate change
	if (changeRate)
	{
		  gint64 position;
		  GstFormat format = GST_FORMAT_TIME;

		  // Obtain the current position, needed for the seek event
		  if (!gst_element_query_position(pipeline, &format, &position)) {
		    printf("Unable to retrieve current position.\n");
		    return -1;
		  }

		  // Create the seek event
		  gdouble rate = 4.0;
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

	// Wait until error or EOS
	printf("Waiting for EOS or ERROR\n");
	bus = gst_element_get_bus (pipeline);
	msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

	// Free resources
	if (msg != NULL)
		gst_message_unref (msg);
	gst_object_unref (bus);
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (pipeline);
	return 0;
}
