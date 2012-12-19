/*
  This src adds DLNA playback capabilities to souphttpsrc
 */

/**
 * SECTION:element-dlnasrc
 *
 * HTTP/DLNA client source
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch ...
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <gst/gst.h>
#include <glib-object.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK(s) (void)close(s)

#include "gstdlnasrc.h"

// GStreamer debugging facilities
//
GST_DEBUG_CATEGORY_STATIC (gst_dlna_src_debug);
#define GST_CAT_DEFAULT gst_dlna_src_debug

// Define the boilerplate type stuff to reduce typos and code size.
// Defines the get_type method and the parent_class static variable.
// Args are: type,type_as_function,parent_type,parent_type_macro
//
//GST_BOILERPLATE (GstDlnaSrc, gst_dlna_src, GstElement, GST_TYPE_BIN);
static void _do_Init(GType type);
GST_BOILERPLATE_FULL(GstDlnaSrc, gst_dlna_src, GstElement, GST_TYPE_BIN, _do_Init);
// GstPushSrc, GST_TYPE_PUSH_SRC, _do_init)

/* props */
enum
{
	PROP_0,
	PROP_URI,
	PROP_DTCP_KEY_STORAGE,
	PROP_CL_NAME,
	//...
};

#define DLNA_SRC_CL_NAME "dlnasrc"

// Constant names for elements in this src
#define ELEMENT_NAME_HTTP_SRC "http-source"
#define ELEMENT_NAME_FILE_SINK "file-sink"
#define ELEMENT_NAME_DTCP_DECRYPTER "dtcp-decrypter"

#define MAX_HTTP_BUF_SIZE 1024
static const char CRLF[] = "\r\n";
static const char COLON[] = ":";

// Constant strings identifiers for header fields in HEAD response
static const char* HEAD_RESPONSE_HDRS[] = {
		"HTTP/",					// 0
		"VARY", 					// 1
		"TIMESEEKRANGE.DLNA.ORG", 	// 2
		"TRANSFERMODE.DLNA.ORG",	// 3	// {"BYTES", BYTE_RANGE_TYPE},
		"DATE", 					// 4
		"CONTENT-TYPE",				// 5
		"SERVER", 					// 6
		"TRANSFER-ENCODING", 		// 7
		"CONTENTFEATURES.DLNA.ORG", // 8
		"CONTENT-RANGE.DTCP.COM", 	// 9
		"PRAGMA", 					// 10
		"CACHE-CONTROL", 			// 11
};

// Constants which represent indices in HEAD_RESPONSE_HDRS string array
// NOTE: Needs to stay in sync with HEAD_RESPONSE_HDRS
#define HDR_IDX_HTTP 0
#define HDR_IDX_VARY 1
#define HDR_IDX_TIMESEEKRANGE 2
#define HDR_IDX_TRANSFERMODE 3
#define HDR_IDX_DATE 4
#define HDR_IDX_CONTENT_TYPE 5
#define HDR_IDX_SERVER 6
#define HDR_IDX_TRANSFER_ENCODING 7
#define HDR_IDX_CONTENTFEATURES 8
#define HDR_IDX_DTCP_RANGE 9
#define HDR_IDX_PRAGMA 10
#define HDR_IDX_CACHE_CONTROL 11

// Count of field headers in HEAD_RESPONSE_HDRS along with HDR_IDX_* constants
static const gint HEAD_RESPONSE_HDRS_CNT = 12;

// Subfield headers within TIMESEEKRANGE.DLNA.ORG
static const char* TIME_SEEK_HDRS[] = {
		"NPT",						// 0
		"BYTES", 					// 1
};
#define HDR_IDX_NPT 0
#define HDR_IDX_BYTES 1

// Subfield headers within CONTENTFEATURES.DLNA.ORG
static const char* CONTENT_FEATURES_HDRS[] = {
		"DLNA.ORG_PN", 				// 0
		"DLNA.ORG_OP", 				// 1
		"DLNA.ORG_PS",				// 2
		"DLNA.ORG_FLAGS",			// 3
};
#define HDR_IDX_PN 0
#define HDR_IDX_OP 1
#define HDR_IDX_PS 2
#define HDR_IDX_FLAGS 3

// Subfield headers with CONTENT-TYPE
static const char* CONTENT_TYPE_HDRS[] = {
		"DTCP1HOST",				// 0
		"DTCP1PORT", 				// 1
		"CONTENTFORMAT",			// 2
		"APPLICATION/X-DTCP1" 		// 3
};
#define HDR_IDX_DTCP_HOST 0
#define HDR_IDX_DTCP_PORT 1
#define HDR_IDX_CONTENT_FORMAT 2
#define HDR_IDX_APP_DTCP 3


/**
 * DLNA Flag parameters defined in DLNA spec
 * primary flags - 8 hexadecimal digits representing 32 binary flags
 * protocol info dlna org flags represented by primary flags followed
 * by reserved data of 24 hexadecimal digits (zeros)
 */
static const gint SP_FLAG = 1 << 31; //(Sender Paced Flag), content src is clock
static const gint LOP_NPT = 1 << 30; //(Limited Operations Flags: Time-Based Seek)
static const gint LOP_BYTES = 1 << 29; //(Limited Operations Flags: Byte-Based Seek)
static const gint PLAYCONTAINER_PARAM = 1 << 28; //(DLNA PlayContainer Flag)
static const gint S0_INCREASING = 1 << 27; //(UCDAM s0 Increasing Flag) (content has no fixed beginning)
static const gint SN_INCREASING = 1 << 26; //(UCDAM sN Increasing Flag) (content has no fixed ending)
static const gint RTSP_PAUSE = 1 << 25; //(Pause media operation support for RTP Serving Endpoints)
static const gint TM_S = 1 << 24; //(Streaming Mode Flag) - av content must have this set
static const gint TM_I = 1 << 23; //(Interactive Mode Flag)
static const gint TM_B = 1 << 22; //(Background Mode Flag)
static const gint HTTP_STALLING = 1 << 21; //(HTTP Connection Stalling Flag)
static const gint DLNA_V15_FLAG = 1 << 20; //(DLNA v1.5 versioning flag)
static const gint LP_FLAG = 1 << 16; //(Link Content Flag)
static const gint CLEARTEXTBYTESEEK_FULL_FLAG = 1 << 15;  // Support for Full RADA ClearTextByteSeek hdr
static const gint LOP_CLEARTEXTBYTES = 1 << 14; // Support for Limited RADA ClearTextByteSeek hdr

static const int RESERVED_FLAGS_LENGTH = 24;


// Structure describing details of this element, used when initializing element
//
const GstElementDetails gst_dlna_src_details
= GST_ELEMENT_DETAILS("HTTP/DLNA client source 11/19/12 7:26 PM",
		"Source/Network",
		"Receive data as a client via HTTP with DLNA extensions",
		"Eric Winkelman <e.winkelman@cablelabs.com>");

// Description of a pad that the element will (or might) create and use
//
static GstStaticPadTemplate gst_dlna_src_src_pad_template =
		GST_STATIC_PAD_TEMPLATE (
				"src",					// name for pad
				GST_PAD_SRC,				// direction of pad
				GST_PAD_ALWAYS,			// indicates if pad exists
				GST_STATIC_CAPS ("ANY")	// Supported types by this element (capabilities)
		);

// **********************
// Method declarations associated with gstreamer framework function pointers
//
static void gst_dlna_src_dispose (GObject* object);

static void gst_dlna_src_set_property (GObject* object, guint prop_id,
		const GValue* value, GParamSpec* spec);

static void gst_dlna_src_get_property (GObject* object, guint prop_id,
		GValue* value, GParamSpec* spec);

static gboolean gst_dlna_src_sink_event(GstPad* pad, GstEvent* event);


// **********************
// Method declarations associated with autoplugging, type finding, & ranking
//
static void gst_dlna_src_uri_handler_init(gpointer, gpointer);


// **********************
// Local method declarations
//
static gboolean dlna_src_set_uri(GstDlnaSrc *dlna_src, const gchar* value);

static gboolean dlna_src_init_uri(GstDlnaSrc *dlna_src, const gchar* value);

static gboolean dlna_src_dtcp_setup(GstDlnaSrc *dlna_src);

static gboolean dlna_src_open_socket(GstDlnaSrc *dlna_src);

static gboolean dlna_src_parse_uri(GstDlnaSrc *dlna_src);

static gboolean dlna_src_formulate_head_request(GstDlnaSrc *dlna_src);

static gboolean dlna_src_issue_head_request(GstDlnaSrc *dlna_src);

static gboolean dlna_src_close_socket(GstDlnaSrc *dlna_src);

static gboolean dlna_src_head_response_parse(GstDlnaSrc *dlna_src);

static gint dlna_src_head_response_get_field_idx(GstDlnaSrc *dlna_src, gchar* field_str);

