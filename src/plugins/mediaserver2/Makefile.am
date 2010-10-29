include $(top_srcdir)/src/plugins/Makefile.plugins

plugindir = $(PLUGINDIR)/mediaserver2
plugin_LTLIBRARIES = libmediaserver2.la

plugin_in_files = mediaserver2.plugin.in

uidir = $(plugindir)

libmediaserver2_la_SOURCES = totem-mediaserver2.c
libmediaserver2_la_LDFLAGS = $(plugin_ldflags)
libmediaserver2_la_LIBADD = $(LIBMS2_LIBS)
libmediaserver2_la_CFLAGS = \
        $(plugin_cflags)        \
        $(LIBMS2_CFLAGS)

-include $(top_srcdir)/git.mk

