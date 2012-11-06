/*
 *  gst-launch filesrc location=/home/fancy/MpegTransport/bbb24p_00.ts ! cldemux ! mpegtsdemux name=demuxer demuxer. ! queue2 ! ffdec_mpeg2video ! ffmpegcolorspace ! videoscale ! autovideosink demuxer.! queue2 ! a52dec ! audioconvert ! audioresample ! autoaudiosink
 *  gst-launch filesrc location=/home/fancy/MpegTransport/Cablelabs_DPI_3_inserts_enhanced.mpg ! cldemux ! mpegtsdemux name=demuxer demuxer. ! queue2 ! ffdec_mpeg2video ! ffmpegcolorspace ! videoscale ! autovideosink demuxer.! queue2 ! a52dec ! audioconvert ! audioresample ! autoaudiosink
 *
 *  --- send data to filesink ---
 *  gst-launch filesrc location=/home/fancy/MpegTransport/Cablelabs_DPI_3_inserts_enhanced.mpg ! cldemux name=cl1 ! mpegtsdemux name=demuxer demuxer. ! queue2 ! ffdec_mpeg2video ! ffmpegcolorspace ! videoscale ! autovideosink  cl1.text_src ! filesink location=/home/fancy/cldemuxDebug.txt  demuxer.! queue2 ! a52dec ! audioconvert ! audioresample ! autoaudiosink
 *
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2012 CableLabs Inc <<TBD@cablelabs.com>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


/**
 * SECTION:element-cldemux
 *
 * FIXME:Describe cldemux here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! cldemux ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

/*=============================================================================
	Includes
=============================================================================*/
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstcldemux.h"



/*=============================================================================
  Externs from gstcldemux_stream.cc
=============================================================================*/
extern void cldemux_stream_initialize(void);

extern void cldemux_stream_addStream(int pid);

extern void cldemux_stream_finalize(void);


/*=============================================================================
	Constants
=============================================================================*/
GST_DEBUG_CATEGORY_STATIC (gst_cl_demux_debug);
#define GST_CAT_DEFAULT gst_cl_demux_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

#define SYNC_BYTE 0x47

// TODO: work with other packet sizes
#define TRANSPORT_PACKET_SIZE 188

#define MAX_PID  8192

// The max # descriptors that will be parsed in any given structure
#define MAX_DESCRIPTORS  8
#define MAX_DESCRIPTOR_DATA 256

#define MAX_STREAMS 32


/*=============================================================================
  Types
=============================================================================*/
typedef struct
{
  unsigned char tag;
  unsigned char length;
  unsigned char data[MAX_DESCRIPTOR_DATA];
} Descriptor;

typedef struct
{
  unsigned char type;
  unsigned short pid;
  unsigned short esLength;
  Descriptor descriptor;  // save the first one
} Stream;