static gboolean dlna_src_head_response_assign_field_value(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_time_seek(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_content_features(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_profile(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_operations(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_playspeeds(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_flags(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_dtcp_range(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_parse_content_type(GstDlnaSrc *dlna_src, gint idx, gchar* field_str);

static gboolean dlna_src_head_response_is_flag_set(GstDlnaSrc *dlna_src, gchar* flags_str, gint flag);

static gboolean dlna_src_head_response_init_struct(GstDlnaSrc *dlna_src);

static gboolean dlna_src_head_response_struct_to_str(GstDlnaSrc *dlna_src);

static gboolean dlna_src_handle_seek_event(GstDlnaSrc *dlna_src, GstEvent* event);

static gboolean dlna_src_is_rate_supported(GstDlnaSrc *dlna_src, gdouble rate);

static gboolean dlna_src_request_new_rate(GstDlnaSrc *dlna_src, gdouble rate, gint64 start);

static gboolean dlna_src_http_src_set_rate(GstDlnaSrc *dlna_src, gdouble rate, gint64 start);

// *TODO* - is this really needed???
void
gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure *closure,
		GValue *return_value G_GNUC_UNUSED,
		guint n_param_values,
		const GValue *param_values,
		gpointer invocation_hint G_GNUC_UNUSED,
		gpointer marshal_data);


/*
 * Registers element details with the plugin during, which is part of
 * the GObject system. This function will be set for this GObject
 * in the function where you register the type with GLib.
 *
 * @param	gclass	gstreamer representation of this element
 */
static void
gst_dlna_src_base_init (gpointer gclass)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

	gst_element_class_set_details_simple
	(element_class,
			"HTTP/DLNA client source",
			"Source/Network",
			"Receive data as a client via HTTP with DLNA extensions",
			"Eric Winkelman <e.winkelman@cablelabs.com>");

	// Add the src pad template
	gst_element_class_add_pad_template
	(element_class,
			gst_static_pad_template_get(&gst_dlna_src_src_pad_template));
}

/*
 * Initializes (only called once) the class associated with this element from within
 * gstreamer framework.  Installs properties and assigns specific
 * methods for function pointers.  Also defines detailed info
 * associated with this element.  The purpose of the *_class_init
 * method is to register the plugin with the GObject system.
 *
 * @param	klass	class representation of this element
 */
static void
gst_dlna_src_class_init (GstDlnaSrcClass * klass)
{
	GObjectClass *gobject_klass;
	GstElementClass *gstelement_klass;
	//GstSrcClass *gstsrc_klass;

	gobject_klass = (GObjectClass *) klass;
	gstelement_klass = (GstElementClass *) klass;
	//gstsrc_klass = (GstSrcClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	gobject_klass->set_property = gst_dlna_src_set_property;
	gobject_klass->get_property = gst_dlna_src_get_property;

	g_object_class_install_property (gobject_klass, PROP_URI,
			g_param_spec_string ("uri", "Stream URI",
					"Sets URI A/V stream",
					NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_klass, PROP_CL_NAME,
			g_param_spec_string ("cl_name", "CableLabs name",
					"CableLabs name used to verify playbin2 selected source",
					NULL, G_PARAM_READABLE));

	g_object_class_install_property (gobject_klass, PROP_DTCP_KEY_STORAGE,
			g_param_spec_string ("dtcp_key_storage", "dtcp_key_storage",
					"Directory that contains client's keys",
					"/media/truecrypt1/dll/test_keys", G_PARAM_READWRITE));


	gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_dlna_src_dispose);

	gst_element_class_set_details (gstelement_klass, &gst_dlna_src_details);
}

/*
 * Initializes a specific instance of this element, called when object
 * is created from within gstreamer framework.
 *
 * @param dlna_src	specific instance of element to intialize
 * @param gclass	class representation of this element
 */
static void
gst_dlna_src_init (GstDlnaSrc * dlna_src,
		GstDlnaSrcClass * gclass)
{
    GST_LOG_OBJECT(dlna_src, "Initializing");

    GstPad *pad;

	// Initialize source name
	dlna_src->cl_name = g_strdup(DLNA_SRC_CL_NAME);

	// Initialize play rate to 1.0
	dlna_src->rate = 1.0;

	// Create source element
	dlna_src->http_src = gst_element_factory_make ("souphttpsrc", ELEMENT_NAME_HTTP_SRC);
	if (!dlna_src->http_src) {
		GST_ERROR_OBJECT(dlna_src, "The source element could not be created. Exiting.\n");
		exit(1);
	}

	// Add source element to the src
	gst_bin_add(GST_BIN(&dlna_src->bin), dlna_src->http_src);

	// Create src ghost pad of dlna src using http src so playbin will recognize element as a src
	pad = gst_element_get_static_pad(dlna_src->http_src, "src");
	if (!pad)
	{
		GST_ERROR_OBJECT(dlna_src, "Could not get pad for souphttpsrc. Exiting.\n");
		exit(1);
	}
	dlna_src->src_pad = gst_ghost_pad_new("src", pad);
	gst_pad_set_active (dlna_src->src_pad, TRUE);
	gst_element_add_pad(GST_ELEMENT (&dlna_src->bin), dlna_src->src_pad);
	gst_object_unref (pad);

	// Configure event function on sink pad before adding pad to element
	gst_pad_set_event_function(dlna_src->src_pad, (GstPadEventFunction)gst_dlna_src_sink_event);

    GST_LOG_OBJECT(dlna_src, "Initialization complete");
}

/**
 * Called by framework when tearing down pipeline
 *
 * @param object  element to destroy
 */
static void
gst_dlna_src_dispose (GObject * object)
{
	GstDlnaSrc* dlna_src = GST_DLNA_SRC (object);

    GST_INFO_OBJECT(dlna_src, " Disposing the dlna src");

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * Method called by framework to set this element's properties
 *
 * @param	object	set property of this element
 * @param	prop_id	identifier of property to set
 * @param	value	set property to this value
 * @param	pspec	description of property type
 */
static void 
gst_dlna_src_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec)
{
	GstDlnaSrc *dlna_src = GST_DLNA_SRC (object);

    GST_INFO_OBJECT(dlna_src, "Setting property: %d", prop_id);

    switch (prop_id) {

	case PROP_URI:
	{
		if (!dlna_src_set_uri(dlna_src, g_value_get_string(value)))
		{
		    GST_ERROR_OBJECT(dlna_src, "Failed to set URI property");
		}
		break;
	}
	case PROP_DTCP_KEY_STORAGE:
	{
		if (dlna_src->dtcp_key_storage)
		{
			g_free(dlna_src->dtcp_key_storage);
		}
		dlna_src->dtcp_key_storage = g_value_dup_string (value);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * Retrieves current value of property associated with supplied element instance.
 *
 * @param	object	get property value of this element
 * @param	prop_id	get property identified by this supplied id
 * @param	value	returned current value of property
 * @param	pspec	description of property type
 */
static void
gst_dlna_src_get_property (GObject * object, guint prop_id, GValue * value,
		GParamSpec * pspec)
{
	GstDlnaSrc *dlna_src = GST_DLNA_SRC (object);
	GST_LOG_OBJECT(dlna_src, "Getting property: %d", prop_id);

	switch (prop_id) {

	case PROP_URI:
		g_value_set_pointer(value, dlna_src->uri);
		break;
    case PROP_DTCP_KEY_STORAGE:
      g_value_set_string (value, dlna_src->dtcp_key_storage);
      break;
	case PROP_CL_NAME:
		g_value_set_string(value, dlna_src->cl_name);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/**
 * Processes the supplied sink event
 *
 * @param pad	where event was received
 * @param event	received event
 *
 * @return	true if this element handles event, false otherwise
 */
static gboolean
gst_dlna_src_sink_event(GstPad    *pad,
						GstEvent  *event)
{
	gboolean ret;
	GstDlnaSrc *dlna_src = GST_DLNA_SRC(gst_pad_get_parent(pad));

	switch (GST_EVENT_TYPE (event))
	{

	case GST_EVENT_SEEK:
		dlna_src_handle_seek_event(dlna_src, event);
		break;

	default:
		// Just call the default handler
		ret = gst_pad_event_default (pad, event);
		break;
	}

	return ret;
}

/*********************************************/
/**********                         **********/
/********** GstUriHandler INTERFACE **********/
/**********                         **********/
/*********************************************/
static void
_do_Init(GType type)
{
    static const GInterfaceInfo urihandler_info =
    {
        gst_dlna_src_uri_handler_init,
        NULL,
        NULL
    };

    g_type_add_interface_static(type, GST_TYPE_URI_HANDLER, &urihandler_info);
}

static GstURIType
gst_dlna_src_uri_get_type(void)
{
	return GST_URI_SRC;
}

static gchar **
gst_dlna_src_uri_get_protocols(void)
{
	static gchar *protocols[] = { "http", "https", NULL };
	return protocols;
}

static const gchar *
gst_dlna_src_uri_get_uri(GstURIHandler* handler)
{
	GstDlnaSrc *dlna_src = GST_DLNA_SRC(handler);
	return dlna_src->uri;
}

static gboolean
gst_dlna_src_uri_set_uri(GstURIHandler* handler, const gchar* uri)
{
	GstDlnaSrc *dlna_src = GST_DLNA_SRC(handler);

	GST_INFO_OBJECT(dlna_src, "uri handler called to set uri: %s, current: %s",
			uri, dlna_src->uri);

	return dlna_src_set_uri(dlna_src, uri);
}

static void
gst_dlna_src_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *)g_iface;

	iface->get_type = gst_dlna_src_uri_get_type;
	iface->get_protocols = gst_dlna_src_uri_get_protocols;
	iface->get_uri = gst_dlna_src_uri_get_uri;
	iface->set_uri = gst_dlna_src_uri_set_uri;
}

/**
 * Perform action necessary when seek event is received
 *
 * @param dlna_src		this element
 * @param seek_event	seek event which has been received
 *
 * @return	true if this event has been handled, false otherwise
 */
static gboolean
dlna_src_handle_seek_event(GstDlnaSrc *dlna_src, GstEvent* event)
{
	gdouble rate;
	GstFormat format;
	GstSeekFlags flags;
    GstSeekType start_type;
    gint64 start;
    GstSeekType stop_type;
    gint64 stop;

    // Make sure a URI has been set and HEAD response received
    if ((dlna_src->uri == NULL) || (dlna_src->head_response == NULL) ||
    	(dlna_src->head_response->content_features == NULL))
    {
		GST_INFO_OBJECT(dlna_src, "No URI and/or HEAD response info, unable to handle event");
    	return FALSE;
    }

	// Parse event received
	gst_event_parse_seek(event, (gdouble*)&rate, (GstFormat*)&format,
	                     (GstSeekFlags*)&flags, (GstSeekType*)&start_type,
	                     (gint64*)&start, (GstSeekType*)&stop_type, (gint64*)&stop);

	GST_LOG_OBJECT(dlna_src,
	"Sink event rate: %4.1f, format: %s, flags: %d, start type: %d,  start: %lld, stop type: %d, stop: %lld",
			rate, gst_format_get_name(format), flags, start_type, start, stop_type, stop);

	// Check if rate change has been requested
	if (rate != dlna_src->rate)
	{
		// If this event is a byte type, request new rate
		if (format == GST_FORMAT_BYTES)
		{
			GST_INFO_OBJECT(dlna_src, "New rate of %4.1f has been requested, current: %4.1f",
				rate, dlna_src->rate);

			// Verify requested rate is supported
			if (dlna_src_is_rate_supported(dlna_src, rate))
			{
				GST_INFO_OBJECT(dlna_src, "New rate of %4.1f is supported by server", rate);

				dlna_src_request_new_rate(dlna_src, rate, start);
			}
			else
			{
				GST_INFO_OBJECT(dlna_src, "New rate of %4.1f is not supported by server", rate);
			}
		}
		else
		{
			// *TODO* - is this right???
			// Ignoring time based seeks since bytes will be more accurate
		}
	}
	else
	{
		// *TODO* - handle new time & byte position requests
	}

	// *TODO* - should we always return true???
	return TRUE;
}

/**
 * Determines if current rate is supported by server based on current
 * URI and HEAD response.
 *
 * @param dlna_src		this element
 * @param rate			requested rate
 *
 * @return	true if requested rate is supported by server, false otherwise
 */
static gboolean
dlna_src_is_rate_supported(GstDlnaSrc *dlna_src, gdouble rate)
{
	gboolean is_supported = FALSE;

	// Look through list of server supported playspeeds to see if rate is supported
	int i = 0;
	for (i = 0; i < dlna_src->head_response->content_features->playspeeds_cnt; i++)
	{
		if (dlna_src->head_response->content_features->playspeeds[i] == rate)
		{
			is_supported = TRUE;
			break;
		}
	}

	return is_supported;
}

/**
 *
 */
static gboolean
dlna_src_request_new_rate(GstDlnaSrc *dlna_src, gdouble rate, gint64 start)
{
	GST_INFO_OBJECT(dlna_src, "requesting new rate");

	// Get parent which will be playbin2
	GstElement* playbin2 = (GstElement*)gst_element_get_parent(dlna_src);

	/*
	// Send EOS since changing rate
	GstEvent* eos_event = gst_event_new_eos();
	if (!gst_element_send_event(playbin2, eos_event))
	{
		GST_WARNING_OBJECT(dlna_src, "EOS event was not handled");
	}
	else
	{
		GST_INFO_OBJECT(dlna_src, "Sent EOS");
	}
	GstEvent* flush_start = gst_event_new_flush_start();
	if (!gst_element_send_event(playbin2, flush_start))
	{
		GST_WARNING_OBJECT(dlna_src, "Flush start event was not handled");
	}

	GstEvent* flush_stop = gst_event_new_flush_stop();
	if (!gst_element_send_event(playbin2, flush_stop))
	{
		GST_WARNING_OBJECT(dlna_src, "Flush stop event was not handled");
	}
	*/
	GST_INFO_OBJECT(dlna_src, "pausing playbin2");
	gst_element_set_state(playbin2, GST_STATE_NULL);
	//gst_element_set_state(dlna_src->http_src, GST_STATE_PAUSED);
	GST_INFO_OBJECT(dlna_src, "paused playbin2");

	if (1)
	{
		if (!dlna_src_http_src_set_rate(dlna_src, rate, start))
		{
			GST_WARNING_OBJECT(dlna_src, "Problems setting http src rate");
		}
	}

	// *TODO* - remove this since it should not be necessary
    g_object_set(G_OBJECT(dlna_src->http_src), "location", dlna_src->uri, NULL);

	// Set playbin state back to playing
	GST_INFO_OBJECT(dlna_src, "Resuming playbin2");
	gst_element_set_state(playbin2, GST_STATE_PLAYING);
	GST_INFO_OBJECT(dlna_src, "Resumed playbin2");

	return TRUE;
}

/**
 *
 */
static gboolean
dlna_src_http_src_set_rate(GstDlnaSrc *dlna_src, gdouble rate, gint64 start)
{
	// Assign play rate to supplied rate
	dlna_src->rate = rate;

	// Setup header to request playspeed
	// *TODO* - make these constants
	gchar* ps_field_name = "PlaySpeed.dlna.org";
	gchar* ps_field_value_prefix = "speed = ";
	gchar ps_field_value[64];
	sprintf((gchar*)&ps_field_value[0], "%s%d", ps_field_value_prefix, (int)rate);
	GST_INFO_OBJECT(dlna_src, "Setting playspeed header value: %s", ps_field_value);

	// Setup range header
	gchar* range_field_name = "Range";
	gchar* range_field_value_prefix = "bytes = ";
	gchar range_field_value[64];
	sprintf((gchar*)&range_field_value[0], "%s%lld-", range_field_value_prefix, start);
	GST_INFO_OBJECT(dlna_src, "Setting range header value: %s", range_field_value);

	// Create GstStructure & GValue which contains extra headers
	GstStructure* extraHdrsStruct = gst_structure_new("extraHdrsStruct",
									ps_field_name,
									G_TYPE_STRING,
									&ps_field_value,
									range_field_name,
									G_TYPE_STRING,
									&range_field_value,
									NULL);

	GValue structValue = { 0 };
	g_value_init(&structValue, GST_TYPE_STRUCTURE);
	gst_value_set_structure(&structValue, extraHdrsStruct);

	GST_INFO_OBJECT(dlna_src, "setting extra headers of http src property");

	g_object_set_property(G_OBJECT(dlna_src->http_src), "extra-headers", &structValue);

	GST_INFO_OBJECT(dlna_src, "set extra hdrs of http src");

    //gst_structure_free(extraHdrsStruct);

	return TRUE;
}

/**
 * Perform actions necessary based on supplied URI
 *
 * @param dlna_src	this element
 * @param value		specified URI to use
 */
static gboolean
dlna_src_set_uri(GstDlnaSrc *dlna_src, const gchar* value)
{
	// Determine if this is a new URI or just another request using same URI
	if ((dlna_src->uri == NULL) || (strcmp(value, dlna_src->uri) != 0))
	{
		GST_INFO_OBJECT(dlna_src, "Need to initialize URI");

		// Setup for new URI
		if (!dlna_src_init_uri(dlna_src, value))
		{
			GST_ERROR_OBJECT(dlna_src, "Problems initializing URI");
			if (dlna_src->uri) {
				free(dlna_src->uri);
			}
			dlna_src->uri = NULL;
			return FALSE;
		}
	}
	GST_INFO_OBJECT(dlna_src, "Successfully setup URI: %s", dlna_src->uri);

	// *TODO* - force rate to non-1x rate
	// Don't think RI server supports non-1x rate at the get go
	//dlna_src_http_src_set_rate(dlna_src, 32.0, 0);

	// Set the URI
	g_object_set(G_OBJECT(dlna_src->http_src), "location", dlna_src->uri, NULL);

	// Setup elements based on HEAD response
	// Use flag or profile name starts with DTCP (
	// *TODO* - add check for profile name starting with DTCP
	if (dlna_src->head_response->content_features->flag_link_protected_set)
	{
		if (!dlna_src_dtcp_setup(dlna_src))
		{
			GST_ERROR_OBJECT(dlna_src, "Problems setting up dtcp elements\n");
			return FALSE;
		}
	}
	else
	{
		GST_INFO_OBJECT(dlna_src, "No DTCP setup required\n");
	}

	return TRUE;
}

/**
 * Initialize the URI which includes formulating a HEAD request
 * and parsing the response to get needed info about the URI.
 *
 * @param dlna_src	this element
 * @param value		specified URI to use
 */
static gboolean
dlna_src_init_uri(GstDlnaSrc *dlna_src, const gchar* value)
{
	// Set the uri in the src
	if (dlna_src->uri)
	{
		GST_INFO_OBJECT(dlna_src, "Resetting URI from: %s, to: %s", dlna_src->uri, value);
		free(dlna_src->uri);
	}
	else
	{
		GST_INFO_OBJECT(dlna_src, "Initializing URI to %s", value);
	}
	dlna_src->uri = g_strdup(value);

	// Parse URI to get socket info & content info to send head request
	if (!dlna_src_parse_uri(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems parsing URI");
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}

	// Open socket to send HEAD request
	if (!dlna_src_open_socket(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems creating socket to send HEAD request\n");
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}

	// Formulate HEAD request
	if (!dlna_src_formulate_head_request(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems formulating HEAD request\n");
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}

	// Send HEAD Request and read response
	if (!dlna_src_issue_head_request(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems sending and receiving HEAD request\n");
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}

	// Close socket
	if (!dlna_src_close_socket(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems closing socket used to send HEAD request\n");
	}

	// Parse HEAD response to gather info about URI content item
	if (!dlna_src_head_response_parse(dlna_src))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems parsing HEAD response\n");
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}

	// Make sure return code from HEAD response is OK
	if ((dlna_src->head_response->ret_code != 200) && (dlna_src->head_response->ret_code != 201))
	{
		GST_ERROR_OBJECT(dlna_src, "Error code received in HEAD response: %d %s\n",
				dlna_src->head_response->ret_code, dlna_src->head_response->ret_msg);
		if (dlna_src->uri) {
			free(dlna_src->uri);
		}
		dlna_src->uri = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * Parse URI and extract info necessary to open socket to send
 * HEAD request
 *
 * @param dlna_src	this element
 *
 * @return	true if successfully parsed, false if problems encountered
 */
static gboolean
dlna_src_parse_uri(GstDlnaSrc *dlna_src)
{
	GST_INFO_OBJECT(dlna_src, "Parsing URI: %s", dlna_src->uri);

	// URI format is:
	// <scheme>:<hierarchical_part>[?query][#fragment]
	// where hierarchical part is:
	// [user_info@]<host_info>[:port][/path]
	// where host info can be ip address
	//
	// An example is:
	// http://192.168.0.111:8008/ocaphn/recording?rrid=1&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg
    gchar *p = NULL;
    gchar *addr = NULL;
    gchar *protocol = gst_uri_get_protocol(dlna_src->uri);

    if (NULL != protocol)
    {
        if (strcmp(protocol, "http") == 0)
        {
            if (NULL != (addr = gst_uri_get_location(dlna_src->uri)))
            {
                if (NULL != (p = strchr(addr, ':')))
                {
                    *p = 0; // so that the addr is null terminated where the address ends.
                    dlna_src->uri_port = atoi(++p);
                    GST_INFO_OBJECT(dlna_src, "Port retrieved: \"%d\".", dlna_src->uri_port);
                }
                // If address is changing, free old
                if (NULL != dlna_src->uri_addr && 0 != strcmp(dlna_src->uri_addr, addr))
                {
                    g_free(dlna_src->uri_addr);
                }
                if (NULL == dlna_src->uri_addr || 0 != strcmp(dlna_src->uri_addr, addr))
                {
                	dlna_src->uri_addr = g_strdup(addr);
                }
                GST_INFO_OBJECT(dlna_src, "New addr set: \"%s\".", dlna_src->uri_addr);
                g_free(addr);
                g_free(protocol);
            }
            else
            {
                GST_ERROR_OBJECT(dlna_src, "Location was null: \"%s\".", dlna_src->uri);
                g_free(protocol);
                return FALSE;
            }
        }
        else
        {
            GST_ERROR_OBJECT(dlna_src, "Protocol Info was NOT http: \"%s\".", protocol);
            return FALSE;
        }
    }
    else
    {
        GST_ERROR_OBJECT(dlna_src, "Protocol Info was null: \"%s\".", dlna_src->uri);
        return FALSE;
    }

    return TRUE;
}

/**
 * Create a socket for sending to HEAD request
 *
 * @param dlna_src	this element
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_src_open_socket(GstDlnaSrc *dlna_src)
{
	GST_LOG_OBJECT(dlna_src, "Opening socket to URI src");

    // Create socket
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if ((dlna_src->sock = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol)) == -1)
    {
        GST_ERROR_OBJECT(dlna_src, "Socket creation failed");
        return FALSE;
    }

    gint ret = 0;
    gchar portStr[8] = {0};
    snprintf(portStr, sizeof(portStr), "%d", dlna_src->uri_port);

    struct addrinfo* srvrInfo = NULL;
    if (0 != (ret = getaddrinfo(dlna_src->uri_addr, portStr, &hints, &srvrInfo)))
    {
        GST_ERROR_OBJECT(dlna_src, "getaddrinfo[%s]\n", gai_strerror(ret));
        return FALSE;
    }

    struct addrinfo* pSrvr = NULL;
    for(pSrvr = srvrInfo; pSrvr != NULL; pSrvr = pSrvr->ai_next)
    {
        if (0 > (dlna_src->sock = socket(pSrvr->ai_family,
                                    pSrvr->ai_socktype,
                                    pSrvr->ai_protocol)))
        {
            GST_ERROR_OBJECT(dlna_src, "socket() failed?");
            continue;
        }

        /*
        if (0 > setsockopt(dlna_src->sock, SOL_SOCKET, SO_REUSEADDR,
                           (char*) &yes, sizeof(yes)))
        {
            GST_ERROR_OBJECT(dlna_src, "setsockopt() failed?");
            return FALSE;
        }
		*/
        GST_LOG_OBJECT(dlna_src, "Got sock: %d\n", dlna_src->sock);

        if (connect(dlna_src->sock, pSrvr->ai_addr, pSrvr->ai_addrlen) != 0)
        {
        	GST_WARNING_OBJECT(dlna_src, "srcd() failed?");
            continue;
        }

        // Successfully connected
        GST_INFO_OBJECT(dlna_src, "Successful connect to sock: %d\n", dlna_src->sock);
        break;
    }

    if (NULL == pSrvr)
    {
        GST_ERROR_OBJECT(dlna_src, "failed to bind");
        freeaddrinfo(srvrInfo);
        return FALSE;
    }

    freeaddrinfo(srvrInfo);

    return TRUE;
}

/**
 * Close socket used to send HEAD request.
 *
 * @param	this element instance
 *
 * @return	false if problems encountered, true otherwise
 */
static gboolean
dlna_src_close_socket(GstDlnaSrc *dlna_src)
{
	GST_LOG_OBJECT(dlna_src, "Closing socket used for HEAD request");

	if (dlna_src->sock >= 0)
		CLOSESOCK(dlna_src->sock);

#ifdef RI_WIN32_SOCKETS
	WSACleanup();
#endif

	return TRUE;
}

/**
 * Creates the string which represents the HEAD request to send
 * to server to get info related to URI
 *
 * @param dlna_src	this element
 *			GST_WARNING_OBJECT(dlna_src,
				"No BYTES= found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_src_formulate_head_request(GstDlnaSrc *dlna_src)
{
	GST_LOG_OBJECT(dlna_src, "Formulating head request");

    gchar requestStr[MAX_HTTP_BUF_SIZE];
    gchar tmpStr[32];

    strcpy(requestStr, "HEAD ");

    strcat(requestStr, dlna_src->uri);

    strcat(requestStr, " HTTP/1.1");
    strcat(requestStr, CRLF);

    strcat(requestStr, "HOST: ");
    strcat(requestStr, dlna_src->uri_addr);
    strcat(requestStr, ":");

    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_src->uri_port);
    strcat(requestStr, tmpStr);
    strcat(requestStr, CRLF);

    // Include request to get content features
    strcat(requestStr, "getcontentFeatures.dlna.org : 1");
    strcat(requestStr, CRLF);

    // *TODO* - should we be including this and/or TimeSeekRange???
    // Include available seek range
    strcat(requestStr, "getAvailableSeekRange.dlna.org : 1");
    strcat(requestStr, CRLF);

    // Include time seek range if supported
    strcat(requestStr, "TimeSeekRange.dlna.org : npt=0-");
    strcat(requestStr, CRLF);

    // Add termination characters for overall request
    strcat(requestStr, CRLF);

    dlna_src->head_request_str = g_strdup(requestStr);
	GST_LOG_OBJECT(dlna_src, "HEAD Request: %s", dlna_src->head_request_str);

    return TRUE;
}

/**
 * Sends the HEAD request to server, reads response, parses and
 * stores info related to this URI.
 *
 * @param dlna_src	this element
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_src_issue_head_request(GstDlnaSrc *dlna_src)
{
	GST_LOG_OBJECT(dlna_src, "Issuing head request: %s", dlna_src->head_request_str);

	// Send HEAD request on socket
    gint bytesTxd = 0;
    gint bytesToTx = strlen(dlna_src->head_request_str);

    if ((bytesTxd = send(dlna_src->sock, dlna_src->head_request_str, bytesToTx, 0)) < -1)
    {
        GST_ERROR_OBJECT(dlna_src, "Problems sending on socket");
        return FALSE;
    }
    else if (bytesTxd == -1)
    {
        GST_ERROR_OBJECT(dlna_src, "Problems sending on socket, got back -1");
        return FALSE;
    }
    else if (bytesTxd != bytesToTx)
    {
        GST_ERROR_OBJECT(dlna_src, "Sent %d bytes instead of %d", bytesTxd, bytesToTx);
        return FALSE;
    }
	GST_INFO_OBJECT(dlna_src, "Issued head request: %s", dlna_src->head_request_str);

	// Read HEAD response
    gint bytesRcvd = 0;
    gchar responseStr[MAX_HTTP_BUF_SIZE];

    if ((bytesRcvd = recv(dlna_src->sock, responseStr, MAX_HTTP_BUF_SIZE, 0)) <= 0)
    {
        GST_ERROR_OBJECT(dlna_src, "HEAD Response recv() failed");
        return FALSE;
    }
    else
    {
    	// Null terminate response string
    	responseStr[bytesRcvd] = '\0';
    }
    dlna_src->head_response_str = g_strdup(responseStr);
	GST_INFO_OBJECT(dlna_src, "HEAD Response received: %s", dlna_src->head_response_str);

	return TRUE;
}

/**
 * Parse HEAD response into specific values related to URI content item.
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_parse(GstDlnaSrc *dlna_src)
{
	GST_LOG_OBJECT(dlna_src, "Parsing HEAD Response: %s", dlna_src->head_response_str);

	// Initialize structure to hold parsed HEAD Response
	if (!dlna_src_head_response_init_struct(dlna_src))
	{
        GST_ERROR_OBJECT(dlna_src, "Problems initializing struct to store HEAD response");
        return FALSE;
	}

	// Convert all header field strings to upper case to aid in parsing
	int i = 0;
	for (i = 0; dlna_src->head_response_str[i]; i++)
	{
		dlna_src->head_response_str[i] = toupper(dlna_src->head_response_str[i]);
	}

	// Initialize array of strings used to store field values
	char* fields[HEAD_RESPONSE_HDRS_CNT];
	for (i = 0; i < HEAD_RESPONSE_HDRS_CNT; i++)
	{
		fields[i] = NULL;
	}

	// Tokenize HEAD response into individual field values using CRLF as delim
	char* tokens = strtok(dlna_src->head_response_str, CRLF);
	while (tokens != NULL)
	{
		// Look for field header contained in this string
		gint idx = dlna_src_head_response_get_field_idx(dlna_src, tokens);

		// If found field header, extract value
		if (idx != -1)
		{
			fields[idx] = tokens;
		}
		else
		{
			GST_WARNING_OBJECT(dlna_src, "No Idx found for Field:%s", tokens);
		}

		// Go on to next field
		tokens = strtok(NULL, CRLF);
	}

	// Parse value from each field header string
	for (i = 0; i < HEAD_RESPONSE_HDRS_CNT; i++)
	{
		if (fields[i] != NULL)
		{
			dlna_src_head_response_assign_field_value(dlna_src, i, fields[i]);
		}
	}

	// Print out results of HEAD request
	if (!dlna_src_head_response_struct_to_str(dlna_src))
	{
        GST_ERROR_OBJECT(dlna_src, "Problems converting HEAD response struct to string");
        return FALSE;
	}
	else
	{
        GST_INFO_OBJECT(dlna_src, "Parsed HEAD Response into struct: %s",
        		dlna_src->head_response->struct_str);
	}
	return TRUE;
}

/**
 * Initialize structure to store HEAD Response
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_init_struct(GstDlnaSrc *dlna_src)
{
	// Allocate storage
	dlna_src->head_response = g_try_malloc0(sizeof(GstDlnaSrcHeadResponse));
	dlna_src->head_response->content_features = g_try_malloc0(sizeof(GstDlnaSrcHeadResponseContentFeatures));

	// Initialize structs
	// {"HTTP", STRING_TYPE}
	dlna_src->head_response->http_rev_idx = HDR_IDX_HTTP;
	dlna_src->head_response->http_rev = NULL;
	dlna_src->head_response->ret_code = 0;
	dlna_src->head_response->ret_msg = NULL;

	// {"TIMESEEKRANGE.DLNA.ORG", STRING_TYPE},
	dlna_src->head_response->time_seek_idx = HDR_IDX_TIMESEEKRANGE;

	// {"NPT", NPT_RANGE_TYPE},
	dlna_src->head_response->npt_seek_idx = HDR_IDX_NPT;
	dlna_src->head_response->time_seek_npt_start = NULL;
	dlna_src->head_response->time_seek_npt_end = NULL;
	dlna_src->head_response->time_seek_npt_duration = NULL;

	// {"BYTES", BYTE_RANGE_TYPE},
	dlna_src->head_response->byte_seek_idx = HDR_IDX_BYTES;
	dlna_src->head_response->byte_seek_start = 0;
	dlna_src->head_response->byte_seek_end = 0;
	dlna_src->head_response->byte_seek_total = 0;

	// {CONTENT RANGE DTCP, BYTE_RANGE_TYPE},
	dlna_src->head_response->dtcp_range_idx = HDR_IDX_DTCP_RANGE;
	dlna_src->head_response->dtcp_range_start = 0;
	dlna_src->head_response->dtcp_range_end = 0;
	dlna_src->head_response->dtcp_range_total = 0;

	// {"TRANSFERMODE.DLNA.ORG", STRING_TYPE}
	dlna_src->head_response->transfer_mode_idx = HDR_IDX_TRANSFERMODE;
	dlna_src->head_response->transfer_mode = NULL;

	// {"TRANSFER-ENCODING", STRING_TYPE}
	dlna_src->head_response->transfer_encoding_idx = HDR_IDX_TRANSFER_ENCODING;

	dlna_src->head_response->transfer_encoding = NULL;

	// {"DATE", STRING_TYPE}
	dlna_src->head_response->date_idx = HDR_IDX_DATE;
	dlna_src->head_response->date = NULL;

	// {"SERVER", STRING_TYPE}
	dlna_src->head_response->server_idx = HDR_IDX_SERVER;
	dlna_src->head_response->server = NULL;

	// {"CONTENT-TYPE", STRING_TYPE}
	dlna_src->head_response->content_type_idx = HDR_IDX_CONTENT_TYPE;
	dlna_src->head_response->content_type = NULL;

	// Addition subfields in CONTENT TYPE if dtcp encrypted
	dlna_src->head_response->dtcp_host_idx = HDR_IDX_DTCP_HOST;
	dlna_src->head_response->dtcp_host = NULL;
	dlna_src->head_response->dtcp_port_idx = HDR_IDX_DTCP_PORT;
	dlna_src->head_response->dtcp_port = -1;
	dlna_src->head_response->content_format_idx = HDR_IDX_CONTENT_FORMAT;

	// {"CONTENTFEATURES.DLNA.ORG", STRING_TYPE},
	dlna_src->head_response->content_features_idx = HDR_IDX_CONTENTFEATURES;

	// {"DLNA.ORG_PN", STRING_TYPE}
	dlna_src->head_response->content_features->profile_idx = HDR_IDX_PN;
    dlna_src->head_response->content_features->profile = NULL;

	// {"DLNA.ORG_OP", FLAG_TYPE}
    dlna_src->head_response->content_features->operations_idx = HDR_IDX_OP;
	dlna_src->head_response->content_features->op_time_seek_supported = FALSE;
	dlna_src->head_response->content_features->op_range_supported = FALSE;

	// {"DLNA.ORG_PS", NUMERIC_TYPE}, // 13
	dlna_src->head_response->content_features->playspeeds_idx = HDR_IDX_PS;
    dlna_src->head_response->content_features->playspeeds_cnt = 0;

	// {"DLNA.ORG_FLAGS", FLAG_TYPE} // 14
	dlna_src->head_response->content_features->flags_idx = HDR_IDX_FLAGS;
	dlna_src->head_response->content_features->flag_sender_paced_set = FALSE;
	dlna_src->head_response->content_features->flag_limited_time_seek_set = FALSE;
	dlna_src->head_response->content_features->flag_limited_byte_seek_set = FALSE;
	dlna_src->head_response->content_features->flag_play_container_set = FALSE;
	dlna_src->head_response->content_features->flag_so_increasing_set = FALSE;
	dlna_src->head_response->content_features->flag_sn_increasing_set = FALSE;
	dlna_src->head_response->content_features->flag_rtsp_pause_set = FALSE;
	dlna_src->head_response->content_features->flag_streaming_mode_set = FALSE;
	dlna_src->head_response->content_features->flag_interactive_mode_set = FALSE;
	dlna_src->head_response->content_features->flag_background_mode_set = FALSE;
	dlna_src->head_response->content_features->flag_stalling_set = FALSE;
	dlna_src->head_response->content_features->flag_dlna_v15_set = FALSE;
	dlna_src->head_response->content_features->flag_link_protected_set = FALSE;
	dlna_src->head_response->content_features->flag_full_clear_text_set = FALSE;
	dlna_src->head_response->content_features->flag_limited_clear_text_set = FALSE;

	return TRUE;
}

/**
 * Looks for a matching HEAD response field in supplied string.
 *
 * @param	look for HEAD response field in this string
 *
 * @return	index of matching HEAD response field,
 * 			-1 if does not contain a HEAD response field header
 */
static gint
dlna_src_head_response_get_field_idx(GstDlnaSrc *dlna_src, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Determine associated HEAD response field");

	gint idx = -1;
	int i = 0;
	for (i = 0; i < HEAD_RESPONSE_HDRS_CNT; i++)
	{
		if (strstr(field_str, HEAD_RESPONSE_HDRS[i]) != NULL)
		{
			idx = i;
			break;
		}
	}

	return idx;
}

/**
 * Initialize associated value in HEAD response struct
 *
 * @param	dlna_src	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_assign_field_value(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Store value received in HEAD response field for field %d - %s",
			idx, HEAD_RESPONSE_HDRS[idx]);

	gboolean rc = TRUE;
	// *TODO* - figure out max size
	char tmp1[32];
	char tmp2[32];
	gint int_value = 0;
	gint ret_code = 0;

	// Get value based on index
	switch (idx)
	{
	case HDR_IDX_TRANSFERMODE:
		dlna_src->head_response->transfer_mode = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_DATE:
		dlna_src->head_response->date = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_CONTENT_TYPE:
		if (!dlna_src_head_response_parse_content_type(dlna_src, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_SERVER:
		dlna_src->head_response->server = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_TRANSFER_ENCODING:
		dlna_src->head_response->transfer_encoding = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_HTTP:
		// *TODO* - verify this is correct based on allowable white spaces
		strcat(field_str, "\n");
		if ((ret_code = sscanf(field_str, "%s %d %[^\n]", tmp1, &int_value, tmp2)) != 3)
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems with HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, tmp: %s, %s",
					HEAD_RESPONSE_HDRS[idx], idx, field_str, ret_code, tmp1, tmp2);
		}
		else
		{
			dlna_src->head_response->http_rev = g_strdup(tmp1);
			dlna_src->head_response->ret_code = int_value;
			dlna_src->head_response->ret_msg = g_strdup(tmp2);
		}
		break;

	case HDR_IDX_TIMESEEKRANGE:
		if (!dlna_src_head_response_parse_time_seek(dlna_src, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_CONTENTFEATURES:
		if (!dlna_src_head_response_parse_content_features(dlna_src, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_DTCP_RANGE:
		if (!dlna_src_head_response_parse_dtcp_range(dlna_src, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_VARY:
	case HDR_IDX_PRAGMA:
	case HDR_IDX_CACHE_CONTROL:
		// Ignore field values
		break;

	default:
		GST_WARNING_OBJECT(dlna_src, "Unsupported HEAD response field idx %d: %s", idx, field_str);
	}

	return rc;
}

/**
 * TimeSeekRange header formatting as specified in DLNA 7.4.40.5:
 *
 * TimeSeekRange.dlna.org : npt=335.1-336.1/40445.4 bytes=1539686400-1540210688/304857907200
  *
 * The time seek range header can have two different formats
 * Either:
 * 	"npt = 1*DIGIT["."1*3DIGIT]
 *		ntp sec = 0.232, or 1 or 15 or 16.652 (leading at one or more digits,
 *		optionally followed by decimal point and 3 digits)
 * OR
 * 	"npt=00:00:00.000" where format is HH:MM:SS.mmm (hours, minutes, seconds, milliseconds)
 *
 * @param	dlna_src	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE
 */
static gboolean
dlna_src_head_response_parse_time_seek(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	char tmp1[32];
	char tmp2[32];
	char tmp3[32];
	char tmp4[132];
	char* tmp_str1 = NULL;
	char* tmp_str2 = NULL;
	gint ret_code = 0;
	guint64 ullong1 = 0;
	guint64 ullong2 = 0;
	guint64 ullong3 = 0;

	// *TODO* - need more sophisticated parsing of NPT to handle different formats
	// Extract start and end NPT
	tmp_str2 = strstr(field_str, TIME_SEEK_HDRS[HDR_IDX_NPT]);
	tmp_str1 = strstr(tmp_str2, "=");
	if (tmp_str1 != NULL)
	{
		tmp_str1++;
		// *TODO* - add logic to deal with '*'
		if ((ret_code = sscanf(tmp_str1, "%[^-]-%[^/]/%s %s", tmp1, tmp2, tmp3, tmp4)) != 4)
		{
			GST_WARNING_OBJECT(dlna_src,
				"Problems parsing NPT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s, %s",
				HEAD_RESPONSE_HDRS[idx], tmp_str1, ret_code, tmp1, tmp2, tmp3);
		}
		else
		{
			dlna_src->head_response->time_seek_npt_start = g_strdup(tmp1);
			dlna_src->head_response->time_seek_npt_end = g_strdup(tmp2);
			dlna_src->head_response->time_seek_npt_duration = g_strdup(tmp3);
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_src,
			"No NPT found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
			HEAD_RESPONSE_HDRS[idx], idx, field_str);
	}

	// Extract start and end BYTES
	tmp_str2 = strstr(field_str, TIME_SEEK_HDRS[HDR_IDX_BYTES]);
	if (tmp_str2 != NULL)
	{
		tmp_str1 = strstr(tmp_str2, "=");
		if (tmp_str1 != NULL)
		{
			tmp_str1++;
			// *TODO* - add logic to deal with '*'
			if ((ret_code = sscanf(tmp_str1, "%llu-%llu/%llu",
					&ullong1, &ullong2, &ullong3)) != 3)
			{
				GST_WARNING_OBJECT(dlna_src,
					"Problems parsing BYTES from HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, ullong: %llu, %llu, %llu",
					HEAD_RESPONSE_HDRS[idx], idx, tmp_str1, ret_code, ullong1, ullong2, ullong3);
			}
			else
			{
				dlna_src->head_response->byte_seek_start = ullong1;
				dlna_src->head_response->byte_seek_end = ullong2;
				dlna_src->head_response->byte_seek_total = ullong3;
			}
		}
		else
		{
			GST_WARNING_OBJECT(dlna_src,
				"No BYTES= found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_src,
			"No BYTES= found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
			HEAD_RESPONSE_HDRS[idx], idx, field_str);
	}
	return TRUE;
}

/**
 * DTCP Range header formatting:
 *
 * Content-Range.dtcp.com : bytes=1539686400-1540210688/304857907200
 *
 * @param	dlna_src	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE
 */
static gboolean
dlna_src_head_response_parse_dtcp_range(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	char* tmp_str1 = NULL;
	char* tmp_str2 = NULL;
	gint ret_code = 0;
	guint64 ullong1 = 0;
	guint64 ullong2 = 0;
	guint64 ullong3 = 0;

	// Extract start and end BYTES same format as TIME SEEK BYTES header
	tmp_str2 = strstr(field_str, TIME_SEEK_HDRS[HDR_IDX_BYTES]);
	if (tmp_str2 != NULL)
	{
		tmp_str1 = tmp_str2 + strlen(TIME_SEEK_HDRS[HDR_IDX_BYTES] + 1) + 1;
		// *TODO* - add logic to deal with '*'
		if ((ret_code = sscanf(tmp_str1, "%llu-%llu/%llu",
				&ullong1, &ullong2, &ullong3)) != 3)
		{
			GST_WARNING_OBJECT(dlna_src,
					"Problems parsing BYTES from HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, ullong: %llu, %llu, %llu",
					HEAD_RESPONSE_HDRS[idx], idx, tmp_str1, ret_code, ullong1, ullong2, ullong3);
		}
		else
		{
			dlna_src->head_response->dtcp_range_start = ullong1;
			dlna_src->head_response->dtcp_range_end = ullong2;
			dlna_src->head_response->dtcp_range_total = ullong3;
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_src,
				"No BYTES= found in dtcp range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
	}
	return TRUE;
}

/**
 * Extract values from content features header in HEAD Response
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	content feature header field extracted from HEAD response
 *
 * @return	TRUE
 */
static gboolean
dlna_src_head_response_parse_content_features(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Called with field str: %s", field_str);

	// Split CONTENTFEATURES.DLNA.ORG into following sub-fields using ";" as deliminator
		//"DLNA.ORG_PN"
		//"DLNA.ORG_OP"
		//"DLNA.ORG_PS"
		//"DLNA.ORG_FLAGS"
	gchar* pn_str = NULL;
	gchar* op_str = NULL;
	gchar* ps_str = NULL;
	gchar* flags_str = NULL;

	gchar* tmp_str2 = strstr(field_str, HEAD_RESPONSE_HDRS[idx]);
	gchar* tmp_str1 = strstr(tmp_str2, ":");
	if (tmp_str1 != NULL)
	{
		// Increment ptr to get pass ":"
		tmp_str1++;

		// Split into parts using ";" as delmin
		char* tokens = strtok(tmp_str1, ";");
		while (tokens != NULL)
		{
			// "DLNA.ORG_PN"
			if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_PN])) != NULL)
			{
				GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_PN]);
				pn_str = tokens;
			}
			// "DLNA.ORG_OP"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_OP])) != NULL)
			{
				GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_OP]);
				op_str = tokens;
			}
			// "DLNA.ORG_PS"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_PS])) != NULL)
			{
				GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_PS]);
				ps_str = tokens;
			}
			// "DLNA.ORG_FLAGS"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_FLAGS])) != NULL)
			{
				GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_FLAGS]);
				flags_str = tokens;
			}
			else
			{
				GST_WARNING_OBJECT(dlna_src, "Unrecognized sub field:%s", tokens);
			}

			// Go on to next field
			tokens = strtok(NULL, ";");
		}
	}

	if (pn_str != NULL)
	{
		if (!dlna_src_head_response_parse_profile(dlna_src, idx, pn_str))
		{
			GST_WARNING_OBJECT(dlna_src, "Problems parsing profile sub field: %s", pn_str);
		}
	}
	if (op_str != NULL)
	{
		if (!dlna_src_head_response_parse_operations(dlna_src, idx, op_str))
		{
			GST_WARNING_OBJECT(dlna_src, "Problems parsing operations sub field: %s", op_str);
		}
	}
	if (ps_str != NULL)
	{
		if (!dlna_src_head_response_parse_playspeeds(dlna_src, idx, ps_str))
		{
			GST_WARNING_OBJECT(dlna_src, "Problems parsing playspeeds sub field: %s", ps_str);
		}
	}
	if (flags_str != NULL)
	{
		if (!dlna_src_head_response_parse_flags(dlna_src, idx, flags_str))
		{
			GST_WARNING_OBJECT(dlna_src, "Problems parsing flags sub field: %s", flags_str);
		}
	}
	return TRUE;
}

/**
 * Parse DLNA profile identified by DLNA.ORG_PN header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean dlna_src_head_response_parse_profile(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Found PN Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_src,
		"Problems parsing DLNA.ORG_PN from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		dlna_src->head_response->content_features->profile = g_strdup(tmp2);
	}
	return TRUE;
}

/**
 * Parse DLNA supported operations sub field identified by DLNA.ORG_OP header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_OP field
 *
 * @return	TRUE
 */
static gboolean dlna_src_head_response_parse_operations(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Found OP Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_src,
		"Problems parsing DLNA.ORG_OP from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		GST_LOG_OBJECT(dlna_src, "OP Field value: %s", tmp2);

		// Verify length is as expected = 2
		if (strlen(tmp2) != 2)
		{
			GST_WARNING_OBJECT(dlna_src,
				"DLNA.ORG_OP from HEAD response sub field value: %s, is not at expected len of 2",
				field_str, tmp2);
		}
		else
		{
			// First char represents time seek support
			if ((tmp2[0] == '0') || (tmp2[0] == '1'))
			{
				if (tmp2[0] == '0')
				{
					dlna_src->head_response->content_features->op_time_seek_supported = FALSE;
				}
				else
				{
					dlna_src->head_response->content_features->op_time_seek_supported = TRUE;
				}
			}
			else
			{
				GST_WARNING_OBJECT(dlna_src,
					"DLNA.ORG_OP Time Seek Flag from HEAD response sub field value: %s, is not 0 or 1",
					field_str, tmp2);
			}

			// Second char represents range support
			if ((tmp2[1] == '0') || (tmp2[1] == '1'))
			{
				if (tmp2[1] == '0')
				{
					dlna_src->head_response->content_features->op_range_supported = FALSE;
				}
				else
				{
					dlna_src->head_response->content_features->op_range_supported = TRUE;
				}
			}
			else
			{
				GST_WARNING_OBJECT(dlna_src,
					"DLNA.ORG_OP Range Flag from HEAD response sub field value: %s, is not 0 or 1",
					field_str, tmp2);
			}
		}
	}
	return TRUE;
}

/**
 * Parse DLNA playspeeds sub field identified by DLNA.ORG_PS header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PS field
 *
 * @return	TRUE
 */
static gboolean dlna_src_head_response_parse_playspeeds(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Found PS Field: %s", field_str);
	gint ret_code = 0;
	char* save_ptr;

	gchar tmp1[256];
	gchar tmp2[256];
	gdouble rate = 0;

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_src,
		"Problems parsing DLNA.ORG_PS from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
		return FALSE;
	}
	else
	{
		GST_LOG_OBJECT(dlna_src, "PS Field value: %s", tmp2);

		// Tokenize list of comma separated playspeeds
		char* playspeeds = strtok_r(tmp2, ",", &save_ptr);
		while ((playspeeds != NULL) &&
				(dlna_src->head_response->content_features->playspeeds_cnt < PLAYSPEEDS_MAX_CNT))
		{
			GST_LOG_OBJECT(dlna_src, "Found PS: %s", playspeeds);

			// Store string representation
			dlna_src->head_response->content_features->playspeed_strs[
			     dlna_src->head_response->content_features->playspeeds_cnt] = g_strdup(playspeeds);

			// Check if this is a non-fractional value
			if (strstr(playspeeds,"/") == NULL)
			{
				// Convert str to numeric value
				if ((ret_code = sscanf(playspeeds, "%lf", &rate)) != 1)
				{
					GST_WARNING_OBJECT(dlna_src,
					"Problems converting playspeed %s into numeric value", playspeeds);
					return FALSE;
				}
				else
				{
					dlna_src->head_response->content_features->playspeeds[
					    dlna_src->head_response->content_features->playspeeds_cnt] = rate;
				}
			}
			else
			{
				// *TODO* - need logic to handle fractional values
			}
			dlna_src->head_response->content_features->playspeeds_cnt++;

			// Go on to next field
			playspeeds = strtok_r(NULL, ",", &save_ptr);
		}
	}

	return TRUE;
}

/**
 * Parse DLNA flags sub field identified by DLNA.ORG_FLAGS header.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_FLAGS field
 *
 * @return	TRUE
 */
static gboolean dlna_src_head_response_parse_flags(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Found Flags Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_src,
		"Problems parsing DLNA.ORG_FLAGS from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		GST_LOG_OBJECT(dlna_src, "FLAGS Field value: %s", tmp2);

		// Get value of each of the defined flags
		dlna_src->head_response->content_features->flag_sender_paced_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, SP_FLAG);
		dlna_src->head_response->content_features->flag_limited_time_seek_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, LOP_NPT);
		dlna_src->head_response->content_features->flag_limited_byte_seek_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, LOP_BYTES);
		dlna_src->head_response->content_features->flag_play_container_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, PLAYCONTAINER_PARAM);
		dlna_src->head_response->content_features->flag_so_increasing_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, S0_INCREASING);
		dlna_src->head_response->content_features->flag_sn_increasing_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, SN_INCREASING);
		dlna_src->head_response->content_features->flag_rtsp_pause_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, RTSP_PAUSE);
		dlna_src->head_response->content_features->flag_streaming_mode_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, TM_S);
		dlna_src->head_response->content_features->flag_interactive_mode_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, TM_I);
		dlna_src->head_response->content_features->flag_background_mode_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, TM_B);
		dlna_src->head_response->content_features->flag_stalling_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, HTTP_STALLING);
		dlna_src->head_response->content_features->flag_dlna_v15_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, DLNA_V15_FLAG);
		dlna_src->head_response->content_features->flag_link_protected_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, LP_FLAG);
		dlna_src->head_response->content_features->flag_full_clear_text_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, CLEARTEXTBYTESEEK_FULL_FLAG);
		dlna_src->head_response->content_features->flag_limited_clear_text_set =
				dlna_src_head_response_is_flag_set(dlna_src, tmp2, LOP_CLEARTEXTBYTES);
	}

	return TRUE;
}

