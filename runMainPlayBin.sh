#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10:/usr/lib/gstreamer-0.10
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:5,playbin2:4,uridecodebin:5,videosink:3,GST_EVENT:2,mpegtsdemux:3,mpeg2dec:3,mpegvparse:3,filesrc:3,mpeg2dec:3,basesrc:5,pushsrc:2
export GST_DEBUG
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
./src/main rrid=8 host=192.168.2.2 playbin seek wait=10
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 playbin switch position file 