/*=============================================================================
	Data
=============================================================================*/
/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate text_src_factory = GST_STATIC_PAD_TEMPLATE (
    "text_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );


GST_BOILERPLATE (GstClDemux, gst_cl_demux, GstElement, GST_TYPE_ELEMENT);

long numBuffers = 0;		// number of buffers processed
long packets = 0;       // number of transport packets

// We'll parse an entire buffer but save any fractional packets here.
unsigned char leftoverBuffer[TRANSPORT_PACKET_SIZE];

// The current packet
unsigned char packetBuffer[TRANSPORT_PACKET_SIZE];
unsigned int packetIndex;  // index to packetBuffer
unsigned char packetBuffering;    // = 0 if not currently storing to packetBuffer
                                  // = 1 incoming data is stored to packetBuffer


unsigned int leftoverIndex;   // index into leftoverBuffer
unsigned int leftoverSize;    // number of bytes in the leftoverBuffer

// index into main buffer
unsigned int bufIndex;

// data and size of main buffer
unsigned char *bufPtr;    // raw data from buffer
unsigned long bufSize;     // size of data in buffer

// track # packets of each PID
unsigned long pidData[MAX_PID];

// data for current transport packet
unsigned short packet_error;
unsigned short packet_payload_start;
unsigned short packet_priority;

// data from the most recent transport packet header
unsigned char continuity;
unsigned char scrambling;
unsigned char adaptation;
unsigned char pointer;

// data from the most recent table (PSI). This data is common to all tables such
// as PAT, PMT, etc
unsigned char tableId;
unsigned short sectionLength;
unsigned short transportStreamId;
unsigned char versionNumber;
unsigned char currentNext;
unsigned char sectionNumber;
unsigned char lastSectionNumber;

// data from the most recent PAT
unsigned short programNumber = 0;
unsigned short pmtPid = 0;

// most recent descriptors parsed
Descriptor descriptors[MAX_DESCRIPTORS];
int numDescriptors = 0;

// stream list from most recent pmt
Stream streams[MAX_STREAMS];
int numStreams = 0;

short eiss_pid_1 = -1;
short eiss_pid_2 = -1;


/*=============================================================================
	Local Functions
=============================================================================*/
static void gst_cl_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_cl_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_cl_demux_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_cl_demux_set_caps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_cl_demux_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn
gst_cl_demux_change_state (GstElement *element, GstStateChange transition);

static void
gst_cl_process_bytes ();

static int
gst_cl_get_byte(unsigned char *byte);

static unsigned int
gst_cl_bytes_left(void);

static int
gst_cl_get_short(unsigned short *data);

static void
gst_cl_process_pat(void);

static int
gst_cl_get_section_header (int bytesRead, int readTSI);

static void
gst_cl_process_pmt (void);

static void
gst_cl_process_eiss (int pid);

static int
gst_cl_get_transport_header (void);

static int
gst_cl_get_descriptor ();

static int
gst_cl_get_descriptors (int length);

static char *
gst_cl_get_descriptor_string (int tag);

static int
gst_cl_get_stream_info (int length);

static int
gst_cl_get_stream ();

static char *
gst_cl_get_stream_string (int tag);

static char *
gst_cl_get_app_cc_string (int type);

static char *
gst_cl_get_protocol_string (int type);



/* GObject vmethod implementations */

/*=============================================================================
void gst_cl_demux_base_init()
=============================================================================*/
static void
gst_cl_demux_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "ClDemux",
    "CableLabs Mpeg2 TS demuxer/analyzer",
    "CableLabs Mpeg2 TS demuxer/analyzer",
    "Fancy Younglove <<fancy@cardinalpeak.com>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_src_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}


/*=============================================================================
void gst_cl_demux_class_init()
=============================================================================*/
static void
gst_cl_demux_class_init (GstClDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_cl_demux_set_property;
  gobject_class->get_property = gst_cl_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_cl_demux_change_state;

  GST_ERROR("========= ClDemux class init =================\n");
}


/*=============================================================================
void gst_cl_demux_init()
=============================================================================*/
static void
gst_cl_demux_init (GstClDemux * filter, GstClDemuxClass * gclass)
{
   /* initialize the new element
   * instantiate pads and add them to element
   * set pad calback functions
   * initialize instance structure
   */
  int i;

  //-------------- sink pad
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");

  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_cl_demux_set_caps));

  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_cl_demux_chain));

  //--------------- src pad
  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  gst_pad_set_getcaps_function (filter->srcpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));


  //--------------- text src pad
  filter->textSrcpad = gst_pad_new_from_static_template (&text_src_factory, "text_src");

  gst_pad_set_getcaps_function (filter->textSrcpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

  GstCaps *caps = gst_caps_new_simple ("ANY", NULL);
  gst_pad_use_fixed_caps(filter->textSrcpad);
  gst_pad_set_caps (filter->textSrcpad, caps);
  gst_caps_unref (caps);
  gst_pad_set_event_function (filter->textSrcpad, GST_DEBUG_FUNCPTR (gst_cl_demux_src_event));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->textSrcpad);

  filter->silent = FALSE;

  if (filter->silent == FALSE)
  {
    g_print ("====== FBY: gst_cl_demux_init ====\n");
  }

  leftoverIndex = 0;
  leftoverSize = 0;
  packetIndex = 0;
  packetBuffering = 0;

  for (i=0; i<MAX_PID; i++)
  {
    pidData[i] = 0;
  }

  cldemux_stream_initialize();
}


/*=============================================================================
void gst_cl_demux_src_event()
=============================================================================*/
static gboolean
gst_cl_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstClDemux *demux = GST_CLDEMUX (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (demux, "************ got event %s",  gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event))
  {
    case GST_EVENT_SEEK:
      //res = gst_fluts_demux_handle_seek_push (demux, event);
      break;

    default:
      res = gst_pad_push_event (demux->sinkpad, event);
      break;
  }

  gst_object_unref (demux);

  return res;
}


