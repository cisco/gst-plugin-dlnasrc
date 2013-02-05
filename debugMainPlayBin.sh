#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:5,playbin:3
export GST_DEBUG
gdb --args ./src/main rrid=8 host=192.168.2.2 playbin seek wait=10
#./src/main rrid=8 host=192.168.2.2 playbin seek wait=10 file
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 playbin switch position file #rate=8 wait=15 
