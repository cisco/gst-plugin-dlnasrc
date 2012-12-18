#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
export GST_DEBUG=dlnasrc:3,souphttpsrc:2,playbin2:3
./src/main