/*=============================================================================
void gst_cl_demux_set_property()
=============================================================================*/
static void
gst_cl_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstClDemux *filter = GST_CLDEMUX (object);

  switch (prop_id) 
  {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*=============================================================================
void gst_cl_demux_get_property()
=============================================================================*/
static void
gst_cl_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstClDemux *filter = GST_CLDEMUX (object);

  switch (prop_id) 
  {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* GstElement vmethod implementations */

/*=============================================================================
gboolean gst_cl_demux_set_caps()
=============================================================================*/
static gboolean
gst_cl_demux_set_caps (GstPad * pad, GstCaps * caps)
{
  /* this function handles the link with other elements */
  GstClDemux *filter;
  GstPad *otherpad;
  gboolean result;

  gboolean gotsrcpad = (pad == filter->srcpad);
  gboolean gotsinkpad = (pad == filter->sinkpad);


  filter = GST_CLDEMUX (gst_pad_get_parent (pad));

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  gst_object_unref (filter);

  result = gst_pad_set_caps (otherpad, caps);


  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *mime;
  /* Since we’re an audio filter, we want to handle raw audio
  * and from that audio type, we need to get the samplerate and
  * number of channels. */
  mime = gst_structure_get_name (structure);
  g_print("********************* gst_cl_demux_set_caps mime=%s result=%d gotsrcpad=%d gotsinkpad=%d\n", mime, result, gotsrcpad, gotsinkpad);


  return result;
}


/*=============================================================================
GstStateChangeReturn gst_cl_demux_change_state()
=============================================================================*/
static GstStateChangeReturn
gst_cl_demux_change_state (GstElement *element, GstStateChange transition)
{
  int i;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstClDemux *filter = GST_CLDEMUX (element);

  switch (transition)
  {
    case GST_STATE_CHANGE_NULL_TO_READY:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_NULL_TO_READY\n");
      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state(element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
  {
    return ret;
  }

  switch (transition)
  {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      g_print ("FBY gst_cl_demux_chain: GST_STATE_CHANGE_READY_TO_NULL\n");

      // print final stats:
      g_print (" buffers processed=%d  packets=%d\n", numBuffers, packets);
      for (i=0; i<MAX_PID; i++)
      {
        if (pidData[i] > 10)
        {
          g_print("PID %d  # packets=%d\n", i, pidData[i]);
        }
      }

      cldemux_stream_finalize();
      g_print("cldemux finished, version = 1.1\n");

      // free memory
      break;
  }

return ret;
}


/*=============================================================================
GstFlowReturn gst_cl_demux_chain()
=============================================================================*/
static GstFlowReturn
gst_cl_demux_chain (GstPad * pad, GstBuffer * buf)
{
  // Do the actual processing here
  GstClDemux *filter;
  GstFlowReturn retval;

  filter = GST_CLDEMUX (GST_OBJECT_PARENT (pad));

  numBuffers++;

  bufPtr   = GST_BUFFER_DATA(buf);
  bufSize  = GST_BUFFER_SIZE(buf);
  bufIndex = 0;

  gst_cl_process_bytes();

  /* just push out the incoming buffer without touching it */
  //g_print ("FBY before pad_push, gst_cl_demux_chain: num=%d buffer size=%d \n", numBuffers, GST_BUFFER_SIZE (buf), retval);

  retval = gst_pad_push (filter->srcpad, buf);

  //g_print ("FBY after pad_push, gst_cl_demux_chain: num=%d buffer size=%d retval=%d\n", numBuffers, GST_BUFFER_SIZE (buf), retval);

  return retval;
}


/*=============================================================================
void gst_cl_get_byte()
=============================================================================*/
static int
gst_cl_get_byte(unsigned char *byte)
{
  // Get the next byte from the buffer
  // Get from the leftover buffer first because it has data from the previous
  // buffer
  //g_print("leftoverSize=%d leftoverIndex=%d bufIndex=%d bufSize=%d\n",
  //    leftoverSize, leftoverIndex, bufIndex, bufSize);

  if (leftoverSize && (leftoverIndex < leftoverSize))
  {
    *byte = leftoverBuffer[leftoverIndex++];
    return 1;
  }
  else if (bufIndex < bufSize)
  {
    *byte = bufPtr[bufIndex++];
    return 1;
  }

  return 0;
}


/*=============================================================================
void gst_cl_get_short()
=============================================================================*/
static int
gst_cl_get_short(unsigned short *data)
{
  unsigned char byte;
  unsigned short value;

  if (gst_cl_get_byte(&byte) == 0)
    return 0;   // end of data

  value = byte;
  value <<= 8;

  if (gst_cl_get_byte(&byte) == 0)
    return 1;   // end of data

  value += byte;

  *data = value;
  return 2;
}


/*=============================================================================
void gst_cl_bytes_left()
=============================================================================*/
static unsigned int
gst_cl_bytes_left(void)
{
  return leftoverSize - leftoverIndex + bufSize - bufIndex;
}


/*=============================================================================
void gst_cl_get_transport_header()
=============================================================================*/
static int
gst_cl_get_transport_header (void)
{
  int i;
  unsigned char byte;
  int bytesRead = 3;    // This should be called after sync byte and PID

  bytesRead += gst_cl_get_byte(&continuity);

  scrambling = continuity;
  scrambling >>= 6;
  scrambling &= 0x03;

  adaptation = continuity;
  adaptation >>= 4;
  adaptation &= 0x03;

  continuity &= 0x0F;

  if (adaptation != 1)
  {
    // Adaptation = 0, reserved
    // Adaptation = 1, payload only
    // Adaptation = 2, adaptation only
    // Adaptation = 3, adaptation, then payload

    // Skip over this packet. The first 4 bytes has already been read
    for (i=0; i<TRANSPORT_PACKET_SIZE-4; i++)
    {
      bytesRead += gst_cl_get_byte(&byte);
    }

    // TODO: handle adaptation fields
    return bytesRead;
  }

  // The remainder of the packet is payload. The first byte if packet_payload_start != 0
  // is the pointer
  if (packet_payload_start == 0)
  {
    // Skip over this packet. The first 4 bytes has already been read
    for (i=0; i<TRANSPORT_PACKET_SIZE-4; i++)
    {
      bytesRead += gst_cl_get_byte(&byte);
    }

    // TODO: handle multiple packet PATs
    return bytesRead;
  }

  bytesRead += gst_cl_get_byte(&pointer);

  // Limit size of pointer field so we don't read past the end of one packet
  if (pointer > TRANSPORT_PACKET_SIZE-13)
  {
    pointer = TRANSPORT_PACKET_SIZE-13;
  }

  for (i=0; i<pointer; i++)
  {
    bytesRead += gst_cl_get_byte(&byte);
  }

  return bytesRead;
}


/*=============================================================================
void gst_cl_get_section_header()
=============================================================================*/
static int
gst_cl_get_section_header (int bytesRead, int readTSI)
{
  unsigned char byte;

  bytesRead += gst_cl_get_byte(&tableId);
  bytesRead += gst_cl_get_short(&sectionLength);
  sectionLength &= 0x0FFF;

  if (readTSI)
  {
    bytesRead += gst_cl_get_short(&transportStreamId);
    bytesRead += gst_cl_get_byte(&versionNumber);
    currentNext = versionNumber & 0x01;
    versionNumber &= 0x3F;
    versionNumber >>= 1;
  }
  else
  {
    // For EISS table
    bytesRead += gst_cl_get_byte(&byte);
  }

  bytesRead += gst_cl_get_byte(&sectionNumber);
  bytesRead += gst_cl_get_byte(&lastSectionNumber);

  return bytesRead;
}


/*=============================================================================
void gst_cl_get_stream_info()
=============================================================================*/
static int
gst_cl_get_stream_info (int length)
{
  int bytesRead = 0;
  int count = MAX_STREAMS;

  numStreams = 0;

  while (bytesRead < length && count--)
  {
    bytesRead += gst_cl_get_stream();
  }

  return bytesRead;
}


/*=============================================================================
void gst_cl_get_stream()
=============================================================================*/
static int
gst_cl_get_stream ()
{
  int bytesRead = 0;
  unsigned char type;
  unsigned char byte;
  unsigned short pid;
  unsigned short esLength;
  int i;

  bytesRead += gst_cl_get_byte(&type);
  bytesRead += gst_cl_get_short(&pid);
  bytesRead += gst_cl_get_short(&esLength);

  pid &= 0x1FFF;
  esLength &= 0xFFF;

  streams[numStreams].type = type;
  streams[numStreams].pid = pid;
  streams[numStreams].esLength = esLength;

  // TODO parse descriptors
  // for now, skip over descriptors
  unsigned char tag;
  unsigned char length;
  unsigned char data;

  // Parse first descriptor for this stream if present
  if (esLength > 0)
  {
    bytesRead += gst_cl_get_byte(&tag);
    bytesRead += gst_cl_get_byte(&length);

    streams[numStreams].descriptor.tag = tag;
    streams[numStreams].descriptor.length = length;

    for (i=0; i<length; i++)
    {
      bytesRead += gst_cl_get_byte(&data);
      streams[numStreams].descriptor.data[i] = data;
    }

    // The remainder that was not parsed for the first descriptor is skipped
    for (i=0; i<esLength - 2 - length; i++)
    {
      bytesRead += gst_cl_get_byte(&byte);
    }
  }

  if (numStreams < MAX_STREAMS-1)
  {
    numStreams++;
  }

  return bytesRead;
}


/*=============================================================================
void gst_cl_get_descriptors()
=============================================================================*/
static int
gst_cl_get_descriptors (int length)
{
  int bytesRead = 0;
  int count = MAX_DESCRIPTORS;

  numDescriptors = 0;

  // length is the program_info_length and is the number of bytes of descriptors
  // return number of bytes read
  while (bytesRead < length && count--)
  {
    bytesRead += gst_cl_get_descriptor();
  }

  return bytesRead;
}


/*=============================================================================
void gst_cl_get_descriptor()
=============================================================================*/
static int
gst_cl_get_descriptor ()
{
  int bytesRead = 0;
  unsigned char tag;
  unsigned char length;
  unsigned char data;
  int i;

  bytesRead += gst_cl_get_byte(&tag);
  bytesRead += gst_cl_get_byte(&length);

  descriptors[numDescriptors].tag = tag;
  descriptors[numDescriptors].length = length;

  for (i=0; i<length; i++)
  {
    bytesRead += gst_cl_get_byte(&data);
    descriptors[numDescriptors].data[i] = data;
  }

  if (numDescriptors < MAX_DESCRIPTORS-1)
  {
    numDescriptors++;
  }

  return bytesRead;
}


/*=============================================================================
void gst_cl_process_pmt()
=============================================================================*/
static void
gst_cl_process_pmt (void)
{
  int bytesRead;        // should be 188 when the entire packet is read
  unsigned short pcr_pid;
  unsigned short program_info_length;
  int i, j;
  unsigned char byte;
  unsigned short streamInfoLength;

  static printPmts = 0;     // # of PMTs printed for debug


  bytesRead = gst_cl_get_transport_header();

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }

  bytesRead = gst_cl_get_section_header(bytesRead, 1);

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }

  bytesRead += gst_cl_get_short(&pcr_pid);
  pcr_pid &= 0x1FFF;

  bytesRead += gst_cl_get_short(&program_info_length);
  program_info_length &= 0xFFF;

  // Read descriptors
  bytesRead += gst_cl_get_descriptors(program_info_length);

  // Calculate length of stream info, subtract 9 for header bytes after the section length
  // subtract 4 for the CRC
  streamInfoLength = sectionLength - 9 - program_info_length - 4;

  bytesRead += gst_cl_get_stream_info(streamInfoLength);

  // Skip to end of packet:
  for (i=bytesRead; i<TRANSPORT_PACKET_SIZE; i++)
  {
    bytesRead += gst_cl_get_byte(&byte);
  }

  if (printPmts++ < 10)
  {
    g_print("\n");
    g_print("=========== PMT packet: ======\n");
    g_print("      payload_start=%d\n", packet_payload_start);
    g_print("         scrambling=%d\n", scrambling);
    g_print("         adaptation=%d\n", adaptation);
    g_print("         continuity=%d\n", continuity);
    g_print("----------------------\n");
    g_print("            tableId=%d\n", tableId);
    g_print("      sectionLength=%d\n", sectionLength);
    g_print("  transportStreamId=%d\n", transportStreamId);
    g_print("      versionNumber=%d\n", versionNumber);
    g_print("      sectionNumber=%d\n", sectionNumber);
    g_print("  lastSectionNumber=%d\n", lastSectionNumber);
    g_print("            pcr_pid=%d\n", pcr_pid);
    g_print("program_info_length=%d\n", program_info_length);

    for (i=0; i<numDescriptors; i++)
    {
      g_print("  descriptor[%d] tag=%d(%s) length=%d\n",
              i,
              descriptors[i].tag,
              gst_cl_get_descriptor_string(descriptors[i].tag),
              descriptors[i].length);

      if (descriptors[i].length > 16)
      {
        descriptors[i].length = 16;    // limit # bytes displayed
      }

      g_print("---> Data: ");
      for (j=0; j<descriptors[i].length; j++)
      {
        g_print("%.2x[%c] ", descriptors[i].data[j], isprint(descriptors[i].data[j]) ? descriptors[i].data[j] : '.' );
      }
      g_print("\n");
    }
    g_print("Note: find registration descriptors at: http://www.smpte-ra.org/mpegreg/mpegreg.html\n");

    for (i=0; i<numStreams; i++)
    {
      g_print("  stream[%d] type=%d(%s) pid=%d esLength=%d desc tag=%d desc len=%d\n",
             i,
             streams[i].type,
             gst_cl_get_stream_string(streams[i].type),
             streams[i].pid,
             streams[i].esLength,
             streams[i].descriptor.tag,
             streams[i].descriptor.length);

      cldemux_stream_addStream(streams[i].pid);

      g_print("  ---> Data: ");
      for (j=0; j<streams[i].descriptor.length; j++)
      {
        g_print("%.2x[%c] ", streams[i].descriptor.data[j],
            isprint(streams[i].descriptor.data[j]) ? streams[i].descriptor.data[j] : '.' );
      }
      g_print("\n");

      if ((streams[i].type == 0xC0) && (eiss_pid_1 == -1))
      {
        eiss_pid_1 = streams[i].pid;
      }
      else if ((streams[i].type == 0xC0) && (eiss_pid_2 == -1))
      {
        eiss_pid_2 = streams[i].pid;
      }

    }

    g_print("\n");

  }
}


/*=============================================================================
void gst_cl_process_eiss()
=============================================================================*/
#define MAX_LOCATOR_STRING 256

static void
gst_cl_process_eiss (int pid)
{
  int bytesRead;        // should be 188 when the entire packet is read
  int i;
  unsigned char byte;
  unsigned char protocolVersionMajor;
  unsigned char protocolVersionMinor;
  unsigned short appType;
  unsigned char appId[6];
  unsigned char platformIdLength;
  int descriptorLengthTotal;
  static int eissPrinted = 0;

  // EISS app info descriptor
  unsigned char descriptorTag;
  unsigned char descriptorLength;
  unsigned char appControlCode;
  unsigned char appVersionMajor;
  unsigned char appVersionMinor;
  unsigned char maxProtocolVersionMajor;
  unsigned char maxProtocolVersionMinor;
  unsigned char testFlag;
  unsigned char applicationPriority;

  unsigned short locatorType;
  unsigned short locatorLength;
  unsigned char locatorString[MAX_LOCATOR_STRING];
  unsigned short esLength;


  bytesRead = gst_cl_get_transport_header();

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }

  bytesRead = gst_cl_get_section_header(bytesRead, 0);

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }

  if (eissPrinted >= 10)
  {
    // Skip to end of packet:
    for (i=bytesRead; i<TRANSPORT_PACKET_SIZE; i++)
    {
      bytesRead += gst_cl_get_byte(&byte);
    }

    return;
  }

  if (tableId == 0xE0)
  {
    g_print("--- EISS table E0 pid=%d\n", pid);
  }
  else if (tableId == 0xE2)
  {
    g_print("--- EISS table E2 pid=%d\n", pid);
  }
  else
  {
    //g_print("--- Unknown table %d pid=%d\n", tableId, pid);

    // Skip to end of packet:
    for (i=bytesRead; i<TRANSPORT_PACKET_SIZE; i++)
    {
      bytesRead += gst_cl_get_byte(&byte);
    }

    return;
  }

  eissPrinted++;    // only print this table 10 times


  bytesRead += gst_cl_get_byte(&protocolVersionMajor);
  bytesRead += gst_cl_get_byte(&protocolVersionMinor);
  bytesRead += gst_cl_get_short(&appType);

  // Read app id
  for (i=0; i<6; i++)
  {
    bytesRead += gst_cl_get_byte(&appId[i]);
  }

  bytesRead += gst_cl_get_byte(&platformIdLength);

  // Read and discard platform ids
  for (i=0; i<platformIdLength; i++)
  {
    bytesRead += gst_cl_get_byte(&byte);
  }

  // At this point only the descriptors and the 4 byte CRC should be left
  descriptorLengthTotal = sectionLength - bytesRead - 4;

  // Read the first descriptor
  bytesRead += gst_cl_get_byte(&descriptorTag);
  bytesRead += gst_cl_get_byte(&descriptorLength);

  g_print("   major=%d minor=%d appType=%d appId=%.2x %.2x %.2x %.2x %.2x %.2x  platformIdLength=%d descLength=%d\n",
      protocolVersionMajor, protocolVersionMinor, appType,
      appId[0], appId[1], appId[2], appId[3], appId[4], appId[5],
      platformIdLength, descriptorLengthTotal);

  g_print("   descriptor:  tag=0x%X length=%d\n", descriptorTag, descriptorLength);

  if (descriptorTag == 0xE0)
  {
    bytesRead += gst_cl_get_byte(&appControlCode);
    bytesRead += gst_cl_get_byte(&appVersionMajor);
    bytesRead += gst_cl_get_byte(&appVersionMinor);
    bytesRead += gst_cl_get_byte(&maxProtocolVersionMajor);
    bytesRead += gst_cl_get_byte(&maxProtocolVersionMinor);
    bytesRead += gst_cl_get_byte(&testFlag);
    bytesRead += gst_cl_get_byte(&byte);
    bytesRead += gst_cl_get_byte(&byte);
    bytesRead += gst_cl_get_byte(&byte);
    bytesRead += gst_cl_get_byte(&applicationPriority);

    // Get locator see EBIF I06 11.14
    bytesRead += gst_cl_get_short(&locatorType);
    locatorLength = locatorType;
    locatorLength &= 0x03FF;
    locatorType >>= 10;

    g_print("    appControlCode=%d[%s] app ver major=%d minor=%d  protocol ver major=%d minor=%d test=%d prioriy=%d\n",
        appControlCode, gst_cl_get_app_cc_string(appControlCode), appVersionMajor, appVersionMinor,
        protocolVersionMajor, protocolVersionMinor, testFlag, applicationPriority);

    g_print("    locatorType=%d[%s] locatorLength=%d\n", locatorType, gst_cl_get_protocol_string(locatorType), locatorLength);

    if (locatorLength > sectionLength - bytesRead - 4)
    {
      locatorLength = sectionLength - bytesRead - 4;
    }

    bytesRead += gst_cl_get_short(&esLength);

    for (i=0; i < esLength; i++)
    {
      bytesRead += gst_cl_get_byte(&locatorString[i]);
    }
    locatorString[i] = 0;

    if (locatorLength > 0)
    {
      g_print("    esLength=%d initial_resource_locator: %s\n", esLength, locatorString);
    }
  }

  // Skip to end of packet:
  for (i=bytesRead; i<TRANSPORT_PACKET_SIZE; i++)
  {
    bytesRead += gst_cl_get_byte(&byte);
  }

  g_print("\n");

}


