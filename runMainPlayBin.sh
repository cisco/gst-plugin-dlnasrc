#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
#:/usr/lib/gstreamer-1.0
export GST_PLUGIN_PATH
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:3,playbin:4,uridecodebin:5,videosink:3,GST_EVENT:1,GST_ELEMENT_FACTORY:1,mpegtsdemux:3,mpeg2dec:3,mpegvparse:3,filesrc:3,mpeg2dec:3,basesrc:1,pushsrc:1
export GST_DEBUG
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
./src/main rrid=8 host=192.168.2.2 playbin
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 playbin switch position file seek wait=10