/**
 * Parse content type identified by CONTENT-TYPE header.  Includes additional
 * subfields when content is DTCP encrypted.
 *
 * @param	dlna_src	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean dlna_src_head_response_parse_content_type(GstDlnaSrc *dlna_src, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_src, "Found Content Type Field: %s", field_str);
	gint ret_code = 0;
	gchar tmp1[32];
	gchar tmp2[32];
	gchar tmp3[32];

	// If not DTCP content, this field is mime-type
	if (strstr(field_str, "DTCP") == NULL)
	{
		dlna_src->head_response->content_type = g_strdup((strstr(field_str, ":")+1));
	}
	else
	{
		// DTCP related info in subfields
		// Split CONTENT-TYPE into following sub-fields using ";" as deliminator
			//
			// DTCP1HOST
			// DTCP1PORT
			// CONTENTFORMAT
		gchar* tmp_str2 = strstr(field_str, HEAD_RESPONSE_HDRS[idx]);
		gchar* tmp_str1 = strstr(tmp_str2, ":");
		if (tmp_str1 != NULL)
		{
			// Increment ptr to get pass ":"
			tmp_str1++;

			// Split into parts using ";" as delmin
			char* tokens = strtok(tmp_str1, ";");
			while (tokens != NULL)
			{
				// DTCP1HOST
				if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_DTCP_HOST])) != NULL)
				{
					GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_HOST]);
					dlna_src->head_response->dtcp_host = g_strdup((strstr(tmp_str2, "=")+1));
				}
				// DTCP1PORT
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT])) != NULL)
				{
					if ((ret_code = sscanf(tmp_str2, "%[^=]=%d", tmp1,
										   &dlna_src->head_response->dtcp_port)) != 2)
					{
						GST_WARNING_OBJECT(dlna_src,
						"Problems parsing DTCP PORT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s",
								HEAD_RESPONSE_HDRS[idx], tmp_str2, ret_code, tmp1);
					}
					else
					{
						GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT]);
					}
				}
				// CONTENTFORMAT
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_CONTENT_FORMAT])) != NULL)
				{
					if ((ret_code = sscanf(tmp_str2, "%[^=]=\"%[^\"]%s", tmp1, tmp2, tmp3)) != 3)
					{
						GST_WARNING_OBJECT(dlna_src,
						"Problems parsing DTCP CONTENT FORMAT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s, %s",
								HEAD_RESPONSE_HDRS[idx], tmp_str2, ret_code, tmp1, tmp2, tmp3);
					}
					else
					{
						GST_LOG_OBJECT(dlna_src, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT]);
						dlna_src->head_response->content_type = g_strdup(tmp2);
					}
				}
				//  APPLICATION/X-DTCP1
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_APP_DTCP])) != NULL)
				{
					// Ignore this field
				}
				else
				{
					GST_WARNING_OBJECT(dlna_src, "Unrecognized sub field:%s", tokens);
				}

				// Go on to next field
				tokens = strtok(NULL, ";");
			}
		}
	}
	return TRUE;
}

/**
 * Utility method which determines if a given flag is set in the flags string.
 *
 * @param flagsStr the fourth field of a protocolInfo string
 *
 * @return TRUE if flag is set, FALSE otherwise
 */
