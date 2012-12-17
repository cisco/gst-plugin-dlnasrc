#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
export GST_DEBUG=dlnabin:3,souphttpsrc:3,playbin2:3
./src/main wait=4
