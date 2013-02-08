#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
#GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0
#gst-inspect-1.0 --gst-debug-level=1 --gst-plugin-spew --gst-plugin-load=$GST_PLUGIN_PATH/libgstdlnasrc.so dlnasrc
#gst-inspect-1.0 --gst-debug-level=4 --gst-plugin-spew --gst-plugin-load=$GST_PLUGIN_PATH/libgstmyfilter.so myfilter
gst-inspect-1.0 --gst-debug-level=1 --gst-plugin-spew theoradec

