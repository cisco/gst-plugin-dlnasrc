#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10:/usr/lib/gstreamer-0.10
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:2,playbin2:3
export GST_DEBUG
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
./src/main rrid=8 host=192.168.2.2 playbin
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 playbin

