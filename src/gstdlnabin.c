/*
  This bin adds DLNA playback capabilities to souphttpsrc
 */

/**
 * SECTION:element-dlnabin
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

#include "gstdlnabin.h"

// GStreamer debugging facilities
//
GST_DEBUG_CATEGORY_STATIC (gst_dlna_bin_debug);
#define GST_CAT_DEFAULT gst_dlna_bin_debug

// Define the boilerplate type stuff to reduce typos and code size.
// Defines the get_type method and the parent_class static variable.
// Args are: type,type_as_function,parent_type,parent_type_macro
//
GST_BOILERPLATE (GstDlnaBin, gst_dlna_bin, GstElement, GST_TYPE_BIN);

/* props */
enum
{
	PROP_0,
	PROP_URI,
	PROP_DTCP_KEY_STORAGE,
	//...
};

// Constant names for elements in this bin
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
const GstElementDetails gst_dlna_bin_details
= GST_ELEMENT_DETAILS("HTTP/DLNA client source 11/19/12 7:26 PM",
		"Source/Network",
		"Receive data as a client via HTTP with DLNA extensions",
		"Eric Winkelman <e.winkelman@cablelabs.com>");

// Description of a pad that the element will (or might) create and use
//
/* TODO - is this really needed??? */
static GstStaticPadTemplate gst_dlna_bin_src_pad_template =
		GST_STATIC_PAD_TEMPLATE (
				"src",					// name for pad
				GST_PAD_SRC,				// direction of pad
				GST_PAD_ALWAYS,			// indicates if pad exists
				GST_STATIC_CAPS ("ANY")	// Supported types by this element (capabilities)
		);

// **********************
// Local method declarations associated with gstreamer framework function pointers
//
static void gst_dlna_bin_dispose (GObject * object);

static void gst_dlna_bin_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * spec);

static void gst_dlna_bin_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * spec);


// **********************
// Local method declarations
//
static GstDlnaBin* gst_dlna_build_bin (GstDlnaBin *dlna_bin);

static gboolean dlna_bin_setup_uri(GstDlnaBin *dlna_bin, const GValue * value);

