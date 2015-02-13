EXTRA_DIST = autogen.sh

# Note: plugindir is set in configure

# src folder

##############################################################################
# TODO: change libgstplugin.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = src/libgstdlnasrc.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
src_libgstdlnasrc_la_SOURCES = src/gstdlnasrc.c src/gstdlnasrc.h 

# compiler and linker flags used to compile this plugin, set in configure.ac
src_libgstdlnasrc_la_CFLAGS = $(GST_CFLAGS) $(SOUP_CFLAGS) $(URI_PARSER_CFLAGS)
src_libgstdlnasrc_la_CXXFLAGS = $(GST_CFLAGS)
src_libgstdlnasrc_la_LIBADD = $(GST_LIBS) $(SOUP_LIBS) $(URI_PARSER_LIBS)
src_libgstdlnasrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
src_libgstdlnasrc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = src/gstdlnasrc.h
