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
	ARG_0,
	ARG_URI,
	//...
};

// Constant names for elements in this bin
#define ELEMENT_NAME_HTTP_SRC "http-source"
#define ELEMENT_NAME_NON_DTCP_SINK "non-dtcp-sink"
#define ELEMENT_NAME_DTCP_SINK "dtcp-sink"

#define MAX_HTTP_REQUEST_SIZE 256
static const char CRLF[] = "\r\n";

// Structure describing details of this element, used when initializing element
//
const GstElementDetails gst_dlna_bin_details
= GST_ELEMENT_DETAILS("HTTP/DLNA client source 11/8/12 10:26 AM",
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

	g_object_class_install_property (gobject_klass, ARG_URI,
			g_param_spec_string ("uri", "Stream URI",
					"Sets URI A/V stream",
					NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

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
    GST_DEBUG_OBJECT(dlna_bin, "Initializing");

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

    GST_INFO_OBJECT(dlna_bin, "Setting property: %d", prop_id);

    switch (prop_id) {

	case ARG_URI:
	{
		if (!dlna_bin_setup_uri(dlna_bin, value))
		{
		    GST_ERROR_OBJECT(dlna_bin, "Failed to set URI property");
		}
	}
	break;

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

	case ARG_URI:
		g_value_set_pointer(value, dlna_bin->uri);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
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
	GST_DEBUG_OBJECT(dlna_bin, "Building bin");

	//gst_dlna_build_bin(dlna_bin);
	//GstElement *http_src;
	//GstPad *pad, *gpad;

	// Create source element
	dlna_bin->http_src = gst_element_factory_make ("souphttpsrc", ELEMENT_NAME_HTTP_SRC);
	if (!dlna_bin->http_src) {
		GST_DEBUG_OBJECT(dlna_bin, "The source element could not be created. Exiting.\n");
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
	GST_DEBUG_OBJECT(dlna_bin, "Setting up URI");

	GstElement *elem;

	// Set the uri in the bin
	// (ew) Do we need to free the old value?
	if (dlna_bin->uri) {
		free(dlna_bin->uri);
	}
	dlna_bin->uri = g_value_dup_string(value);

	// Get the http source
	elem = gst_bin_get_by_name(&dlna_bin->bin, ELEMENT_NAME_HTTP_SRC);

	// Set the URI
	g_object_set(G_OBJECT(elem), "location", dlna_bin->uri, NULL);

	GST_INFO_OBJECT(dlna_bin, "Set the URI to %s\n", dlna_bin->uri);

	// Parse URI to get socket info & content info to send head request
	if (!dlna_bin_parse_uri(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems parsing URI");
		return FALSE;
	}

	// Open socket to send HEAD request
	if (!dlna_bin_open_socket(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems creating socket to send HEAD request\n");
		return FALSE;
	}

	// Formulate HEAD request
	if (!dlna_bin_formulate_head_request(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems formulating HEAD request\n");
		return FALSE;
	}

	// Send HEAD Request and read response
	if (!dlna_bin_issue_head_request(dlna_bin))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems sending and receiving HEAD request\n");
		return FALSE;
	}

	// Setup elements based on HEAD response
	if (dlna_bin->is_dtcp_encrypted)
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
	GST_INFO_OBJECT(dlna_bin, "Parsing URI: %s", dlna_bin->uri);

	// URI format is:
	// <scheme>:<hierarchical_part>[?query][#fragment]
	// where hierarchical part is:
	// [user_info@]<host_info>[:port][/path]
	// where host info can be ip address
	//
	// An example is:
	// http://192.168.0.111:8008/ocaphn/recording?rrid=1&profile=MPEG_TS_SD_NA_ISO&mime=video/mpeg

	// Verify scheme is HTTP if one was supplied.
	// Scheme could not be specified or not (absolute URIs include scheme, relative URIs do not)
	/*
	int i = 0;
	char* scheme = NULL;
	scheme = strtok(dlna_bin->uri, "://");
	if (scheme != NULL)
	{
		// Convert scheme to uppercase
		for (i = 0; scheme[i]; i++)
			scheme[i] = toupper(scheme[i]);
		if (strcmp(scheme, "HTTP") != 0)
		{
			g_printerr ("Only supporting HTTP URIs, unsupported URI: %s\n",
					dlna_bin->uri);
			return FALSE;
		}
	}
	else
	{
		g_printerr ("No scheme specified, assuming relative URI: %s\n", dlna_bin->uri);
	}
	printf("%s() - URI scheme supported: %s\n", __FUNCTION__, scheme);

	// Extract host and port
	dlna_bin->uri_addr = strtok(NULL, "://");
	char* portStr = strtok(NULL, "://");
	if (portStr != NULL)
	{
		dlna_bin->uri_port = strtol(portStr, NULL, 10);
	}
	else
	{	printf("%s() - called with URI: %s\n", __FUNCTION__, );

		dlna_bin->uri_port = 80;
	}
	printf("%s() - URI address: %s\n", __FUNCTION__, dlna_bin->uri_addr);
	printf("%s() - URI port: %d\n", __FUNCTION__, dlna_bin->uri_port);
*/
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
                dlna_bin->uri_addr = g_strdup(addr);
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
	GST_DEBUG_OBJECT(dlna_bin, "Opening socket to URI src");

	char portStr[8] = {0};
    struct addrinfo hints = {0};
    struct addrinfo* srvrInfo = NULL;
    struct addrinfo* pSrvr = NULL;
    int yes = 1;
    int ret = 0;

#ifdef RI_WIN32_SOCKETS
    WSADATA wsd;

    if (WSAStartup(MAKEWORD(2, 2), &wsd))
    {
        GST_ERROR_OBJECT(src, "%s WSAStartup() failed?\n", __func__);
        return FALSE;
    }
#endif

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(portStr, sizeof(portStr), "%d", dlna_bin->uri_port);

    if (0 != (ret = getaddrinfo(dlna_bin->uri_addr, portStr, &hints, &srvrInfo)))
    {
        GST_ERROR_OBJECT(dlna_bin, "%s getaddrinfo[%s]\n", __func__,
                         gai_strerror(ret));
        return FALSE;
    }

    for(pSrvr = srvrInfo; pSrvr != NULL; pSrvr = pSrvr->ai_next)
    {
        if (0 > (dlna_bin->sock = socket(pSrvr->ai_family,
                                    pSrvr->ai_socktype,
                                    pSrvr->ai_protocol)))
        {
            GST_ERROR_OBJECT(dlna_bin, "%s socket() failed?\n", __func__);
            continue;
        }

        if (0 > setsockopt(dlna_bin->sock, SOL_SOCKET, SO_REUSEADDR,
                           (char*) &yes, sizeof(yes)))
        {
            GST_ERROR_OBJECT(dlna_bin, "%s setsockopt() failed?\n", __func__);
            return FALSE;
        }

        GST_INFO_OBJECT(dlna_bin, "Got sock: %d\n", dlna_bin->sock);

        if (0 > (bind(dlna_bin->sock, pSrvr->ai_addr, pSrvr->ai_addrlen)))
        {
            CLOSESOCK(dlna_bin->sock);
            GST_ERROR_OBJECT(dlna_bin, "%s bind() failed?\n", __func__);
            continue;
        }

        // We successfully bound!
        break;
    }

    if (NULL == pSrvr)
    {
        GST_ERROR_OBJECT(dlna_bin, "%s failed to bind\n", __func__);
        freeaddrinfo(srvrInfo);
        return FALSE;
    }

    freeaddrinfo(srvrInfo);
    return TRUE;
}

/**
 * Creates the string which represents the HEAD request to send
 * to server to get info related to URI
 *
 * @param dlna_bin	this element
 *
 * @return	true if sucessful, false otherwise
 */
static gboolean
dlna_bin_formulate_head_request(GstDlnaBin *dlna_bin)
{
	GST_DEBUG_OBJECT(dlna_bin, "Formulating head request");

    char requestStr[MAX_HTTP_REQUEST_SIZE];
    char tmpStr[32];

    strcpy(requestStr, "HEAD ");

    strcat(requestStr, dlna_bin->uri);

    strcat(requestStr, " HTTP/1.1");
    strcat(requestStr, CRLF);

    strcat(requestStr, "HOST: ");
    strcat(requestStr, dlna_bin->uri_addr);
    strcat(requestStr, ":");
    (void) memset((char *)&tmpStr, 0, sizeof(tmpStr));
    sprintf(tmpStr, "%d", dlna_bin->uri_port);
    strcat(requestStr, tmpStr);
    strcat(requestStr, CRLF);

    // Include request to get content features
    strcat(requestStr, "getcontentFeatures.dlna.org : 1");
    strcat(requestStr, CRLF);

    // Include available seek range
    strcat(requestStr, "getAvailableSeekRange.dlna.org : 1");
    strcat(requestStr, CRLF);

    // Include time seek range if supported
    strcat(requestStr, "TimeSeekRange.dlna.org : npt=0-");
    strcat(requestStr, CRLF);

    // Add termination characters for overall request
    strcat(requestStr, CRLF);

    /*
    // Request memory to store request
    int requestSize = strlen(requestStr) + 1;

    if (mpe_memAlloc(requestSize, (void **) &player->httpHeadRequestStr)
            != MPE_SUCCESS)
    {
        MPEOS_LOG(MPE_LOG_ERROR, MPE_MOD_HN,
                "%s() - Could not allocate %d bytes of memory for HEAD request string\n",
                __FUNCTION__, requestSize);
        return MPE_HN_ERR_OS_FAILURE;
    }
    else
    {
        memcpy(dlna_bin->head_request_str, requestStr, requestSize);
    }
    */

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
	GST_INFO_OBJECT(dlna_bin, "Issuing head request: %s", dlna_bin->head_request_str);

	dlna_bin->is_dtcp_encrypted = FALSE;

	return TRUE;
}

/**
 * Setup this bin in order to handle non-DTCP encrypted content
 *
 * @param dlna_bin	this element
 */
static gboolean
dlna_bin_non_dtcp_setup(GstDlnaBin *dlna_bin)
{
	GST_INFO_OBJECT(dlna_bin, "Creating non-dtcp sink");

	// Create non-encrypt sink element
	GstElement *non_dtcp_sink = gst_element_factory_make ("filesink", ELEMENT_NAME_NON_DTCP_SINK);
	if (!non_dtcp_sink) {
		GST_ERROR_OBJECT(dlna_bin, "The sink element could not be created. Exiting.\n");
		return FALSE;
	}

	// *TODO* - this should be property of this element???
	// Hardcode the file sink location to be tmp.txt
	g_object_set (G_OBJECT(non_dtcp_sink), "location", "non_dtcp.txt", NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), non_dtcp_sink);

	// Link elements together
	if (!gst_element_link_many(dlna_bin->http_src, non_dtcp_sink, NULL))
	{
		GST_ERROR_OBJECT(dlna_bin, "Problems linking elements in bin. Exiting.\n");
		return FALSE;
	}
	return TRUE;
}

/**
 * Setup this bin in order to handle DTCP encrypted content
 *
 * @param dlna_bin	this element
 */
static gboolean
dlna_bin_dtcp_setup(GstDlnaBin *dlna_bin)
{
	GST_INFO_OBJECT(dlna_bin, "Creating dtcp sink");

	// Create non-encrypt sink element
	GstElement *dtcp_sink = gst_element_factory_make ("filesink", ELEMENT_NAME_DTCP_SINK);
	if (!dtcp_sink) {
		GST_ERROR_OBJECT(dlna_bin, "The sink element could not be created. Exiting.\n");
		return FALSE;
	}

	// *TODO* - this should be property of this element???
	// Hardcode the file sink location to be tmp.txt
	g_object_set (G_OBJECT(dtcp_sink), "location", "dtcp.txt", NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), dtcp_sink);

	// Link elements together
	if (!gst_element_link_many(dlna_bin->http_src, dtcp_sink, NULL))
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
		"http://gstreamer.net/"
)

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