/*=============================================================================
void gst_cl_get_protocol_string()
=============================================================================*/
static char *
gst_cl_get_protocol_string (int type)
{
  char *str;

  switch (type)
  {
  case 0: str="EBI Heap Locator";               break;
  case 1: str="EBI Table Locator";              break;
  case 2: str="DSM-CC U-N Data Carousel Module";  break;
  case 3: str="DSM-CC U-U Object Carousel Object Locator";    break;
  case 4: str="URI Locator";                    break;
  case 5: str="Mpeg Service Locator";           break;
  case 6: str="Mpeg Component Locator";         break;
  case 7: str="ETV Stream Event Locator";       break;
  case 8: str="Indirect URI Locator";           break;
  case 9: str="Application Locator";            break;
  case 10: str="Async Message Event Locator";   break;

  default:
    str="Reserved";
    break;
  }

  return str;
}


/*=============================================================================
void gst_cl_get_app_cc_string()
=============================================================================*/
static char *
gst_cl_get_app_cc_string (int type)
{
  char *str;

  switch (type)
  {
  case 0: str = "reserved";     break;
  case 1: str = "AUTOSTART";    break;
  case 2: str = "PRESENT";      break;
  case 3: str = "DESTROY";      break;

  case 4:
  case 5:
  case 6:
  case 7:
    str = "SUSPEND";
    break;

  default:
    str = "reserved";
    break;
  }

  return str;
}


