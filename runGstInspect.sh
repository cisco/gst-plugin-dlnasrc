#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
gst-inspect --gst-plugin-load=$GST_PLUGIN_PATH/libgstdlnabin.so dlnabin
