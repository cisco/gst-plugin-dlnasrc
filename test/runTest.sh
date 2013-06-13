#!/bin/sh
#rm ./tmp/*.dot
#
TEST_FILE_URL_PREFIX=file:///home/landerson/gst/git/gst-plugins-cl/dlnasrc/test/
export TEST_FILE_URL_PREFIX
#
#RUIH_GST_DTCP_DISABLE=true
#export RUIH_GST_DTCP_DISABLE
#
RUIH_GST_DTCP_KEY_STORAGE=/media/RUIH_RI_2/test_keys
RUIH_GST_DTCP_DLL=/media/RUIH_RI_2/dtcp_greg/dtcpip_test_nodebug.so
#RUIH_GST_DTCP_DLL=/media/RUIH_RI_2/dtcp_greg/dtcpip_test_debug.so
#
#RUIH_GST_DTCP_KEY_STORAGE=/media/RUIH-RI-3/test_keys
#RUIH_GST_DTCP_DLL=/media/RUIH-RI-3/dtcpip_v1.1_Linux_test.so
#
#RUIH_GST_DTCP_KEY_STORAGE=/home/landerson/RUIHRI/dtcp/test_keys
#RUIH_GST_DTCP_DLL=/home/landerson/RUIHRI/dtcp/dtcpip_v1.1_Linux_test.so
#
export RUIH_GST_DTCP_KEY_STORAGE
export RUIH_GST_DTCP_DLL
#
GST_DEBUG=*:1,dlnasrc:4,souphttpsrc:4,mpeg2dec:3,tsdemux:4,mpegtsbase:4,mpegtspacketizer:4,dtcpip:5,GST_PADS:1
#uridecodebin:3,filesrc:5,dtcpip:4,playbin:3,passthru:1,\
#basesrc:3,pushsrc:5,baseparse:1,task:1,queue2:2,multiqueue:2,bin:1,\
#mpegtsdemux:4,xvimagesink:3,fakesink:5,structure:1,\
#GST_REGISTRY:1,GST_EVENT:3,GST_SEGMENT:5,GST_ELEMENT_FACTORY:1,GST_ELEMENT_PAD:1,\
#GST_PADS:2,GST_STATES:1,GST_BUFFER:1,GST_BUS:1,GST_REFCOUNTING:1,GST_TYPEFIND:1,\
#GST_PLUGIN_LOADING:1,GST_MEMORY:1
export GST_DEBUG
#
GST_DEBUG_NO_COLOR=1
export GST_DEBUG_NO_COLOR
#
# This doesn't seem to work here - need to export in terminal window using ./tmp as value
#export GST_DEBUG_DUMP_DOT_DIR=tmp
#
#./test file=tears.mpg
gst-git ./test host=192.168.0.106 rrid=33 wait=15 rate=0
#gst-git ./test uri=http://10.36.32.195:80/fxi/us.ts
#gst-git ./test uri=http://10.36.32.195/fxi/Ultimate-Stream-1280x720-5Mb-mpeg2v-ac3_0100_CC_Trim.ts
#gst-git ./test uri=http://192.168.2.16:818/true_lies.mpg
#gst-git ./test uri=http://192.168.2.16:8895/resource/1/MEDIA_ITEM/MPEG_TS_SD_KO_ISO-0/ORIGINAL
#gst-git ./test uri=http://dveo.com/downloads/TS-sample-files/San_Diego_Clip.ts
#gst-git ./test uri=http://192.168.0.106:60656/mal/I/AM2/1.mpeg
#gst-git ./test uri="http://192.168.2.16:8008/ocaphn/service?ocaploc=0x45a&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg"
#
# Bunch of possible cmd line args to include
#rate=8 wait=15 host=192.168.0.106 or 192.168.2.2 pipeline switch position seek file=clock.mpg file=two_videos.mkv file=scte20_7.mpg dtcp byte dot manual_elements	
#
# To debug with gdb debugger, use this line
#gdb --args gst-git ./test host=192.168.2.2 rrid=19
#gdb --args gst-git ./test file=0.mpg wait=10 rate=4