/*=============================================================================
void gst_cl_get_stream_string()
=============================================================================*/
static char *
gst_cl_get_stream_string (int type)
{
  char *str;

  switch (type)
  {
   case 0x00: str="ITU-T | ISO/IEC Reserved"; break;
   case 0x01: str="ISO/IEC 11172 Video"; break;
   case 0x02: str="ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream"; break;
   case 0x03: str="ISO/IEC 11172 Audio"; break;
   case 0x04: str="ISO/IEC 13818-3 Audio"; break;
   case 0x05: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections"; break;
   case 0x06: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data"; break;
   case 0x07: str="ISO/IEC 13522 MHEG"; break;
   case 0x08: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC"; break;
   case 0x09: str="ITU-T Rec. H.222.1"; break;
   case 0x0A: str="ISO/IEC 13818-6 type A"; break;
   case 0x0B: str="ISO/IEC 13818-6 type B"; break;
   case 0x0C: str="ISO/IEC 13818-6 type C"; break;
   case 0x0D: str="ISO/IEC 13818-6 type D"; break;
   case 0x0E: str="ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary"; break;
   case 0x0F: str="ISO/IEC 13818-7 Audio with ADTS transport syntax"; break;
   case 0x10: str="ISO/IEC 14496-2 Visual"; break;
   case 0x11: str="ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1"; break;
   case 0x12: str="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets"; break;
   case 0x13: str="ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC14496_sections."; break;
   case 0x14: str="ISO/IEC 13818-6 Synchronized Download Protocol"; break;

   default:
     if (type == 129)
     {
       str="ATSC AC-3 Audio";
     }
     else if (type == 0x86)
     {
       str="SCTE-35 splice information table";
     }
     else if (type == 0xC0)
     {
       str = "EISS ?";
     }
     else if ((type >= 0x15) && (type <= 0x7F))
     {
       str="Reserved";
     }
     else
     {
       str = "User private";
     }
     break;
  }

return str;
}


