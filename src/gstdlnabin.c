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

#include <string.h>
#include <gst/gst.h>
#include <glib-object.h>

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

// Structure describing details of this element, used when initializing element
//
const GstElementDetails gst_dlna_bin_details
= GST_ELEMENT_DETAILS("HTTP/DLNA client source -11/7/12 9:00",
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

static void dlna_bin_non_dtcp_setup(GstDlnaBin *dlna_bin);

// **********************
// Local method declarations
//
static GstDlnaBin* gst_dlna_build_bin (GstDlnaBin *dlna_bin);

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
	printf("%s() - Initializing the dlna bin\n", __FUNCTION__);

	gst_dlna_build_bin(dlna_bin);

	printf("%s() - Initializing the dlna bin - done\n", __FUNCTION__);
}

/**
 * Called by framework when tearing down pipeline
 *
 * @param object  element to destroy
 */
static void
gst_dlna_bin_dispose (GObject * object)
{
	printf("%s() - Disposing the dlna bin\n", __FUNCTION__);

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
	GstDlnaBin *dlna_bin;

	dlna_bin = GST_DLNA_BIN (object);

	switch (prop_id) {

	case ARG_URI:
	{
		GstElement *elem;

		printf("%s() - Setting the URI property\n", __FUNCTION__);

		// Set the uri in the bin
		// (ew) Do we need to free the old value?
		if (dlna_bin->uri) {
			free(dlna_bin->uri);
		}
		dlna_bin->uri = g_value_dup_string(value);

		// Get the http source
		elem = gst_bin_get_by_name(&dlna_bin->bin, "http-source");

		printf("%s() - Setting the URI to %s\n", __FUNCTION__, dlna_bin->uri);

		// Set the URI
		g_object_set(G_OBJECT(elem), "location", dlna_bin->uri, NULL);

		dlna_bin_non_dtcp_setup(dlna_bin);
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
	GstDlnaBin *dlna_bin;

	dlna_bin = GST_DLNA_BIN (object);

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
	printf("%s() - Initializing the dlna bin\n", __FUNCTION__);

	//gst_dlna_build_bin(dlna_bin);
	//GstElement *http_src;
	//GstPad *pad, *gpad;

	// Create source element
	dlna_bin->http_src = gst_element_factory_make ("souphttpsrc", ELEMENT_NAME_HTTP_SRC);
	if (!dlna_bin->http_src) {
		g_printerr ("The source element could not be created. Exiting.\n");
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
	printf("%s() - Initializing the dlna bin - done\n", __FUNCTION__);
}

/**
 * Setup this bin in order to handle non-DTCP encrypted content
 *
 * @param dlna_bin	this element
 */
static void
dlna_bin_non_dtcp_setup(GstDlnaBin *dlna_bin)
{
	printf("%s() - Creating non-dtcp sink\n");

	GstElement *non_dtcp_sink;

	// Create non-encrypt sink element
	non_dtcp_sink = gst_element_factory_make ("filesink", ELEMENT_NAME_NON_DTCP_SINK);
	if (!non_dtcp_sink) {
		g_printerr ("The sink element could not be created. Exiting.\n");
		exit(1);
	}

	// *TODO* - this should be property of this element???
	// Hardcode the file sink location to be tmp.txt
	g_object_set (G_OBJECT(non_dtcp_sink), "location", "non_dtcp.txt", NULL);

	// Add this element to the bin
	gst_bin_add(GST_BIN(&dlna_bin->bin), non_dtcp_sink);

	// Link elements together
	if (!gst_element_link_many(dlna_bin->http_src, non_dtcp_sink, NULL))
	{
		g_printerr ("Problems linking elements in bin. Exiting.\n");
		exit(1);
	}
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