static gboolean dlna_bin_non_dtcp_setup(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_dtcp_setup(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_open_socket(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_parse_uri(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_formulate_head_request(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_issue_head_request(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_close_socket(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_head_response_parse(GstDlnaBin *dlna_bin);

static gint dlna_bin_head_response_get_field_idx(GstDlnaBin *dlna_bin, gchar* field_str);

static gboolean dlna_bin_head_response_assign_field_value(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_time_seek(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_content_features(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_profile(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_operations(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_playspeeds(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_flags(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_dtcp_range(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_parse_content_type(GstDlnaBin *dlna_bin, gint idx, gchar* field_str);

static gboolean dlna_bin_head_response_is_flag_set(GstDlnaBin *dlna_bin, gchar* flags_str, gint flag);

static gboolean dlna_bin_head_response_init_struct(GstDlnaBin *dlna_bin);

static gboolean dlna_bin_head_response_struct_to_str(GstDlnaBin *dlna_bin);


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
gst_dlna_bin_base_init (gpointer gclass)
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
			gst_static_pad_template_get(&gst_dlna_bin_src_pad_template));
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
gst_dlna_bin_class_init (GstDlnaBinClass * klass)
{
	GObjectClass *gobject_klass;
	GstElementClass *gstelement_klass;
	//GstBinClass *gstbin_klass;

	gobject_klass = (GObjectClass *) klass;
	gstelement_klass = (GstElementClass *) klass;
	//gstbin_klass = (GstBinClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	gobject_klass->set_property = gst_dlna_bin_set_property;
	gobject_klass->get_property = gst_dlna_bin_get_property;

	g_object_class_install_property (gobject_klass, PROP_URI,
			g_param_spec_string ("uri", "Stream URI",
					"Sets URI A/V stream",
					NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_klass, PROP_DTCP_KEY_STORAGE,
			g_param_spec_string ("dtcp_key_storage", "dtcp_key_storage",
					"Directory that contains client's keys",
					"/media/truecrypt1/dll/test_keys", G_PARAM_READWRITE));


	gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_dlna_bin_dispose);

	gst_element_class_set_details (gstelement_klass, &gst_dlna_bin_details);

}

/*
 * Initializes a specific instance of this element, called when object
 * is created from within gstreamer framework.
 *
 * @param dlna_bin	specific instance of element to intialize
 * @param gclass	class representation of this element
 */
static void
gst_dlna_bin_init (GstDlnaBin * dlna_bin,
		GstDlnaBinClass * gclass)
{
    GST_LOG_OBJECT(dlna_bin, "Initializing");

    gst_dlna_build_bin(dlna_bin);

    GST_LOG_OBJECT(dlna_bin, "Initialization complete");
}

/**
 * Called by framework when tearing down pipeline
 *
 * @param object  element to destroy
 */
static void
gst_dlna_bin_dispose (GObject * object)
{
	GstDlnaBin* dlna_bin = GST_DLNA_BIN (object);

    GST_LOG_OBJECT(dlna_bin, " Disposing the dlna bin");

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
gst_dlna_bin_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec)
{
	GstDlnaBin *dlna_bin = GST_DLNA_BIN (object);

    GST_LOG_OBJECT(dlna_bin, "Setting property: %d", prop_id);

    switch (prop_id) {

	case PROP_URI:
	{
		if (!dlna_bin_setup_uri(dlna_bin, value))
		{
		    GST_ERROR_OBJECT(dlna_bin, "Failed to set URI property");
		}
		break;
	}
	case PROP_DTCP_KEY_STORAGE:
	{
		if (dlna_bin->dtcp_key_storage)
		{
			g_free(dlna_bin->dtcp_key_storage);
		}
		dlna_bin->dtcp_key_storage = g_value_dup_string (value);
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
gst_dlna_bin_get_property (GObject * object, guint prop_id, GValue * value,
		GParamSpec * pspec)
{
	GstDlnaBin *dlna_bin = GST_DLNA_BIN (object);
	GST_INFO_OBJECT(dlna_bin, "Getting property: %d", prop_id);

	switch (prop_id) {

	case PROP_URI:
		g_value_set_pointer(value, dlna_bin->uri);
		break;
    case PROP_DTCP_KEY_STORAGE:
      g_value_set_string (value, dlna_bin->dtcp_key_storage);
      break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/**
 * Constructs elements in this bin and links them together
 *
 * @param	dlna_bin	instance of dlna bin element
 */
static GstDlnaBin*
gst_dlna_build_bin (GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Building bin");

	//gst_dlna_build_bin(dlna_bin);
	//GstElement *http_src;
	//GstPad *pad, *gpad;

	// Create source element
	dlna_bin->http_src = gst_element_factory_make ("souphttpsrc", ELEMENT_NAME_HTTP_SRC);
	if (!dlna_bin->http_src) {
		GST_ERROR_OBJECT(dlna_bin, "The source element could not be created. Exiting.\n");
		// *TODO* - do we really want to exit here???
		exit(1);
	}

	// Add source element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), dlna_bin->http_src);

	// *TODO* - Connect a callback function to soup http src - but what???

	// Create the sink ghost pad
	/*
	pad = gst_element_get_static_pad(http_src, "src");
	if (!pad) {
		g_printerr ("Could not get pad for souphttpsrc\n");
		exit(1);
	}
	//gpad = gst_element_get_static_pad(GST_ELEMENT (&dlna_bin->bin), "src");
	gpad = gst_ghost_pad_new("src", pad);
	gst_pad_set_active (gpad, TRUE);
	gst_element_add_pad (GST_ELEMENT (&dlna_bin->bin), gpad);

	gst_object_unref (pad);
	//gst_object_unref (gpad);
	 */
	GST_LOG_OBJECT(dlna_bin, "Done building bin");

	return dlna_bin;
}

/**
 * Perform actions necessary based on supplied URI
 *
 * @param dlna_bin	this element
 * @param value		specified URI to use
 */
static gboolean
dlna_bin_setup_uri(GstDlnaBin *dlna_bin, const GValue * value)
{
	GST_LOG_OBJECT(dlna_bin, "Setting up URI");

	GstElement *elem;

	// Set the uri in the bin
	// (ew) Do we need to free the old value?
	if (dlna_bin->uri) {
		free(dlna_bin->uri);
	}
	dlna_bin->uri = g_value_dup_string(value);

	// Parse URI to get socket info & content info to send head request
	if (!dlna_bin_parse_uri(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems parsing URI");
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Open socket to send HEAD request
	if (!dlna_bin_open_socket(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems creating socket to send HEAD request\n");
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Formulate HEAD request
	if (!dlna_bin_formulate_head_request(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems formulating HEAD request\n");
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Send HEAD Request and read response
	if (!dlna_bin_issue_head_request(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems sending and receiving HEAD request\n");
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Close socket
	if (!dlna_bin_close_socket(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems closing socket used to send HEAD request\n");
	}

	// Parse HEAD response to gather info about URI content item
	if (!dlna_bin_head_response_parse(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems parsing HEAD response\n");
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Make sure return code from HEAD response is OK
	if ((dlna_bin->head_response->ret_code != 200) && (dlna_bin->head_response->ret_code != 201))
	{
		GST_ERROR_OBJECT(dlna_bin, "Error code received in HEAD response: %d %s\n",
				dlna_bin->head_response->ret_code, dlna_bin->head_response->ret_msg);
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = NULL;
		return FALSE;
	}

	// Get the soup http source
	elem = gst_bin_get_by_name(&dlna_bin->bin, ELEMENT_NAME_HTTP_SRC);

	// Set the URI
	g_object_set(G_OBJECT(elem), "location", dlna_bin->uri, NULL);

	GST_INFO_OBJECT(dlna_bin, "Set the URI to %s\n", dlna_bin->uri);

	// Setup elements based on HEAD response
	// Use flag or profile name starts with DTCP (
	// *TODO* - add check for profile name starting with DTCP
	if (dlna_bin->head_response->content_features->flag_link_protected_set)
	{
		if (!dlna_bin_dtcp_setup(dlna_bin))
		{
			GST_ERROR_OBJECT(dlna_bin, "Problems setting up dtcp elements\n");
			return FALSE;
		}
	}
	else
	{
		if (!dlna_bin_non_dtcp_setup(dlna_bin))
		{
			GST_ERROR_OBJECT(dlna_bin, "Problems setting up non-dtcp elements\n");
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * Parse URI and extract info necessary to open socket to send
 * HEAD request
 *
 * @param dlna_bin	this element
 *
 * @return	true if successfully parsed, false if problems encountered
 */
static gboolean
dlna_bin_parse_uri(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Parsing URI: %s", dlna_bin->uri);

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
    gchar *protocol = gst_uri_get_protocol(dlna_bin->uri);

    if (NULL != protocol)
    {
        if (strcmp(protocol, "http") == 0)
        {
            if (NULL != (addr = gst_uri_get_location(dlna_bin->uri)))
            {
                if (NULL != (p = strchr(addr, ':')))
                {
                    *p = 0; // so that the addr is null terminated where the address ends.
                    dlna_bin->uri_port = atoi(++p);
                    GST_INFO_OBJECT(dlna_bin, "Port retrieved: \"%d\".", dlna_bin->uri_port);
                }
                // If address is changing, free old
                if (NULL != dlna_bin->uri_addr && 0 != strcmp(dlna_bin->uri_addr, addr))
                {
                    g_free(dlna_bin->uri_addr);
                }
                if (NULL == dlna_bin->uri_addr || 0 != strcmp(dlna_bin->uri_addr, addr))
                {
                	dlna_bin->uri_addr = g_strdup(addr);
                }
                GST_INFO_OBJECT(dlna_bin, "New addr set: \"%s\".", dlna_bin->uri_addr);
                g_free(addr);
                g_free(protocol);
            }
            else
            {
                GST_ERROR_OBJECT(dlna_bin, "Location was null: \"%s\".", dlna_bin->uri);
                g_free(protocol);
                return FALSE;
            }
        }
        else
        {
            GST_ERROR_OBJECT(dlna_bin, "Protocol Info was NOT http: \"%s\".", protocol);
            return FALSE;
        }
    }
    else
    {
        GST_ERROR_OBJECT(dlna_bin, "Protocol Info was null: \"%s\".", dlna_bin->uri);
        return FALSE;
    }

    return TRUE;
}

/**
 * Create a socket for sending to HEAD request
 *
 * @param dlna_bin	this element
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_bin_open_socket(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Opening socket to URI src");

    // Create socket
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if ((dlna_bin->sock = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol)) == -1)
    {
        GST_ERROR_OBJECT(dlna_bin, "Socket creation failed");
        return FALSE;
    }

    gint ret = 0;
    gchar portStr[8] = {0};
    snprintf(portStr, sizeof(portStr), "%d", dlna_bin->uri_port);

    struct addrinfo* srvrInfo = NULL;
    if (0 != (ret = getaddrinfo(dlna_bin->uri_addr, portStr, &hints, &srvrInfo)))
    {
        GST_ERROR_OBJECT(dlna_bin, "getaddrinfo[%s]\n", gai_strerror(ret));
        return FALSE;
    }

    struct addrinfo* pSrvr = NULL;
    for(pSrvr = srvrInfo; pSrvr != NULL; pSrvr = pSrvr->ai_next)
    {
        if (0 > (dlna_bin->sock = socket(pSrvr->ai_family,
                                    pSrvr->ai_socktype,
                                    pSrvr->ai_protocol)))
        {
            GST_ERROR_OBJECT(dlna_bin, "socket() failed?");
            continue;
        }

        /*
        if (0 > setsockopt(dlna_bin->sock, SOL_SOCKET, SO_REUSEADDR,
                           (char*) &yes, sizeof(yes)))
        {
            GST_ERROR_OBJECT(dlna_bin, "setsockopt() failed?");
            return FALSE;
        }
		*/
        GST_LOG_OBJECT(dlna_bin, "Got sock: %d\n", dlna_bin->sock);

        if (connect(dlna_bin->sock, pSrvr->ai_addr, pSrvr->ai_addrlen) != 0)
        {
        	GST_WARNING_OBJECT(dlna_bin, "bind() failed?");
            continue;
        }

        // Successfully connected
        GST_INFO_OBJECT(dlna_bin, "Successful connect to sock: %d\n", dlna_bin->sock);
        break;
    }

    if (NULL == pSrvr)
    {
        GST_ERROR_OBJECT(dlna_bin, "failed to bind");
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
dlna_bin_close_socket(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Closing socket used for HEAD request");

	if (dlna_bin->sock >= 0)
		CLOSESOCK(dlna_bin->sock);

#ifdef RI_WIN32_SOCKETS
	WSACleanup();
#endif

	return TRUE;
}

/**
 * Creates the string which represents the HEAD request to send
 * to server to get info related to URI
 *
 * @param dlna_bin	this element
 *			GST_WARNING_OBJECT(dlna_bin,
				"No BYTES= found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_bin_formulate_head_request(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Formulating head request");

    gchar requestStr[MAX_HTTP_BUF_SIZE];
    gchar tmpStr[32];

    strcpy(requestStr, "HEAD ");

    strcat(requestStr, dlna_bin->uri);

    strcat(requestStr, " HTTP/1.1");
    strcat(requestStr, CRLF);

    strcat(requestStr, "HOST: ");
    strcat(requestStr, dlna_bin->uri_addr);
    strcat(requestStr, ":");

    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_bin->uri_port);
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

    dlna_bin->head_request_str = g_strdup(requestStr);
	GST_LOG_OBJECT(dlna_bin, "HEAD Request: %s", dlna_bin->head_request_str);

    return TRUE;
}

/**
 * Sends the HEAD request to server, reads response, parses and
 * stores info related to this URI.
 *
 * @param dlna_bin	this element
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_bin_issue_head_request(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Issuing head request: %s", dlna_bin->head_request_str);

	// Send HEAD request on socket
    gint bytesTxd = 0;
    gint bytesToTx = strlen(dlna_bin->head_request_str);

    if ((bytesTxd = send(dlna_bin->sock, dlna_bin->head_request_str, bytesToTx, 0)) < -1)
    {
        GST_ERROR_OBJECT(dlna_bin, "Problems sending on socket");
        return FALSE;
    }
    else if (bytesTxd == -1)
    {
        GST_ERROR_OBJECT(dlna_bin, "Problems sending on socket, got back -1");
        return FALSE;
    }
    else if (bytesTxd != bytesToTx)
    {
        GST_ERROR_OBJECT(dlna_bin, "Sent %d bytes instead of %d", bytesTxd, bytesToTx);
        return FALSE;
    }
	GST_INFO_OBJECT(dlna_bin, "Issued head request: %s", dlna_bin->head_request_str);

	// Read HEAD response
    gint bytesRcvd = 0;
    gchar responseStr[MAX_HTTP_BUF_SIZE];

    if ((bytesRcvd = recv(dlna_bin->sock, responseStr, MAX_HTTP_BUF_SIZE, 0)) <= 0)
    {
        GST_ERROR_OBJECT(dlna_bin, "HEAD Response recv() failed");
        return FALSE;
    }
    else
    {
    	// Null terminate response string
    	responseStr[bytesRcvd] = '\0';
    }
    dlna_bin->head_response_str = g_strdup(responseStr);
	GST_INFO_OBJECT(dlna_bin, "HEAD Response received: %s", dlna_bin->head_response_str);

	return TRUE;
}

/**
 * Parse HEAD response into specific values related to URI content item.
 *
 * @param	dlna_bin	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_bin_head_response_parse(GstDlnaBin *dlna_bin)
{
	GST_LOG_OBJECT(dlna_bin, "Parsing HEAD Response: %s", dlna_bin->head_response_str);

	// Initialize structure to hold parsed HEAD Response
	if (!dlna_bin_head_response_init_struct(dlna_bin))
	{
        GST_ERROR_OBJECT(dlna_bin, "Problems initializing struct to store HEAD response");
        return FALSE;
	}

	// Convert all header field strings to upper case to aid in parsing
	int i = 0;
	for (i = 0; dlna_bin->head_response_str[i]; i++)
	{
		dlna_bin->head_response_str[i] = toupper(dlna_bin->head_response_str[i]);
	}

	// Initialize array of strings used to store field values
	char* fields[HEAD_RESPONSE_HDRS_CNT];
	for (i = 0; i < HEAD_RESPONSE_HDRS_CNT; i++)
	{
		fields[i] = NULL;
	}

	// Tokenize HEAD response into individual field values using CRLF as delim
	char* tokens = strtok(dlna_bin->head_response_str, CRLF);
	while (tokens != NULL)
	{
		// Look for field header contained in this string
		gint idx = dlna_bin_head_response_get_field_idx(dlna_bin, tokens);

		// If found field header, extract value
		if (idx != -1)
		{
			fields[idx] = tokens;
		}
		else
		{
			GST_WARNING_OBJECT(dlna_bin, "No Idx found for Field:%s", tokens);
		}

		// Go on to next field
		tokens = strtok(NULL, CRLF);
	}

	// Parse value from each field header string
	for (i = 0; i < HEAD_RESPONSE_HDRS_CNT; i++)
	{
		if (fields[i] != NULL)
		{
			dlna_bin_head_response_assign_field_value(dlna_bin, i, fields[i]);
		}
	}

	// Print out results of HEAD request
	if (!dlna_bin_head_response_struct_to_str(dlna_bin))
	{
        GST_ERROR_OBJECT(dlna_bin, "Problems converting HEAD response struct to string");
        return FALSE;
	}
	else
	{
        GST_INFO_OBJECT(dlna_bin, "Parsed HEAD Response into struct: %s",
        		dlna_bin->head_response->struct_str);
	}
	return TRUE;
}

/**
 * Initialize structure to store HEAD Response
 *
 * @param	dlna_bin	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_bin_head_response_init_struct(GstDlnaBin *dlna_bin)
{
	// Allocate storage
	dlna_bin->head_response = g_try_malloc0(sizeof(GstDlnaBinHeadResponse));
	dlna_bin->head_response->content_features = g_try_malloc0(sizeof(GstDlnaBinHeadResponseContentFeatures));

	// Initialize structs
	// {"HTTP", STRING_TYPE}
	dlna_bin->head_response->http_rev_idx = HDR_IDX_HTTP;
	dlna_bin->head_response->http_rev = NULL;
	dlna_bin->head_response->ret_code = 0;
	dlna_bin->head_response->ret_msg = NULL;

	// {"TIMESEEKRANGE.DLNA.ORG", STRING_TYPE},
	dlna_bin->head_response->time_seek_idx = HDR_IDX_TIMESEEKRANGE;

	// {"NPT", NPT_RANGE_TYPE},
	dlna_bin->head_response->npt_seek_idx = HDR_IDX_NPT;
	dlna_bin->head_response->time_seek_npt_start = NULL;
	dlna_bin->head_response->time_seek_npt_end = NULL;
	dlna_bin->head_response->time_seek_npt_duration = NULL;

	// {"BYTES", BYTE_RANGE_TYPE},
	dlna_bin->head_response->byte_seek_idx = HDR_IDX_BYTES;
	dlna_bin->head_response->byte_seek_start = 0;
	dlna_bin->head_response->byte_seek_end = 0;
	dlna_bin->head_response->byte_seek_total = 0;

	// {CONTENT RANGE DTCP, BYTE_RANGE_TYPE},
	dlna_bin->head_response->dtcp_range_idx = HDR_IDX_DTCP_RANGE;
	dlna_bin->head_response->dtcp_range_start = 0;
	dlna_bin->head_response->dtcp_range_end = 0;
	dlna_bin->head_response->dtcp_range_total = 0;

	// {"TRANSFERMODE.DLNA.ORG", STRING_TYPE}
	dlna_bin->head_response->transfer_mode_idx = HDR_IDX_TRANSFERMODE;
	dlna_bin->head_response->transfer_mode = NULL;

	// {"TRANSFER-ENCODING", STRING_TYPE}
	dlna_bin->head_response->transfer_encoding_idx = HDR_IDX_TRANSFER_ENCODING;

	dlna_bin->head_response->transfer_encoding = NULL;

	// {"DATE", STRING_TYPE}
	dlna_bin->head_response->date_idx = HDR_IDX_DATE;
	dlna_bin->head_response->date = NULL;

	// {"SERVER", STRING_TYPE}
	dlna_bin->head_response->server_idx = HDR_IDX_SERVER;
	dlna_bin->head_response->server = NULL;

	// {"CONTENT-TYPE", STRING_TYPE}
	dlna_bin->head_response->content_type_idx = HDR_IDX_CONTENT_TYPE;
	dlna_bin->head_response->content_type = NULL;

	// Addition subfields in CONTENT TYPE if dtcp encrypted
	dlna_bin->head_response->dtcp_host_idx = HDR_IDX_DTCP_HOST;
	dlna_bin->head_response->dtcp_host = NULL;
	dlna_bin->head_response->dtcp_port_idx = HDR_IDX_DTCP_PORT;
	dlna_bin->head_response->dtcp_port = -1;
	dlna_bin->head_response->content_format_idx = HDR_IDX_CONTENT_FORMAT;

	// {"CONTENTFEATURES.DLNA.ORG", STRING_TYPE},
	dlna_bin->head_response->content_features_idx = HDR_IDX_CONTENTFEATURES;

	// {"DLNA.ORG_PN", STRING_TYPE}
	dlna_bin->head_response->content_features->profile_idx = HDR_IDX_PN;
    dlna_bin->head_response->content_features->profile = NULL;

	// {"DLNA.ORG_OP", FLAG_TYPE}
    dlna_bin->head_response->content_features->operations_idx = HDR_IDX_OP;
	dlna_bin->head_response->content_features->op_time_seek_supported = FALSE;
	dlna_bin->head_response->content_features->op_range_supported = FALSE;

	// {"DLNA.ORG_PS", NUMERIC_TYPE}, // 13
	dlna_bin->head_response->content_features->playspeeds_idx = HDR_IDX_PS;
    dlna_bin->head_response->content_features->playspeeds_cnt = 0;

	// {"DLNA.ORG_FLAGS", FLAG_TYPE} // 14
	dlna_bin->head_response->content_features->flags_idx = HDR_IDX_FLAGS;
	dlna_bin->head_response->content_features->flag_sender_paced_set = FALSE;
	dlna_bin->head_response->content_features->flag_limited_time_seek_set = FALSE;
	dlna_bin->head_response->content_features->flag_limited_byte_seek_set = FALSE;
	dlna_bin->head_response->content_features->flag_play_container_set = FALSE;
	dlna_bin->head_response->content_features->flag_so_increasing_set = FALSE;
	dlna_bin->head_response->content_features->flag_sn_increasing_set = FALSE;
	dlna_bin->head_response->content_features->flag_rtsp_pause_set = FALSE;
	dlna_bin->head_response->content_features->flag_streaming_mode_set = FALSE;
	dlna_bin->head_response->content_features->flag_interactive_mode_set = FALSE;
	dlna_bin->head_response->content_features->flag_background_mode_set = FALSE;
	dlna_bin->head_response->content_features->flag_stalling_set = FALSE;
	dlna_bin->head_response->content_features->flag_dlna_v15_set = FALSE;
	dlna_bin->head_response->content_features->flag_link_protected_set = FALSE;
	dlna_bin->head_response->content_features->flag_full_clear_text_set = FALSE;
	dlna_bin->head_response->content_features->flag_limited_clear_text_set = FALSE;

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
dlna_bin_head_response_get_field_idx(GstDlnaBin *dlna_bin, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Determine associated HEAD response field");

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
 * @param	dlna_bin	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_bin_head_response_assign_field_value(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Store value received in HEAD response field for field %d - %s",
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
		dlna_bin->head_response->transfer_mode = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_DATE:
		dlna_bin->head_response->date = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_CONTENT_TYPE:
		if (!dlna_bin_head_response_parse_content_type(dlna_bin, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_bin,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_SERVER:
		dlna_bin->head_response->server = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_TRANSFER_ENCODING:
		dlna_bin->head_response->transfer_encoding = g_strdup((strstr(field_str, ":")+1));
		break;

	case HDR_IDX_HTTP:
		// *TODO* - verify this is correct based on allowable white spaces
		strcat(field_str, "\n");
		if ((ret_code = sscanf(field_str, "%s %d %[^\n]", tmp1, &int_value, tmp2)) != 3)
		{
			GST_WARNING_OBJECT(dlna_bin,
					"Problems with HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, tmp: %s, %s",
					HEAD_RESPONSE_HDRS[idx], idx, field_str, ret_code, tmp1, tmp2);
		}
		else
		{
			dlna_bin->head_response->http_rev = g_strdup(tmp1);
			dlna_bin->head_response->ret_code = int_value;
			dlna_bin->head_response->ret_msg = g_strdup(tmp2);
		}
		break;

	case HDR_IDX_TIMESEEKRANGE:
		if (!dlna_bin_head_response_parse_time_seek(dlna_bin, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_bin,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_CONTENTFEATURES:
		if (!dlna_bin_head_response_parse_content_features(dlna_bin, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_bin,
					"Problems with HEAD response field hdr %s, value: %s",
					HEAD_RESPONSE_HDRS[idx], field_str);
		}
		break;

	case HDR_IDX_DTCP_RANGE:
		if (!dlna_bin_head_response_parse_dtcp_range(dlna_bin, idx, field_str))
		{
			GST_WARNING_OBJECT(dlna_bin,
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
		GST_WARNING_OBJECT(dlna_bin, "Unsupported HEAD response field idx %d: %s", idx, field_str);
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
 * @param	dlna_bin	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE
 */
static gboolean
dlna_bin_head_response_parse_time_seek(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
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
			GST_WARNING_OBJECT(dlna_bin,
				"Problems parsing NPT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s, %s",
				HEAD_RESPONSE_HDRS[idx], tmp_str1, ret_code, tmp1, tmp2, tmp3);
		}
		else
		{
			dlna_bin->head_response->time_seek_npt_start = g_strdup(tmp1);
			dlna_bin->head_response->time_seek_npt_end = g_strdup(tmp2);
			dlna_bin->head_response->time_seek_npt_duration = g_strdup(tmp3);
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_bin,
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
				GST_WARNING_OBJECT(dlna_bin,
					"Problems parsing BYTES from HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, ullong: %llu, %llu, %llu",
					HEAD_RESPONSE_HDRS[idx], idx, tmp_str1, ret_code, ullong1, ullong2, ullong3);
			}
			else
			{
				dlna_bin->head_response->byte_seek_start = ullong1;
				dlna_bin->head_response->byte_seek_end = ullong2;
				dlna_bin->head_response->byte_seek_total = ullong3;
			}
		}
		else
		{
			GST_WARNING_OBJECT(dlna_bin,
				"No BYTES= found in time seek range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_bin,
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
 * @param	dlna_bin	this element instance
 * @param	idx			index which describes HEAD response field and type
 * @param	fieldStr	string containing HEAD response field header and value
 *
 * @return	returns TRUE
 */
static gboolean
dlna_bin_head_response_parse_dtcp_range(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
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
			GST_WARNING_OBJECT(dlna_bin,
					"Problems parsing BYTES from HEAD response field hdr %s, idx: %d, value: %s, retcode: %d, ullong: %llu, %llu, %llu",
					HEAD_RESPONSE_HDRS[idx], idx, tmp_str1, ret_code, ullong1, ullong2, ullong3);
		}
		else
		{
			dlna_bin->head_response->dtcp_range_start = ullong1;
			dlna_bin->head_response->dtcp_range_end = ullong2;
			dlna_bin->head_response->dtcp_range_total = ullong3;
		}
	}
	else
	{
		GST_WARNING_OBJECT(dlna_bin,
				"No BYTES= found in dtcp range HEAD response field hdr %s, idx: %d, value: %s",
				HEAD_RESPONSE_HDRS[idx], idx, field_str);
	}
	return TRUE;
}

/**
 * Extract values from content features header in HEAD Response
 *
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	content feature header field extracted from HEAD response
 *
 * @return	TRUE
 */
static gboolean
dlna_bin_head_response_parse_content_features(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Called with field str: %s", field_str);

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
				GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_PN]);
				pn_str = tokens;
			}
			// "DLNA.ORG_OP"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_OP])) != NULL)
			{
				GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_OP]);
				op_str = tokens;
			}
			// "DLNA.ORG_PS"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_PS])) != NULL)
			{
				GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_PS]);
				ps_str = tokens;
			}
			// "DLNA.ORG_FLAGS"
			else if ((tmp_str2 = strstr(tokens, CONTENT_FEATURES_HDRS[HDR_IDX_FLAGS])) != NULL)
			{
				GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_FEATURES_HDRS[HDR_IDX_FLAGS]);
				flags_str = tokens;
			}
			else
			{
				GST_WARNING_OBJECT(dlna_bin, "Unrecognized sub field:%s", tokens);
			}

			// Go on to next field
			tokens = strtok(NULL, ";");
		}
	}

	if (pn_str != NULL)
	{
		if (!dlna_bin_head_response_parse_profile(dlna_bin, idx, pn_str))
		{
			GST_WARNING_OBJECT(dlna_bin, "Problems parsing profile sub field: %s", pn_str);
		}
	}
	if (op_str != NULL)
	{
		if (!dlna_bin_head_response_parse_operations(dlna_bin, idx, op_str))
		{
			GST_WARNING_OBJECT(dlna_bin, "Problems parsing operations sub field: %s", op_str);
		}
	}
	if (ps_str != NULL)
	{
		if (!dlna_bin_head_response_parse_playspeeds(dlna_bin, idx, ps_str))
		{
			GST_WARNING_OBJECT(dlna_bin, "Problems parsing playspeeds sub field: %s", ps_str);
		}
	}
	if (flags_str != NULL)
	{
		if (!dlna_bin_head_response_parse_flags(dlna_bin, idx, flags_str))
		{
			GST_WARNING_OBJECT(dlna_bin, "Problems parsing flags sub field: %s", flags_str);
		}
	}
	return TRUE;
}

/**
 * Parse DLNA profile identified by DLNA.ORG_PN header.
 *
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean dlna_bin_head_response_parse_profile(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Found PN Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_bin,
		"Problems parsing DLNA.ORG_PN from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		dlna_bin->head_response->content_features->profile = g_strdup(tmp2);
	}
	return TRUE;
}

/**
 * Parse DLNA supported operations sub field identified by DLNA.ORG_OP header.
 *
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_OP field
 *
 * @return	TRUE
 */
static gboolean dlna_bin_head_response_parse_operations(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Found OP Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_bin,
		"Problems parsing DLNA.ORG_OP from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		GST_LOG_OBJECT(dlna_bin, "OP Field value: %s", tmp2);

		// Verify length is as expected = 2
		if (strlen(tmp2) != 2)
		{
			GST_WARNING_OBJECT(dlna_bin,
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
					dlna_bin->head_response->content_features->op_time_seek_supported = FALSE;
				}
				else
				{
					dlna_bin->head_response->content_features->op_time_seek_supported = TRUE;
				}
			}
			else
			{
				GST_WARNING_OBJECT(dlna_bin,
					"DLNA.ORG_OP Time Seek Flag from HEAD response sub field value: %s, is not 0 or 1",
					field_str, tmp2);
			}

			// Second char represents range support
			if ((tmp2[1] == '0') || (tmp2[1] == '1'))
			{
				if (tmp2[1] == '0')
				{
					dlna_bin->head_response->content_features->op_range_supported = FALSE;
				}
				else
				{
					dlna_bin->head_response->content_features->op_range_supported = TRUE;
				}
			}
			else
			{
				GST_WARNING_OBJECT(dlna_bin,
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
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PS field
 *
 * @return	TRUE
 */
static gboolean dlna_bin_head_response_parse_playspeeds(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Found PS Field: %s", field_str);
	gint ret_code = 0;
	char* save_ptr;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_bin,
		"Problems parsing DLNA.ORG_PS from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
		return FALSE;
	}
	else
	{
		GST_LOG_OBJECT(dlna_bin, "PS Field value: %s", tmp2);

		// Tokenize list of comma separated playspeeds
		char* playspeeds = strtok_r(tmp2, ",", &save_ptr);
		while ((playspeeds != NULL) &&
				(dlna_bin->head_response->content_features->playspeeds_cnt < PLAYSPEEDS_MAX_CNT))
		{
			GST_LOG_OBJECT(dlna_bin, "Found PS: %s", playspeeds);
			dlna_bin->head_response->content_features->playspeeds[
			     dlna_bin->head_response->content_features->playspeeds_cnt] = g_strdup(playspeeds);
			dlna_bin->head_response->content_features->playspeeds_cnt++;

			// Go on to next field
			playspeeds = strtok_r(NULL, ",", &save_ptr);
		}
	}

	GST_LOG_OBJECT(dlna_bin, "Found PS Field: %s", field_str);

	return TRUE;
}

/**
 * Parse DLNA flags sub field identified by DLNA.ORG_FLAGS header.
 *
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_FLAGS field
 *
 * @return	TRUE
 */
static gboolean dlna_bin_head_response_parse_flags(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Found Flags Field: %s", field_str);
	gint ret_code = 0;

	// *TODO* - are these big enough?
	gchar tmp1[256];
	gchar tmp2[256];

	if ((ret_code = sscanf(field_str, "%[^=]=%s", tmp1, tmp2)) != 2)
	{
		GST_WARNING_OBJECT(dlna_bin,
		"Problems parsing DLNA.ORG_FLAGS from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s",
				HEAD_RESPONSE_HDRS[idx], field_str, ret_code, tmp1, tmp2);
	}
	else
	{
		GST_LOG_OBJECT(dlna_bin, "FLAGS Field value: %s", tmp2);

		// Get value of each of the defined flags
		dlna_bin->head_response->content_features->flag_sender_paced_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, SP_FLAG);
		dlna_bin->head_response->content_features->flag_limited_time_seek_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, LOP_NPT);
		dlna_bin->head_response->content_features->flag_limited_byte_seek_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, LOP_BYTES);
		dlna_bin->head_response->content_features->flag_play_container_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, PLAYCONTAINER_PARAM);
		dlna_bin->head_response->content_features->flag_so_increasing_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, S0_INCREASING);
		dlna_bin->head_response->content_features->flag_sn_increasing_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, SN_INCREASING);
		dlna_bin->head_response->content_features->flag_rtsp_pause_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, RTSP_PAUSE);
		dlna_bin->head_response->content_features->flag_streaming_mode_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, TM_S);
		dlna_bin->head_response->content_features->flag_interactive_mode_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, TM_I);
		dlna_bin->head_response->content_features->flag_background_mode_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, TM_B);
		dlna_bin->head_response->content_features->flag_stalling_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, HTTP_STALLING);
		dlna_bin->head_response->content_features->flag_dlna_v15_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, DLNA_V15_FLAG);
		dlna_bin->head_response->content_features->flag_link_protected_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, LP_FLAG);
		dlna_bin->head_response->content_features->flag_full_clear_text_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, CLEARTEXTBYTESEEK_FULL_FLAG);
		dlna_bin->head_response->content_features->flag_limited_clear_text_set =
				dlna_bin_head_response_is_flag_set(dlna_bin, tmp2, LOP_CLEARTEXTBYTES);
	}

	return TRUE;
}

/**
 * Parse content type identified by CONTENT-TYPE header.  Includes additional
 * subfields when content is DTCP encrypted.
 *
 * @param	dlna_bin	this element
 * @param	idx			index into array of header strings
 * @param	field_str	sub field string containing DLNA.ORG_PN field
 *
 * @return	TRUE
 */
static gboolean dlna_bin_head_response_parse_content_type(GstDlnaBin *dlna_bin, gint idx, gchar* field_str)
{
	GST_LOG_OBJECT(dlna_bin, "Found Content Type Field: %s", field_str);
	gint ret_code = 0;
	gchar tmp1[32];
	gchar tmp2[32];
	gchar tmp3[32];

	// If not DTCP content, this field is mime-type
	if (strstr(field_str, "DTCP") == NULL)
	{
		dlna_bin->head_response->content_type = g_strdup((strstr(field_str, ":")+1));
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
					GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_HOST]);
					dlna_bin->head_response->dtcp_host = g_strdup((strstr(tmp_str2, "=")+1));
				}
				// DTCP1PORT
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT])) != NULL)
				{
					if ((ret_code = sscanf(tmp_str2, "%[^=]=%d", tmp1,
										   &dlna_bin->head_response->dtcp_port)) != 2)
					{
						GST_WARNING_OBJECT(dlna_bin,
						"Problems parsing DTCP PORT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s",
								HEAD_RESPONSE_HDRS[idx], tmp_str2, ret_code, tmp1);
					}
					else
					{
						GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT]);
					}
				}
				// CONTENTFORMAT
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_CONTENT_FORMAT])) != NULL)
				{
					if ((ret_code = sscanf(tmp_str2, "%[^=]=\"%[^\"]%s", tmp1, tmp2, tmp3)) != 3)
					{
						GST_WARNING_OBJECT(dlna_bin,
						"Problems parsing DTCP CONTENT FORMAT from HEAD response field hdr %s, value: %s, retcode: %d, tmp: %s, %s, %s",
								HEAD_RESPONSE_HDRS[idx], tmp_str2, ret_code, tmp1, tmp2, tmp3);
					}
					else
					{
						GST_LOG_OBJECT(dlna_bin, "Found field: %s", CONTENT_TYPE_HDRS[HDR_IDX_DTCP_PORT]);
						dlna_bin->head_response->content_type = g_strdup(tmp2);
					}
				}
				//  APPLICATION/X-DTCP1
				else if ((tmp_str2 = strstr(tokens, CONTENT_TYPE_HDRS[HDR_IDX_APP_DTCP])) != NULL)
				{
					// Ignore this field
				}
				else
				{
					GST_WARNING_OBJECT(dlna_bin, "Unrecognized sub field:%s", tokens);
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
static gboolean dlna_bin_head_response_is_flag_set(GstDlnaBin *dlna_bin, gchar* flags_str, gint flag)
{
	if ((flags_str == NULL) || (strlen(flags_str) <= RESERVED_FLAGS_LENGTH))
	{
		GST_WARNING_OBJECT(dlna_bin, "FLAGS Field value null or too short : %s", flags_str);
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
 * @param	dlna_bin	this element instance
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_bin_head_response_struct_to_str(GstDlnaBin *dlna_bin)
{
	GST_INFO_OBJECT(dlna_bin, "Formatting HEAD Response struct");

    gchar structStr[2048];
    gchar tmpStr[32];

    strcpy(structStr, "\nHTTP Version: ");
    if (dlna_bin->head_response->http_rev != NULL)
    	strcat(structStr, dlna_bin->head_response->http_rev);
    strcat(structStr, "\n");

    strcat(structStr, "HEAD Ret Code: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_bin->head_response->ret_code);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "HEAD Ret Msg: ");
    if (dlna_bin->head_response->ret_msg != NULL)
    	strcat(structStr, dlna_bin->head_response->ret_msg);
    strcat(structStr, "\n");

    strcat(structStr, "Server: ");
    if (dlna_bin->head_response->server != NULL)
    	strcat(structStr, dlna_bin->head_response->server);
    strcat(structStr, "\n");

    strcat(structStr, "Date: ");
    if (dlna_bin->head_response->date != NULL)
    	strcat(structStr, dlna_bin->head_response->date);
    strcat(structStr, "\n");

    strcat(structStr, "Content Type: ");
    if (dlna_bin->head_response->content_type != NULL)
    	strcat(structStr, dlna_bin->head_response->content_type);
    strcat(structStr, "\n");

    if (dlna_bin->head_response->dtcp_host != NULL)
    {
        strcat(structStr, "DTCP Host: ");
        strcat(structStr, dlna_bin->head_response->dtcp_host);
        strcat(structStr, "\n");

        strcat(structStr, "DTCP Port: ");
        (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
        sprintf(tmpStr, "%d", dlna_bin->head_response->dtcp_port);
        strcat(structStr, tmpStr);
        strcat(structStr, "\n");
    }

    strcat(structStr, "HTTP Transfer Encoding: ");
    if (dlna_bin->head_response->transfer_encoding != NULL)
    	strcat(structStr, dlna_bin->head_response->transfer_encoding);
    strcat(structStr, "\n");

	strcat(structStr, "DLNA Transfer Mode: ");
    if (dlna_bin->head_response->transfer_mode != NULL)
    	strcat(structStr, dlna_bin->head_response->transfer_mode);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT Start: ");
    if (dlna_bin->head_response->time_seek_npt_start != NULL)
    	strcat(structStr, dlna_bin->head_response->time_seek_npt_start);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT End: ");
    if (dlna_bin->head_response->time_seek_npt_end != NULL)
    	strcat(structStr, dlna_bin->head_response->time_seek_npt_end);
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek NPT Duration: ");
    if (dlna_bin->head_response->time_seek_npt_duration != NULL)
    	strcat(structStr, dlna_bin->head_response->time_seek_npt_duration);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek Start: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_bin->head_response->byte_seek_start);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek End: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_bin->head_response->byte_seek_end);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Byte Seek Total: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%lld", dlna_bin->head_response->byte_seek_total);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    if (dlna_bin->head_response->dtcp_range_total != 0)
    {
    	strcat(structStr, "DTCP Range Start: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_bin->head_response->dtcp_range_start);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");

    	strcat(structStr, "DTCP Range End: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_bin->head_response->dtcp_range_end);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");

    	strcat(structStr, "DTCP Range Total: ");
    	(void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    	sprintf(tmpStr, "%lld", dlna_bin->head_response->dtcp_range_total);
    	strcat(structStr, tmpStr);
    	strcat(structStr, "\n");
    }

    strcat(structStr, "DLNA Profile: ");
    if (dlna_bin->head_response->content_features->profile != NULL)
    	strcat(structStr, dlna_bin->head_response->content_features->profile);
    strcat(structStr, "\n");

    strcat(structStr, "Supported Playspeed Cnt: ");
    (void) memset((gchar *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_bin->head_response->content_features->playspeeds_cnt);
    strcat(structStr, tmpStr);
    strcat(structStr, "\n");

    strcat(structStr, "Playspeeds: ");
    gint i = 0;
    for (i = 0; i < dlna_bin->head_response->content_features->playspeeds_cnt; i++)
    {
        strcat(structStr, dlna_bin->head_response->content_features->playspeeds[i]);
        strcat(structStr, ", ");
    }
    strcat(structStr, "\n");

    strcat(structStr, "Time Seek Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->op_time_seek_supported) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Range Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->op_range_supported) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Sender Paced?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_sender_paced_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Time Seek?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_limited_time_seek_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Byte Seek?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_limited_byte_seek_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Play Container?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_play_container_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "S0 Increasing?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_so_increasing_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Sn Increasing?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_sn_increasing_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "RTSP Pause?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_rtsp_pause_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Streaming Mode Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_streaming_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Interactive Mode Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_interactive_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Background Mode Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_background_mode_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Connection Stalling Supported?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_stalling_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "DLNA Ver. 1.5?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_dlna_v15_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Link Protected?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_link_protected_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Full Clear Text?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_full_clear_text_set) ? "TRUE\n" : "FALSE\n");

    strcat(structStr, "Limited Clear Text?: ");
    strcat(structStr, (dlna_bin->head_response->content_features->flag_limited_clear_text_set) ? "TRUE\n" : "FALSE\n");

    // Copy local string to struct str
    dlna_bin->head_response->struct_str = g_strdup(structStr);

	return TRUE;
}

/**
 * Setup this bin in order to handle non-DTCP encrypted content
 *
 * @param dlna_bin	this element
 *
 * @return	returns TRUE if no problems are encountered, false otherwise
 */
static gboolean
dlna_bin_non_dtcp_setup(GstDlnaBin *dlna_bin)
{
	GST_INFO_OBJECT(dlna_bin, "Setup for non-dtcp content");

	// Create non-encrypt sink element
	GstElement *file_sink = gst_element_factory_make ("filesink", ELEMENT_NAME_FILE_SINK);
	if (!file_sink) {
		GST_ERROR_OBJECT(dlna_bin, "The sink element could not be created. Exiting.\n");
		return FALSE;
	}

	// *TODO* - this should be property of this element???
	// Hardcode the file sink location to be tmp.txt
	g_object_set (G_OBJECT(file_sink), "location", "non_dtcp.txt", NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), file_sink);

	// Link elements together
	if (!gst_element_link_many(dlna_bin->http_src, file_sink, NULL))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems linking elements in bin. Exiting.\n");
		return FALSE;
	}
	return TRUE;
}

/**
 * Setup dtcp decoder element and add to bin in order to handle DTCP encrypted
 * content
 *
 * @param dlna_bin	this element
 */
static gboolean
dlna_bin_dtcp_setup(GstDlnaBin *dlna_bin)
{
	GST_INFO_OBJECT(dlna_bin, "Setup for dtcp content");

	// Create non-encrypt sink element
	GST_INFO_OBJECT(dlna_bin, "Creating dtcp decrypter");
	dlna_bin->dtcp_decrypter = gst_element_factory_make ("dtcpip",
														ELEMENT_NAME_DTCP_DECRYPTER);
	if (!dlna_bin->dtcp_decrypter) {
		GST_ERROR_OBJECT(dlna_bin, "The dtcp decrypter element could not be created. Exiting.\n");
		return FALSE;
	}

	// Set DTCP host property
	g_object_set(G_OBJECT(dlna_bin->dtcp_decrypter), "dtcp1host",
			dlna_bin->head_response->dtcp_host, NULL);

	// Set DTCP port property
	g_object_set(G_OBJECT(dlna_bin->dtcp_decrypter), "dtcp1port",
			dlna_bin->head_response->dtcp_port, NULL);

	// Set DTCP key storage property
	g_object_set(G_OBJECT(dlna_bin->dtcp_decrypter), "dtcpip_storage",
			dlna_bin->dtcp_key_storage, NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), dlna_bin->dtcp_decrypter);

	// Create file sink element
	GstElement *file_sink = gst_element_factory_make ("filesink", ELEMENT_NAME_FILE_SINK);
	if (!file_sink) {
		GST_ERROR_OBJECT(dlna_bin, "The sink element could not be created. Exiting.\n");
		return FALSE;
	}

	// *TODO* - this should be property of this element???
	// Hardcode the file sink location to be tmp.txt
	g_object_set (G_OBJECT(file_sink), "location", "dtcp.txt", NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), file_sink);

	// Link elements together
	if (!gst_element_link_many(dlna_bin->http_src, dlna_bin->dtcp_decrypter, file_sink, NULL))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems linking elements in bin. Exiting.\n");
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
dlna_bin_init (GstPlugin * dlna_bin)
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template ' with your description
	 */
	GST_DEBUG_CATEGORY_INIT (gst_dlna_bin_debug, "dlnabin",
			0, "MPEG+DLNA Player");

	return gst_element_register ((GstPlugin *)dlna_bin, "dlnabin",
			GST_RANK_NONE, GST_TYPE_DLNA_BIN);
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dlnabin"
#endif

/* gstreamer looks for this structure to register eisss
 *
 * exchange the string 'Template eiss' with your eiss description
 */
GST_PLUGIN_DEFINE (
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"dlnabin",
		"MPEG+DLNA Decoder",
		(GstPluginInitFunc)dlna_bin_init,
		VERSION,
		"LGPL",
		"gst-cablelabs_ri",
		"http://gstreamer.net/");

#endif

/*
  Function for marshaling the callback arguments into a function closure.

  Taken from the decodebin code, so we can replicate the interface.
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