/*=============================================================================
void gst_cl_get_descriptor_string()
=============================================================================*/
static char *
gst_cl_get_descriptor_string (int tag)
{
   char *str;

   switch (tag)
   {
   case 0: str="Reserved"; break;
   case 1: str="Reserved"; break;
   case 2: str="video_stream_descriptor"; break;
   case 3: str="audio_stream_descriptor"; break;
   case 4: str="hierarchy_descriptor"; break;
   case 5: str="registration_descriptor"; break;
   case 6: str="data_stream_alignment_descriptor"; break;
   case 7: str="target_background_grid_descriptor"; break;
   case 8: str="Video_window_descriptor"; break;
   case 9: str="CA_descriptor"; break;
   case 10: str="ISO_639_language_descriptor"; break;
   case 11: str="System_clock_descriptor"; break;
   case 12: str="Multiplex_buffer_utilization_descriptor"; break;
   case 13: str="Copyright_descriptor"; break;
   case 14: str="Maximum_bitrate_descriptor"; break;
   case 15: str="Private_data_indicator_descriptor"; break;
   case 16: str="Smoothing_buffer_descriptor"; break;
   case 17: str="STD_descriptor"; break;
   case 18: str="IBP_descriptor"; break;
   case 19: str="ISO/IEC 13818-6"; break;
   case 20: str="ISO/IEC 13818-6"; break;
   case 21: str="ISO/IEC 13818-6"; break;
   case 22: str="ISO/IEC 13818-6"; break;
   case 23: str="ISO/IEC 13818-6"; break;
   case 24: str="ISO/IEC 13818-6"; break;
   case 25: str="ISO/IEC 13818-6"; break;
   case 26: str="ISO/IEC 13818-6"; break;
   case 27: str="MPEG-4_video_descriptor"; break;
   case 28: str="MPEG-4_audio_descriptor"; break;
   case 29: str="IOD_descriptor"; break;
   case 30: str="SL_descriptor"; break;
   case 31: str="FMC_descriptor"; break;
   case 32: str="External_ES_ID_descriptor"; break;
   case 33: str="MuxCode_descriptor"; break;
   case 34: str="FmxBufferSize_descriptor"; break;
   case 35: str="MultiplexBuffer_descriptor"; break;

   default:
     if ((tag >= 36) && (tag <= 63))
     {
       str = "Reserved";
     }
     else
     {
       str = "User Private";
     }
     break;
   }

   return str;
}