static gboolean dlna_src_head_response_is_flag_set(GstDlnaSrc *dlna_src, gchar* flags_str, gint flag)
{
	if ((flags_str == NULL) || (strlen(flags_str) <= RESERVED_FLAGS_LENGTH))
	{
		GST_WARNING_OBJECT(dlna_src, "FLAGS Field value null or too short : %s", flags_str);
		return FALSE;
	}

	// Drop reserved flags off of value (prepended zeros will be ignored)
	gchar* tmp_str = g_strdup(flags_str);
	gint len = strlen(tmp_str);
	tmp_str[len - RESERVED_FLAGS_LENGTH] = '\0';

	// Convert into long using hexidecimal format
	gint64 value = strtol(tmp_str, NULL, 16);

	g_free(tmp_str);

	return (value & flag) == flag;
}

/**
 * Format HEAD response structure into string representation.
 *
 * @param	dlna_src	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_src_head_response_struct_to_str(GstDlnaSrc *dlna_src)
{
	GST_INFO_OBJECT(dlna_src, "Formatting HEAD Response struct");

    gchar structStr[2048];
    gchar tmpStr[32];

    strcpy(structStr, "\nHTTP Version: ");
    if (dlna_src->head_response->http_rev != NULL)
    	strcat(structStr, dlna_src->head_response->http_rev);
    strcat(structStr, "\n");

    strcat(structStr, "HEAD Ret Code: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_src->head_response->ret_code);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "HEAD Ret Msg: ");
    if (dlna_src->head_response->ret_msg != NULL)
    	strcat(structStr, dlna_src->head_response->ret_msg);
    strcat(structStr, "\n");

    strcat(structStr, "Server: ");
    if (dlna_src->head_response->server != NULL)
    	strcat(structStr, dlna_src->head_response->server);
    strcat(structStr, "\n");

    strcat(structStr, "Date: ");
    if (dlna_src->head_response->date != NULL)
    	strcat(structStr, dlna_src->head_response->date);
    strcat(structStr, "\n");

    strcat(structStr, "Content Type: ");
    if (dlna_src->head_response->content_type != NULL)
    	strcat(structStr, dlna_src->head_response->content_type);
    strcat(structStr, "\n");

    if (dlna_src->head_response->dtcp_host != NULL)
    {
        strcat(structStr, "DTCP Host: ");
        strcat(structStr, dlna_src->head_response->dtcp_host);
        strcat(structStr, "\n");

        strcat(structStr, "DTCP Port: ");
        (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
        sprintf(tmpStr, "%d", dlna_src->head_response->dtcp_port);
        strcat(structStr, tmpStr);
        strcat(structStr, "\n");
    }

    strcat(structStr, "HTTP Transfer Encoding: ");
    if (dlna_src->head_response->transfer_encoding != NULL)
    	strcat(structStr, dlna_src->head_response->transfer_encoding);
    strcat(structStr, "\n");

	strcat(structStr, "DLNA Transfer Mode: ");
    if (dlna_src->head_response->transfer_mode != NULL)
    	strcat(structStr, dlna_src->head_response->transfer_mode);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT Start: ");
    if (dlna_src->head_response->time_seek_npt_start != NULL)
    	strcat(structStr, dlna_src->head_response->time_seek_npt_start);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT End: ");
    if (dlna_src->head_response->time_seek_npt_end != NULL)
    	strcat(structStr, dlna_src->head_response->time_seek_npt_end);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT Duration: ");
    if (dlna_src->head_response->time_seek_npt_duration != NULL)
    	strcat(structStr, dlna_src->head_response->time_seek_npt_duration);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek Start: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_src->head_response->byte_seek_start);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek End: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_src->head_response->byte_seek_end);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek Total: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_src->head_response->byte_seek_total);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    if (dlna_src->head_response->dtcp_range_total != 0)
    {
    	strcat(structStr, "DTCP Range Start: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_src->head_response->dtcp_range_start);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");

    	strcat(structStr, "DTCP Range End: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_src->head_response->dtcp_range_end);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");

    	strcat(structStr, "DTCP Range Total: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_src->head_response->dtcp_range_total);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");
    }

    strcat(structStr, "DLNA Profile: ");
    if (dlna_src->head_response->content_features->profile != NULL)
    	strcat(structStr, dlna_src->head_response->content_features->profile);
    strcat(structStr, "\n");

    strcat(structStr, "Supported Playspeed Cnt: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_src->head_response->content_features->playspeeds_cnt);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Playspeeds: ");
    gint i = 0;
    for (i = 0; i < dlna_src->head_response->content_features->playspeeds_cnt; i++)
    {
        strcat(structStr, dlna_src->head_response->content_features->playspeed_strs[i]);
        strcat(structStr, ", ");
    }
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->op_time_seek_supported) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Range Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->op_range_supported) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Sender Paced?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_sender_paced_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Time Seek?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_limited_time_seek_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Byte Seek?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_limited_byte_seek_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Play Container?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_play_container_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "S0 Increasing?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_so_increasing_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Sn Increasing?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_sn_increasing_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "RTSP Pause?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_rtsp_pause_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Streaming Mode Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_streaming_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Interactive Mode Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_interactive_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Background Mode Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_background_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Connection Stalling Supported?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_stalling_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "DLNA Ver. 1.5?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_dlna_v15_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Link Protected?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_link_protected_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Full Clear Text?: "); *
    strcat(structStr, (dlna_src->head_response->content_features->flag_full_clear_text_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Clear Text?: ");
    strcat(structStr, (dlna_src->head_response->content_features->flag_limited_clear_text_set) ? "TRUE\n" : "FALSE\n");

    // Copy local string to struct str
    dlna_src->head_response->struct_str = g_strdup(structStr);

	return TRUE;
}

/**
 * Setup dtcp decoder element and add to src in order to handle DTCP encrypted
 * content
 *
 * @param dlna_src	this element
 */
