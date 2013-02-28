#!/bin/sh
#GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
#
LD_LIBRARY_PATH=/usr/local/lib;
export LD_LIBRARY_PATH
#
GST_DEBUG=*:1
export GST_DEBUG
#
#gst-inspect-1.0 --gst-plugin-spew dlnasrc
#gst-inspect-0.10 --gst-plugin-spew dlnasrc
gst-inspect-0.10 --gst-plugin-spew dlnahttpsrc