/*=============================================================================
void gst_cl_process_pat()
=============================================================================*/
static void
gst_cl_process_pat (void)
{
  unsigned char byte;

  unsigned short crc32;
  static printPats = 0;     // # of PATs printed for debug

  int numPids;
  int i;
  int bytesRead;        // should be 188 when the entire packet is read
                        // when function starts, 3 bytes have been read

  pmtPid = 0;


  bytesRead = gst_cl_get_transport_header();

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }

//  bytesRead += gst_cl_get_byte(&continuity);
//
//  scrambling = continuity;
//  scrambling >>= 6;
//  scrambling &= 0x03;
//
//  adaptation = continuity;
//  adaptation >>= 4;
//  adaptation &= 0x03;
//
//  continuity &= 0x0F;
//
//  if (adaptation != 1)
//  {
//    // Adaptation = 0, reserved
//    // Adaptation = 1, payload only
//    // Adaptation = 2, adaptation only
//    // Adaptation = 3, adaptation, then payload
//
//    // Skip over this packet. The first 4 bytes has already been read
//    for (i=0; i<TRANSPORT_PACKET_SIZE-4; i++)
//    {
//      bytesRead += gst_cl_get_byte(&byte);
//    }
//
//    // TODO: handle adaptation fields
//    return;
//  }
//
//  // The remainder of the packet is payload. The first byte if packet_payload_start != 0
//  // is the pointer
//  if (packet_payload_start == 0)
//  {
//    // Skip over this packet. The first 4 bytes has already been read
//    for (i=0; i<TRANSPORT_PACKET_SIZE-4; i++)
//    {
//      bytesRead += gst_cl_get_byte(&byte);
//    }
//
//    // TODO: handle multiple packet PATs
//    return;
//  }
//
//  bytesRead += gst_cl_get_byte(&pointer);
//
//  // Limit size of pointer field so we don't read past the end of one packet
//  if (pointer > TRANSPORT_PACKET_SIZE-13)
//  {
//    pointer = TRANSPORT_PACKET_SIZE-13;
//  }
//
//  for (i=0; i<pointer; i++)
//  {
//    bytesRead += gst_cl_get_byte(&byte);
//  }