static gboolean
dlna_src_dtcp_setup(GstDlnaSrc *dlna_src)
{
	GST_INFO_OBJECT(dlna_src, "Setup for dtcp content");

	// Create non-encrypt sink element
	GST_INFO_OBJECT(dlna_src, "Creating dtcp decrypter");
	dlna_src->dtcp_decrypter = gst_element_factory_make ("dtcpip",
														ELEMENT_NAME_DTCP_DECRYPTER);
	if (!dlna_src->dtcp_decrypter) {
		GST_ERROR_OBJECT(dlna_src, "The dtcp decrypter element could not be created. Exiting.\n");
		return FALSE;
	}

	// Set DTCP host property
	g_object_set(G_OBJECT(dlna_src->dtcp_decrypter), "dtcp1host",
			dlna_src->head_response->dtcp_host, NULL);

	// Set DTCP port property
	g_object_set(G_OBJECT(dlna_src->dtcp_decrypter), "dtcp1port",
			dlna_src->head_response->dtcp_port, NULL);

	// Set DTCP key storage property
	g_object_set(G_OBJECT(dlna_src->dtcp_decrypter), "dtcpip_storage",
			dlna_src->dtcp_key_storage, NULL);

	// Add this element to the src
	gst_bin_add(GST_BIN(&dlna_src->bin), dlna_src->dtcp_decrypter);

	// Link elements together
	if (!gst_element_link_many(dlna_src->http_src, dlna_src->dtcp_decrypter, NULL))
	{
		GST_ERROR_OBJECT(dlna_src, "Problems linking elements in src. Exiting.\n");
		return FALSE;
	}
	return TRUE;
}

