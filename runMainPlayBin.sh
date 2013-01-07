#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:2,playbin2:4
export GST_DEBUG
./src/main rrid=3 host=192.168.2.2 rate=8 wait=15
