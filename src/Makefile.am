# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstgzdec.la

# sources used to compile this plug-in
libgstgzdec_la_SOURCES = gstgzdec.c gstgzdec.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstgzdec_la_CFLAGS = $(GST_CFLAGS) # $(shell pkg-config --cflags zlib)
libgstgzdec_la_LIBADD = $(GST_LIBS)
libgstgzdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lz # $(shell pkg-config --libs zlib)
libgstgzdec_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstgzdec.h