/* 
 * The following section supports the GStreamer auto plugging infrastructure. 
 * Set to 0 if this is done on a package level using (ie gstelements.[hc])
 */
#if 1

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dlna_src_init (GstPlugin * dlna_src)
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template ' with your description
	 */
	GST_DEBUG_CATEGORY_INIT (gst_dlna_src_debug, "dlnasrc", 0, "MPEG+DLNA Player");

    // *TODO* - setting rank + 1 forces this element to get selected as src by playsrc2
	return gst_element_register ((GstPlugin *)dlna_src, "dlnasrc",
	GST_RANK_PRIMARY+1, GST_TYPE_DLNA_SRC);
	//		GST_RANK_PRIMARY, GST_TYPE_DLNA_SRC);
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dlnasrc"
#endif

/* gstreamer looks for this structure to register eisss
 *
 * exchange the string 'Template eiss' with your eiss description
 */
GST_PLUGIN_DEFINE (
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"dlnasrc",
		"MPEG+DLNA Decoder",
		(GstPluginInitFunc)dlna_src_init,
		VERSION,
		"LGPL",
		"gst-cablelabs_ri",
		"http://gstreamer.net/");

#endif

/*
  Function for marshaling the callback arguments into a function closure.

  Taken from the decodesrc code, so we can replicate the interface.
 */

