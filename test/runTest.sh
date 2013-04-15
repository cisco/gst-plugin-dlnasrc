#!/bin/sh
rm ./tmp/*.dot
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
#GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
export GST_PLUGIN_PATH
#
TEST_FILE_URL_PREFIX=file:///home/landerson/RUIHRI/git/gst-plugins-cl/dlnasrc/test/
export TEST_FILE_URL_PREFIX
#
#RUIH_GST_DTCP_DISABLE=true
#export RUIH_GST_DTCP_DISABLE
#sudo apt-get install graphviz
#RUIH_GST_DTCP_KEY_STORAGE=/home/landerson/RUIHRI/git/gst-plugins-cl/dtcpip/
#RUIH_GST_DTCP_KEY_STORAGE=/media/RUIH-RI\ 1/test_keys
#RUIH_GST_DTCP_KEY_STORAGE=/media/truecrypt1/bin/test_keys
RUIH_GST_DTCP_KEY_STORAGE=/home/landerson/test_keys
export RUIH_GST_DTCP_KEY_STORAGE
#
#RUIH_GST_DTCP_DLL=/home/landerson/RUIHRI/git/gst-plugins-cl/dtcpip/test/dtcpip_mock.so
#RUIH_GST_DTCP_DLL=/media/RUIH-RI\ 1/dtcpip_v1.1_Linux_test.so
#RUIH_GST_DTCP_DLL=/media/truecrypt1/bin/dtcpip_test.so
RUIH_GST_DTCP_DLL=/home/landerson/dtcpip_v1.1_Linux_test.so
export RUIH_GST_DTCP_DLL
#
GST_DEBUG=*:1,dlnasrc:4,dtcpip:5,dlnasouphttpsrc:5,cldemux:5,playbin:3,passthru:1,\
basesrc:3,pushsrc:5,baseparse:1,task:1,queue2:2,multiqueue:2,bin:1,\
mpeg2dec:3,mpegvparse:3,tsdemux:3,mpegtsbase:1,uridecodebin:5,filesrc:5,\
mpegtsdemux:4,xvimagesink:3,fakesink:5,strucutre:1,\
GST_REGISTRY:1,GST_EVENT:2,GST_SEGMENT:2,GST_ELEMENT_FACTORY:1,GST_ELEMENT_PAD:1,\
GST_PADS:1,GST_STATES:1,GST_BUFFER:1,GST_BUS:1
export GST_DEBUG
#
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
#
GST_DEBUG_NO_COLOR=1
export GST_DEBUG_NO_COLOR
#
# This doesn't seem to work here - need to export in terminal window using ./tmp as value
#export GST_DEBUG_DUMP_DOT_DIR=tmp
#
./test file=true_lies.mpg dot
#./test host=192.168.2.16 rrid=33 wait=5 rate=8 query
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 pipeline switch position seek file=clock.mpg file=two_videos.mkv file=scte20_7.mpg dtcp	
#
# To debug with gdb debugger, use this line
#gdb --args ./test host=192.168.2.2 rrid=19
#gdb --args ./test file=0.mpg wait=10 rate=4

