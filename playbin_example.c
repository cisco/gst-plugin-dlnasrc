/*
 * Note this code is just for FBY testing and was copied from:
 * http://felipec.wordpress.com/2008/01/19/gstreamer-hello-world/
 */
#include <gst/gst.h>
#include <stdbool.h>

static GMainLoop *loop;

static gboolean bus_call(GstBus *bus, GstMessage *msg, void *user_data)
{
  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS: {
    g_message("End-of-stream");
    g_main_loop_quit(loop);
    break;
  }
  case GST_MESSAGE_ERROR: {
    GError *err;
    gst_message_parse_error(msg, &err, NULL);
    g_error("%s", err->message);
    g_error_free(err);
    g_main_loop_quit(loop);
    break;
  }
  default:
    break;
  }
  return 1; // true
}
static void play_uri(const char *uri)
{
  GstElement *pipeline;
  GstBus *bus;
  loop = g_main_loop_new(NULL, FALSE);
  pipeline = gst_element_factory_make("playbin", "player");

  if (uri) {
    g_object_set(G_OBJECT(pipeline), "uri", uri, NULL);
  }
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, bus_call, NULL);
  gst_object_unref(bus);

  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
  g_main_loop_run(loop);
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);
  play_uri(argv[1]);
  return 0;
}

