#!/bin/sh
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
#:/usr/lib/gstreamer-1.0
export GST_PLUGIN_PATH
#RUIH_GST_DTCP_DISABLE=true
#export RUIH_GST_DTCP_DISABLE
GST_DEBUG=*:1,dlnasrc:4,dtcpip:4,souphttpsrc:3,playbin:3,uridecodebin:3,videosink:3,GST_EVENT:1,GST_SEGMENT:1,GST_ELEMENT_FACTORY:1,GST_ELEMENT_PAD:1,mpegtsdemux:3,mpeg2dec:3,mpegvparse:3,filesrc:3,mpeg2dec:3,basesrc:1,pushsrc:1
export GST_DEBUG
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
./src/main rrid=9 host=192.168.2.2 playbin dtcp
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 playbin switch position seek file=clock.mpg file=two_videos.mkv file=scte20_7.mpg dtcp	
	
