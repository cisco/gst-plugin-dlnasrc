#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:3,souphttpsrc:2,playbin2:3
export GST_DEBUG
./src/main rrid=2 host=192.168.2.2 wait=10 rate=16
