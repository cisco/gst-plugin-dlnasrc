#!/bin/sh
rm *.txt
GST_PLUGIN_PATH=/usr/local/lib/gstreamer-0.10
gst-launch \
	--gst-debug-level=1 --gst-mm=0.10 --gst-debug=dlnabin:3 --gst-debug=dtcpip:5\
        --gst-plugin-load=$GST_PLUGIN_PATH/libgstdlnabin.so \
        --gst-plugin-load=$GST_PLUGIN_PATH/libgstdtcpip.so \
	-v dlnabin dtcp_key_storage=/media/truecrypt1/dll/test_keys \
uri=http://192.168.0.106:8008/ocaphn/recording?rrid=3\&profile=MPEG_TS_SD_NA_ISO\&mime=video/mpeg \

#	-v dlnabin uri=http://192.168.2.3:8008/ocaphn/recording?rrid=1\&profile=MPEG_TS_SD_NA_ISO\&mime=video/mpeg 

