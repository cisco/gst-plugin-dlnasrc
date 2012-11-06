// COPYRIGHT_BEGIN
//  DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
//
//  Copyright (C) 2008-2009, Cable Television Laboratories, Inc.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, version 2. This program is distributed
//  in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
//  even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE. See the GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program.  If not, see  <http://www.gnu.org/licenses/>.
//
//  Please contact CableLabs if you need additional information or
//  have any questions.
//
//      CableLabs
//      858 Coal Creek Cir
//      Louisville, CO 80027-9750
//      303 661-9100
//      oc-mail@cablelabs.com
//
//  If you or the company you represent has a separate agreement with CableLabs
//  concerning the use of this code, your rights and obligations with respect
//  to this code shall be as set forth therein. No license is granted hereunder
//  for any other purpose.
// COPYRIGHT_END

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gstelements.h"
#include "gstsectionassembler.h"
#include "gstsectionfilter.h"
#include "gstpassthru.h"
#include "gstsectionsink.h"
//#include "gstdisplay.h"
#include "gsttransportsync.h"
#include "gstesassembler.h"
#include "gstmpegdecoder.h"
#include "gstpidfilter.h"
#include "gstebifbin.h"
#include "gstlivestreamdec.h"
//#include "gstindexingfilesink.h"
//#include "gsttrickplayfilesrc.h"

#include <libavcodec/avcodec.h>

struct _elements_entry
{
  const gchar *name;
  guint rank;
  GType (*type) (void);
  gboolean (*init) (GstPlugin*);
};

static struct _elements_entry _elements[] = {
//  {"display", GST_RANK_NONE, gst_display_get_type, NULL},
  {"sectionassembler", GST_RANK_NONE, gst_section_assembler_get_type, NULL},
  {"sectionfilter", GST_RANK_NONE, gst_section_filter_get_type, NULL},
  {"passthru", GST_RANK_NONE, gst_pass_thru_get_type, NULL},
  {"sectionsink", GST_RANK_NONE, gst_sectionsink_get_type, NULL},
  {"transportsync", GST_RANK_NONE, gst_transport_sync_get_type, NULL},
  {"esassembler", GST_RANK_NONE, gst_es_assembler_get_type, NULL},
  {"mpegdecoder", GST_RANK_NONE, gst_mpeg_decoder_get_type, NULL},
  {"pidfilter", GST_RANK_NONE, gst_pid_filter_get_type, NULL},
  {"ebifbin", GST_RANK_NONE, gst_ebif_bin_get_type, NULL},
  {"livestreamdec", GST_RANK_NONE, NULL, gst_live_stream_dec_plugin_init},
//  {"indexingfilesink", GST_RANK_NONE, gst_indexing_filesink_get_type, NULL},
//  {"trickplayfilesrc", GST_RANK_NONE, gst_trick_play_file_src_get_type, NULL},
  {NULL, 0, NULL, NULL},
};

static gboolean
plugin_init (GstPlugin * plugin)
{
  struct _elements_entry *my_elements = _elements;

  // Uncomment the following if you need to use the CableLabs GStreamer library
  // in standalone mode (i.e. gst-launch, gst-inspect) mode
  //gst_debug_add_log_function (gst_debug_log_default, NULL);

  // Non thread-safe initialization of FFMPEG library...
  avcodec_init();
  avcodec_register_all();

  while ((*my_elements).name) {

    if((*my_elements).type)
      if (!gst_element_register (plugin, (*my_elements).name, (*my_elements).rank, ((*my_elements).type)())) 
        return FALSE;

    if ((*my_elements).init)
      if (!((*my_elements).init)(plugin))
        return FALSE;

    my_elements++;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "goodelements",
    "CableLabs GStreamer elements",
    plugin_init,
    VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