#define g_marshal_value_peek_boolean(v) (v)->data[0].v_int
#define g_marshal_value_peek_object(v) (v)->data[0].v_pointer
#define g_marshal_value_peek_boxed(v) g_value_get_boxed(v)

void
gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure *closure,
		GValue *return_value G_GNUC_UNUSED,
		guint n_param_values,
		const GValue *param_values,
		gpointer invocation_hint G_GNUC_UNUSED,
		gpointer marshal_data)
{
	typedef void (*GMarshalFunc_VOID__OBJECT_BOOLEAN) (gpointer data1,
			gpointer arg_1,
			gboolean arg_2,
			gpointer data2);
	register GMarshalFunc_VOID__OBJECT_BOOLEAN callback;
	register GCClosure *cc = (GCClosure*) closure;
	register gpointer data1, data2;

	g_return_if_fail (n_param_values == 3);

	if (G_CCLOSURE_SWAP_DATA (closure)) {
		data1 = closure->data;
		data2 = g_value_peek_pointer (param_values + 0);
	} else {
		data1 = g_value_peek_pointer (param_values + 0);
		data2 = closure->data;
	}
	callback = (GMarshalFunc_VOID__OBJECT_BOOLEAN)
    		(marshal_data ? marshal_data : cc->callback);

	callback (data1,
			g_marshal_value_peek_object (param_values + 1),
			g_marshal_value_peek_boolean (param_values + 2),
			data2);
}