//  bytesRead += gst_cl_get_byte(&tableId);
//  bytesRead += gst_cl_get_short(&sectionLength);
//  sectionLength &= 0x0FFF;
//  bytesRead += gst_cl_get_short(&transportStreamId);
//  bytesRead += gst_cl_get_byte(&versionNumber);
//  currentNext = versionNumber & 0x01;
//  versionNumber &= 0x3F;
//  versionNumber >>= 1;
//
//  bytesRead += gst_cl_get_byte(&sectionNumber);
//  bytesRead += gst_cl_get_byte(&lastSectionNumber);

  bytesRead = gst_cl_get_section_header(bytesRead, 1);

  if (bytesRead >= TRANSPORT_PACKET_SIZE)
  {
    return;
  }


  // Read the program number / network or PMT PIDs
  numPids = (sectionLength - 9) / 4;

  // Todo: save a list of PMT pids. For now, just use the last one
  for (i=0; i<numPids; i++)
  {
    bytesRead += gst_cl_get_short(&programNumber);
    bytesRead += gst_cl_get_short(&pmtPid);

    pmtPid &= 0x1FFF;
  }

  bytesRead += gst_cl_get_short(&crc32);  // not using this at present

  // Read & discard remainder of PAT
  for (i=bytesRead; i<TRANSPORT_PACKET_SIZE; i++)
  {
    bytesRead += gst_cl_get_byte(&byte);
  }

  if (printPats++ < 10)
  {
    g_print("\n");
    g_print("========= PAT packet: ======\n");
    g_print("    payload_start=%d\n", packet_payload_start);
    g_print("       scrambling=%d\n", scrambling);
    g_print("       adaptation=%d\n", adaptation);
    g_print("       continuity=%d\n", continuity);
    g_print("--------------------\n");
    g_print("          tableId=%d\n", tableId);
    g_print("    sectionLength=%d\n", sectionLength);
    g_print("transportStreamId=%d\n", transportStreamId);
    g_print("    versionNumber=%d\n", versionNumber);
    g_print("    sectionNumber=%d\n", sectionNumber);
    g_print("lastSectionNumber=%d\n", lastSectionNumber);
    g_print("    programNumber=%d\n", programNumber);
    g_print("           pmtPid=%d\n", pmtPid);

    if (pmtPid != 0)
    {
      cldemux_stream_addStream(pmtPid);
    }
  }
}


/*=============================================================================
void gst_cl_process_bytes()
=============================================================================*/
static void
gst_cl_process_bytes ()
{
  int i;
  int num;
  unsigned short pid;
  unsigned char byte;
  int bytesLeft;


  do
  {
    num = gst_cl_get_byte(&byte);    // The first byte of the packet

    //---- new section
    if (num && (byte == SYNC_BYTE))
    {
      if (packetIndex == TRANSPORT_PACKET_SIZE)
      {
        // The buffer should contain a complete TS packet. This is the first byte of the
        // next packet
        packetIndex = 1;    // 0 contains the SYNC bytes
        cldemux_stream_process_packet(packetBuffer);
      }
      else if (packetIndex < TRANSPORT_PACKET_SIZE)
      {
        // The buffer contains an incomplete packet.
        // This SYNC may be a random byte in the packet, or it may be the start
        // of a new packet. We'll assume it is a non-sync byte in the middle of the packet. If this
        // is false, then the next sync byte will occur at an index > TRANSPORT_PACKET_SIZE, and
        // we'll skip the packet.
        packetBuffer[packetIndex++] = byte;
      }
      else
      {
        // packetIndex > TRANSPORT_PACKET_SIZE
        // The initial SYNC was probably not a valid SYNC. This resulted in a packet that was too large
        // in this case the current SYNC is probably the start of a new packet. Start over with this SYNC.
        packetIndex = 1;
      }
    }
    else if (num)
    {
      // Save non-sync byte if buffering
      if (packetIndex < TRANSPORT_PACKET_SIZE)
      {
        packetBuffer[packetIndex] = byte;
      }
      packetIndex++;
    }


#if 0
    //---- original section
    if (num && (byte == SYNC_BYTE))
    {
      bytesLeft = gst_cl_bytes_left();

      if (bytesLeft >= TRANSPORT_PACKET_SIZE - 1)
      {
        gst_cl_get_short(&pid);

        packet_error         = (pid & 0x8000) ? 1 : 0;
        packet_payload_start = (pid & 0x4000) ? 1 : 0;
        packet_priority      = (pid & 0x2000) ? 1 : 0;

        pid &= 0x1FFF;

        if (pid < MAX_PID)
        {
          pidData[pid]++;
        }

        if (pid == 0)
        {
          gst_cl_process_pat();
        }
        else if (pid == pmtPid)
        {
          gst_cl_process_pmt();
        }
        else if ((pid == eiss_pid_1) || (pid == eiss_pid_2))
        {
          gst_cl_process_eiss(pid);
        }
        else
        {
          // Skip over this packet. The first 3 bytes has already been read
          for (i=0; i<TRANSPORT_PACKET_SIZE-3; i++)
          {
            gst_cl_get_byte(&byte);
          }
        }

        packets++;
      }
      else
      {
        // Not enough bytes left for one packet. Save the remainder,
        // for the next buffer processing step
        leftoverSize = bytesLeft + 1;
        leftoverIndex = 0;
        leftoverBuffer[0] = SYNC_BYTE;    // add back in

        for (i=0; i<bytesLeft; i++)
        {
          leftoverBuffer[i+1] = bufPtr[bufIndex+i];
        }

        return; // done with this buffer
      }
    }
#endif


  } while (num > 0);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
/*=============================================================================
gboolean cldemux_init()
=============================================================================*/
static gboolean
cldemux_init (GstPlugin * cldemux)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template cldemux' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_cl_demux_debug, "cldemux",
      0, "Template cldemux");

  return gst_element_register (cldemux, "cldemux", GST_RANK_NONE,
      GST_TYPE_CLDEMUX);
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstcldemux"
#endif

/* gstreamer looks for this structure to register cldemuxs
 *
 * exchange the string 'Template cldemux' with your cldemux description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cldemux",
    "CableLabs Mpeg 2 TS Demuxer/Analyzer",
    cldemux_init,
    "cldemux version 1",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)