void
gst_play_marshal_BOXED__OBJECT_BOXED (GClosure *closure,
		GValue *return_value G_GNUC_UNUSED,
		guint n_param_values,
		const GValue *param_values,
		gpointer invocation_hint G_GNUC_UNUSED,
		gpointer marshal_data)
{
	typedef gpointer (*GMarshalFunc_BOXED__OBJECT_BOXED) (gpointer data1,
			gpointer arg_1,
			gpointer arg_2,
			gpointer data2);
	register GMarshalFunc_BOXED__OBJECT_BOXED callback;
	register GCClosure *cc = (GCClosure*) closure;
	register gpointer data1, data2;
	gpointer v_return;

	g_return_if_fail (return_value != NULL);
	g_return_if_fail (n_param_values == 3);

	if (G_CCLOSURE_SWAP_DATA (closure)) {
		data1 = closure->data;
		data2 = g_value_peek_pointer (param_values + 0);
	} else {
		data1 = g_value_peek_pointer (param_values + 0);
		data2 = closure->data;
	}
	callback = (GMarshalFunc_BOXED__OBJECT_BOXED)
    		(marshal_data ? marshal_data : cc->callback);

	v_return = callback (data1,
			g_marshal_value_peek_object (param_values + 1),
			g_marshal_value_peek_boxed (param_values + 2),
			data2);

	g_value_take_boxed (return_value, v_return);
}
